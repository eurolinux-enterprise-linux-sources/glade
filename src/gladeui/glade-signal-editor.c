/*
 * Copyright (C) 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors:
 *   Shane Butler <shane_b@users.sourceforge.net>
 *   Joaquin Cuenca Abela <e98cuenc@yahoo.com>
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/**
 * SECTION:glade-signal-editor
 * @Title: GladeSignalEditor
 * @Short_Description: An interface to edit signals for a #GladeWidget.
 *W
 */

#include <string.h>
#include <glib/gi18n-lib.h>

#include "glade.h"
#include "glade-widget.h"
#include "glade-widget-adaptor.h"
#include "glade-signal.h"
#include "glade-signal-editor.h"
#include "glade-signal-model.h"
#include "glade-cell-renderer-icon.h"
#include "glade-editor.h"
#include "glade-command.h"
#include "glade-marshallers.h"
#include "glade-accumulators.h"
#include "glade-project.h"
#include "glade-cell-renderer-icon.h"

G_DEFINE_TYPE (GladeSignalEditor, glade_signal_editor, GTK_TYPE_VBOX)

#define GLADE_SIGNAL_EDITOR_GET_PRIVATE(o)  \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), GLADE_TYPE_SIGNAL_EDITOR, GladeSignalEditorPrivate))

struct _GladeSignalEditorPrivate
{
  GtkTreeModel *model;
	
  GladeWidget *widget;
  GladeWidgetAdaptor *adaptor;

  GtkWidget *signal_tree;
  GtkTreeViewColumn *column_name;
  GtkTreeViewColumn *column_detail;
  GtkTreeViewColumn *column_handler;
  GtkTreeViewColumn *column_userdata;
  GtkTreeViewColumn *column_swap;
  GtkTreeViewColumn *column_after;

  GtkCellRenderer *renderer_userdata;

  GtkListStore *detail_store;
  GtkListStore *handler_store;
};

enum
{
  SIGNAL_ACTIVATED,
  CALLBACK_SUGGESTIONS,
  DETAIL_SUGGESTIONS,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_GLADE_WIDGET
};

static guint glade_signal_editor_signals[LAST_SIGNAL] = { 0 };

/* Utils */
static inline gboolean
glade_signal_is_dummy (GladeSignal *signal)
{
  return glade_signal_get_handler (signal) == NULL;
}

/* Signal handlers */
static void
on_handler_edited (GtkCellRendererText *renderer,
                   gchar *path,
                   gchar *handler,
                   gpointer user_data)
{
  GladeSignalEditor *self = GLADE_SIGNAL_EDITOR (user_data);
  GtkTreePath *tree_path = gtk_tree_path_new_from_string (path);
  GtkTreeIter iter;
  gchar *old_handler;
  GladeSignal *signal;
  gboolean dummy;

  g_return_if_fail (self->priv->widget != NULL);
	
  gtk_tree_model_get_iter (self->priv->model,
			   &iter,
			   tree_path);

  gtk_tree_model_get (self->priv->model, &iter,
		      GLADE_SIGNAL_COLUMN_HANDLER, &old_handler, 
		      GLADE_SIGNAL_COLUMN_SIGNAL, &signal, -1);

  dummy = glade_signal_is_dummy (signal);
	
  /* False alarm ? */
  if (handler && !g_str_equal (old_handler, handler))
    {
      if (!dummy)
	{
	  if (strlen (handler))
	    {
	      /* change an existing signal handler */
	      GladeSignal *old_signal;
	      GladeSignal *new_signal;
				
	      gtk_tree_model_get (self->priv->model,
				  &iter,
				  GLADE_SIGNAL_COLUMN_SIGNAL,
				  &old_signal, -1);

	      new_signal = glade_signal_clone (old_signal);

	      /* Change the new signal handler */
	      glade_signal_set_handler (new_signal, handler);

	      glade_command_change_signal (self->priv->widget, old_signal, new_signal);

              g_object_unref (old_signal);
	      g_object_unref (new_signal);
	    }
	  else
	    {
	      GladeSignal *deleted_signal;
	      gtk_tree_model_get (self->priv->model,
				  &iter,
				  GLADE_SIGNAL_COLUMN_SIGNAL,
				  &deleted_signal, -1);
				
				
	      /* Delete signal */
	      glade_command_remove_signal (self->priv->widget, deleted_signal);
	    }
	}
      else if (strlen (handler))
	{
	  GladeSignal *new_signal;
			
	  /* Get the signal name */
	  gtk_tree_model_get (self->priv->model, &iter,
			      GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
			      -1);
			
	  /* Add a new signal handler */
	  new_signal = glade_signal_new (glade_signal_get_class (signal),
                                         handler, NULL, FALSE, FALSE);
          glade_signal_set_detail (new_signal, glade_signal_get_detail (signal));
	  glade_command_add_signal (self->priv->widget, new_signal);
          glade_signal_set_detail (signal, NULL);
	  g_object_unref (new_signal);
	}
    }

  g_object_unref (signal);
  g_free (old_handler);
  gtk_tree_path_free (tree_path);
}

static gchar **
glade_signal_editor_callback_suggestions (GladeSignalEditor *editor,
					  GladeSignal *signal)
{
  GladeWidget *widget = glade_signal_editor_get_widget (editor);
  gchar *signal_name, **suggestions = g_new (gchar *, 10);
  const gchar *name, *detail;

  if ((detail = glade_signal_get_detail (signal)))
    signal_name = g_strdup_printf ("%s_%s", detail, glade_signal_get_name (signal));
  else
    signal_name = g_strdup (glade_signal_get_name (signal));

  glade_util_replace (signal_name, '-', '_');
  
  name = glade_widget_get_name (widget);
  
  suggestions[0] = g_strdup_printf ("on_%s_%s", name, signal_name);
  suggestions[1] = g_strdup_printf ("%s_%s_cb", name, signal_name);
  suggestions[2] = g_strdup ("gtk_widget_show");
  suggestions[3] = g_strdup ("gtk_widget_hide");
  suggestions[4] = g_strdup ("gtk_widget_grab_focus");
  suggestions[5] = g_strdup ("gtk_widget_destroy");
  suggestions[6] = g_strdup ("gtk_true");
  suggestions[7] = g_strdup ("gtk_false");
  suggestions[8] = g_strdup ("gtk_main_quit");
  suggestions[9] = NULL;

  return suggestions;
}

static gchar **
glade_signal_editor_detail_suggestions (GladeSignalEditor *editor,
                                        GladeSignal *signal)
{
  /* We only support suggestions for notify signal */
  if (!g_strcmp0 (glade_signal_get_name (signal), "notify"))
    {
      GladeSignalEditorPrivate *priv = editor->priv;
      const GList *l, *properties = glade_widget_adaptor_get_properties (priv->adaptor);
      gchar **suggestions = g_new (gchar *, g_list_length ((GList *)properties) + 1);
      gint i;

      for (i = 0, l = properties; l; l = g_list_next (l))
        {
          GladePropertyClass *prop = l->data;

          if (!glade_property_class_is_visible (prop) || 
              glade_property_class_get_virtual (prop)) continue;

          suggestions[i++] = g_strdup (glade_property_class_id (prop));
        }
      
      suggestions[i] = NULL;
      
      return suggestions;
    }

  return NULL;
}

static void
gse_entry_completion_ensure_model (GtkEntry *entry, GtkTreeModel *model)
{
  GtkEntryCompletion *completion = gtk_entry_completion_new ();

  gtk_entry_completion_set_text_column (completion, 0);
  gtk_entry_completion_set_minimum_key_length (completion, 0);
  gtk_entry_completion_set_inline_completion (completion, FALSE);
  gtk_entry_completion_set_inline_selection (completion, TRUE);
  gtk_entry_completion_set_popup_completion (completion, TRUE);

  gtk_entry_completion_set_model (completion, model);

  gtk_entry_set_completion (entry, completion);
}

static void
on_detail_editing_started (GtkCellRenderer *renderer,
                           GtkCellEditable *editable,
                           gchar *path,
                           gpointer user_data)
{
  /* Check if editable is still an entry */
  if (GTK_IS_ENTRY (editable))
    {
      GladeSignalEditor *self = GLADE_SIGNAL_EDITOR (user_data);
      GladeSignalEditorPrivate *priv = self->priv;
      GtkEntry *entry = GTK_ENTRY (editable);
      GtkTreePath *tree_path;
      GtkTreeIter iter;
      GladeSignal *signal;
      gchar **suggestions;

      tree_path = gtk_tree_path_new_from_string (path);
      gtk_tree_model_get_iter (priv->model, &iter, tree_path);
      gtk_tree_path_free (tree_path);
      
      gtk_tree_model_get (priv->model, &iter,
			  GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
			  -1);
      
      if (glade_signal_get_detail (signal) == NULL)
	  gtk_entry_set_text (entry, "");

      g_object_unref (signal);

      gtk_entry_set_completion (entry, NULL);
      gtk_list_store_clear (priv->detail_store);
      
      g_signal_emit (self, glade_signal_editor_signals [DETAIL_SUGGESTIONS], 0, signal, &suggestions);
      
      if (suggestions)
        {
          register GtkListStore *store = priv->detail_store;
          gint i;

          for (i = 0; suggestions[i]; i++)
            {
              gtk_list_store_append (store, &iter);
              gtk_list_store_set (store, &iter, 0, suggestions[i], -1);
            }

          gse_entry_completion_ensure_model (entry, GTK_TREE_MODEL (store));

          g_strfreev (suggestions);
        }
    }
}

static void
on_detail_edited (GtkCellRendererText *renderer,
                  gchar *path,
                  gchar *detail,
                  gpointer user_data)
{
  GladeSignalEditor *self = GLADE_SIGNAL_EDITOR(user_data);
  GtkTreePath *tree_path = gtk_tree_path_new_from_string (path);
  GtkTreeIter iter;
  gchar *old_detail;

  g_return_if_fail (self->priv->widget != NULL);
	
  gtk_tree_model_get_iter (self->priv->model, &iter, tree_path);

  gtk_tree_model_get (self->priv->model, &iter,
		      GLADE_SIGNAL_COLUMN_DETAIL, &old_detail, -1);

  if (detail && strlen (detail) && g_strcmp0 (old_detail, detail))
    {
      /* change an existing signal detail */
      GladeSignal *old_signal;

      gtk_tree_model_get (self->priv->model,
                          &iter,
                          GLADE_SIGNAL_COLUMN_SIGNAL,
                          &old_signal, -1);

      if (glade_signal_is_dummy (old_signal))
        {
          glade_signal_set_detail (old_signal, detail);
        }
      else
        {
          GladeSignal *new_signal = glade_signal_clone (old_signal);
          glade_signal_set_detail (new_signal, detail);
          glade_command_change_signal (self->priv->widget, old_signal, new_signal);
          g_object_unref (new_signal);
        }

      g_object_unref (old_signal);
    }
  g_free (old_detail);
  gtk_tree_path_free (tree_path);
}

static void
on_handler_editing_started (GtkCellRenderer *renderer,
                            GtkCellEditable *editable,
                            gchar *path,
                            gpointer user_data)
{
  /* Check if editable is still an entry */
  if (GTK_IS_ENTRY (editable))
    {
      GladeSignalEditor *self = GLADE_SIGNAL_EDITOR (user_data);
      GladeSignalEditorPrivate *priv = self->priv;
      GtkEntry *entry = GTK_ENTRY (editable);
      GtkTreePath *tree_path;
      GtkTreeIter iter;
      GladeSignal *signal;
      gchar **suggestions;
      gint i;

      tree_path = gtk_tree_path_new_from_string (path);
      gtk_tree_model_get_iter (priv->model, &iter, tree_path);
      gtk_tree_path_free (tree_path);
      
      gtk_tree_model_get (priv->model, &iter,
			  GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
			  -1);
      
      if (glade_signal_is_dummy (signal))
	  gtk_entry_set_text (entry, "");

      g_signal_emit (self, glade_signal_editor_signals [CALLBACK_SUGGESTIONS], 0, signal, &suggestions);

      g_object_unref (signal);

      gtk_entry_set_completion (entry, NULL);
      gtk_list_store_clear (priv->handler_store);

      if (suggestions)
        {
          register GtkListStore *store = priv->handler_store;
          for (i = 0; suggestions[i]; i++)
            {
              gtk_list_store_append (store, &iter);
              gtk_list_store_set (store, &iter, 0, suggestions[i], -1);
            }

          gse_entry_completion_ensure_model (entry, GTK_TREE_MODEL (store));
          g_strfreev (suggestions);
        }
    }
}

static void
glade_signal_editor_user_data_activate (GtkCellRenderer *icon_renderer,
                                        const gchar *path_str,
                                        GladeSignalEditor *editor)
{
  GladeSignalEditorPrivate *priv = editor->priv;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
  GtkTreeModel *model = priv->model;
  GtkTreeIter iter;
  GladeWidget *project_object = NULL;
  GladeProject *project;

  GladeSignal *signal;
	
  GList *selected = NULL; 
  GList *exception = NULL;

  gboolean dummy;
	
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL, &signal, -1);
  dummy = glade_signal_is_dummy (signal);

  if (!dummy)
    {
      project = glade_widget_get_project (priv->widget);

      if (glade_signal_get_userdata (signal))
	{
	  project_object =
	    glade_project_get_widget_by_name (project, glade_signal_get_userdata (signal));
	  selected = g_list_prepend (selected, project_object);
	}

      exception = g_list_prepend (exception, priv->widget);

      if (glade_editor_property_show_object_dialog (project,
						    _("Select an object to pass to the handler"),
						    gtk_widget_get_toplevel (GTK_WIDGET (editor)),
						    G_TYPE_OBJECT, priv->widget,
						    &project_object))
	{
	  GladeSignal *old_signal = signal;
	  GladeSignal *new_signal = glade_signal_clone (signal);

	  glade_signal_set_userdata (new_signal, 
				     project_object ? glade_widget_get_name (project_object) : NULL);

	  glade_command_change_signal (priv->widget, old_signal, new_signal);
	  g_object_unref (new_signal);
	}
    }
  g_object_unref (signal);
  gtk_tree_path_free (path);
}

static void
on_swap_toggled (GtkCellRendererToggle *renderer,
                 gchar *path,
                 gpointer user_data)
{
  GladeSignalEditor *self = GLADE_SIGNAL_EDITOR(user_data);
  GtkTreePath *tree_path = gtk_tree_path_new_from_string (path);
  GtkTreeIter iter;
  GladeSignal *old_signal;
  GladeSignal *new_signal;

  g_return_if_fail (self->priv->widget != NULL);
	
  gtk_tree_model_get_iter (self->priv->model,
			   &iter,
			   tree_path);

  gtk_tree_model_get (self->priv->model,
		      &iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL,
		      &old_signal, -1);

  new_signal = glade_signal_clone (old_signal);

  /* Change the new signal handler */
  glade_signal_set_swapped (new_signal,
			    !gtk_cell_renderer_toggle_get_active (renderer));

  glade_command_change_signal (self->priv->widget, old_signal, new_signal);

  g_object_unref (new_signal);
  g_object_unref (old_signal);

  gtk_tree_path_free (tree_path);		
}

static void
on_after_toggled (GtkCellRendererToggle *renderer,
                  gchar *path,
                  gpointer user_data)
{
  GladeSignalEditor *self = GLADE_SIGNAL_EDITOR(user_data);
  GtkTreePath *tree_path = gtk_tree_path_new_from_string (path);
  GtkTreeIter iter;
  GladeSignal *old_signal;
  GladeSignal *new_signal;

  g_return_if_fail (self->priv->widget != NULL);
	
  gtk_tree_model_get_iter (self->priv->model,
			   &iter,
			   tree_path);

  gtk_tree_model_get (self->priv->model,
		      &iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL,
		      &old_signal, -1);

  new_signal = glade_signal_clone (old_signal);

  /* Change the new signal handler */
  glade_signal_set_after (new_signal, 
			  !gtk_cell_renderer_toggle_get_active (renderer));

  glade_command_change_signal (self->priv->widget, old_signal, new_signal);

  g_object_unref (new_signal);
  g_object_unref (old_signal);
	
  gtk_tree_path_free (tree_path);	
}

static void
glade_signal_editor_devhelp (GtkCellRenderer *cell,
                             const gchar *path_str,
                             GladeSignalEditor *editor)
{
  GladeSignalEditorPrivate *priv = editor->priv;
  GtkTreePath              *path = gtk_tree_path_new_from_string (path_str);
  GtkTreeModel             *model = priv->model;
  GtkTreeIter               iter;
  GladeWidgetAdaptor       *adaptor;
  const GladeSignalClass   *signal_class;
  GladeSignal              *signal;
  gchar                    *book;
  gchar                    *search;	

  g_return_if_fail (gtk_tree_model_get_iter (model, &iter, path));
  gtk_tree_path_free (path);

  gtk_tree_model_get (model, &iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
		      -1);
  signal_class = glade_signal_get_class (signal);
  adaptor = glade_signal_class_get_adaptor (signal_class);
  g_object_get (adaptor, "book", &book, NULL);

  search = g_strdup_printf ("The %s signal", glade_signal_get_name (signal));

  glade_app_search_docs (book, glade_widget_adaptor_get_name (adaptor), search);

  g_free (search);
  g_free (book);
  g_object_unref (signal);
}

static void
glade_signal_editor_get_property  (GObject    *object,
				   guint       prop_id,
				   GValue     *value,
				   GParamSpec *pspec)
{
  GladeSignalEditor *self = GLADE_SIGNAL_EDITOR (object);
  GladeSignalEditorPrivate *priv = self->priv;

  switch (prop_id)
    {
      case PROP_GLADE_WIDGET:
        g_value_set_object (value, priv->widget);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
glade_signal_editor_set_property  (GObject      *object,
				   guint         prop_id,
				   const GValue *value,
				   GParamSpec   *pspec)
{
  GladeSignalEditor *self = GLADE_SIGNAL_EDITOR (object);

  switch (prop_id)
    {
      case PROP_GLADE_WIDGET:
        glade_signal_editor_load_widget (self, g_value_get_object (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

/**
 * glade_signal_editor_new:
 *
 * Returns: a new #GladeSignalEditor
 */
GladeSignalEditor *
glade_signal_editor_new ()
{
  GladeSignalEditor *signal_editor;

  signal_editor = GLADE_SIGNAL_EDITOR (g_object_new (GLADE_TYPE_SIGNAL_EDITOR,
						     NULL, NULL));

  return signal_editor;
}

/**
 * glade_signal_editor_load_widget:
 * @editor: a #GladeSignalEditor
 * @widget: a #GladeWidget or NULL
 *
 * Load a widget in the signal editor. This will show all signals of the widget
 * an it's accessors in the signal editor where the user can edit them.
 */
void
glade_signal_editor_load_widget (GladeSignalEditor *editor,
				 GladeWidget *widget)
{
  GladeSignalEditorPrivate *priv = editor->priv;
  GtkTreePath *path;
  GtkTreeIter  iter;
	
  if (priv->widget != widget)
    {	
      priv->widget = widget;
      priv->adaptor = widget ? glade_widget_get_adaptor (widget) : NULL;
		
      if (priv->widget)
	{
	  g_object_ref (priv->widget);
	}
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (priv->signal_tree), NULL);
  priv->model = NULL;

	
  if (!widget)
    return;

  priv->model = glade_widget_get_signal_model (widget);
  gtk_tree_view_set_model (GTK_TREE_VIEW (priv->signal_tree), priv->model);

  if (gtk_tree_model_iter_children (priv->model, &iter, NULL))
    {
      path = gtk_tree_model_get_path (priv->model, &iter);
      gtk_tree_view_expand_row (GTK_TREE_VIEW (priv->signal_tree), path, FALSE);
      gtk_tree_path_free (path);
    }
}

/**
 * glade_signal_editor_enable_dnd:
 * @editor: a #GladeSignalEditor
 * @enabled: whether the drag and drop support should be enabled
 *
 * If drag and drop support is enabled, the user will be able to drag signal handler
 * from the tree to some editor. The type of the dnd data will be "application/x-glade-signal"
 * and it will be in the form of "widget:signal:handler" so for example 
 * "GtkToggleButton:toggled:on_toggle_button_toggled".
 */ 
void 
glade_signal_editor_enable_dnd (GladeSignalEditor *editor, gboolean enabled)
{
  GladeSignalEditorPrivate *priv = editor->priv;
  if (enabled)
    {
      const GtkTargetEntry entry = {
	"application/x-glade-signal",
	GTK_TARGET_OTHER_WIDGET,
	1
      };
      /* DND */
      gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW(priv->signal_tree),
					      GDK_BUTTON1_MASK,
					      &entry,
					      1,
					      GDK_ACTION_COPY);
    }
  else
    {
      gtk_tree_view_unset_rows_drag_source (GTK_TREE_VIEW (priv->signal_tree));
    }
}

static void
glade_signal_editor_dispose (GObject *object)
{
  GladeSignalEditorPrivate *priv = GLADE_SIGNAL_EDITOR_GET_PRIVATE (object);

  g_clear_object (&priv->detail_store);
  g_clear_object (&priv->handler_store);
  
  G_OBJECT_CLASS (glade_signal_editor_parent_class)->dispose (object);
}

#define BORDER 10

static cairo_surface_t*
create_rich_drag_surface (GtkWidget *widget, const gchar *text)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  GtkStateFlags state = gtk_widget_get_state_flags (widget);
  PangoLayout *layout = pango_layout_new (gtk_widget_get_pango_context (widget));
  cairo_t *cr;
  cairo_surface_t *s;
  gint width, height;
  GdkRGBA rgba;
	
  pango_layout_set_text (layout, text, -1);
  pango_layout_get_size (layout, &width, &height);
  width = PANGO_PIXELS(width) + BORDER;
  height = PANGO_PIXELS(height) + BORDER;

  s = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                                        CAIRO_CONTENT_COLOR,
                                        width,
                                        height);
  cr = cairo_create (s);

  gtk_style_context_get_background_color (context, state, &rgba);
  gdk_cairo_set_source_rgba (cr, &rgba);
  cairo_paint (cr);
  
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_move_to (cr, BORDER/2, BORDER/2);
  pango_cairo_show_layout (cr, layout);

  cairo_rectangle (cr, 0, 0, width, height);
  cairo_stroke (cr);

  cairo_destroy (cr);

  g_object_unref (layout);
	
  return s;
}

static void
glade_signal_editor_drag_begin (GtkWidget *widget,
                                GdkDragContext *context,
                                gpointer user_data)
{
  cairo_surface_t *s = NULL;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreeSelection *selection;

  selection =  gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      gchar* handler;
      gchar* text;
      gtk_tree_model_get (model, &iter,
			  GLADE_SIGNAL_COLUMN_HANDLER, &handler, -1);

      text = g_strdup_printf ("%s ()", handler);
      g_free (handler);
      
      s = create_rich_drag_surface (widget, text);
      g_free (text);
    }
	
  if (s)
    {
      gtk_drag_set_icon_surface (context, s);
      cairo_surface_destroy (s);
    }
  else
    {
      gtk_drag_set_icon_default (context);
    }
}


static void
glade_signal_editor_name_cell_data_func (GtkTreeViewColumn *column,
                                         GtkCellRenderer *renderer,
                                         GtkTreeModel *model,
                                         GtkTreeIter *iter,
                                         gpointer data)
{
  GladeSignal *signal;
  gboolean show_name;

  gtk_tree_model_get (model, iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
		      GLADE_SIGNAL_COLUMN_SHOW_NAME, &show_name,
		      -1);
  if (signal)
    {
      gboolean dummy;
		
      dummy = glade_signal_is_dummy (signal);
      if (!dummy && show_name)
	{
	  g_object_set (renderer, 
			"weight", PANGO_WEIGHT_BOLD,
			NULL);
	}
      else
	{
	  g_object_set (renderer, 
			"weight", PANGO_WEIGHT_NORMAL,
			NULL);
	}
      g_object_unref (signal);
    }
  else
    g_object_set (renderer, 
		  "weight", PANGO_WEIGHT_NORMAL,
		  NULL);
	
  g_object_set (renderer, 
		"visible", show_name,
		NULL);
}

static void
glade_signal_editor_handler_cell_data_func (GtkTreeViewColumn *column,
                                            GtkCellRenderer *renderer,
                                            GtkTreeModel *model,
                                            GtkTreeIter *iter,
                                            gpointer data)
{
  GladeSignalEditor* editor = GLADE_SIGNAL_EDITOR (data);
  GladeSignal* signal;
  GdkRGBA color;

  gtk_tree_model_get (model, iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
		      -1);
  if (signal)
    {
      gboolean dummy;
      GtkStyleContext* context = gtk_widget_get_style_context (editor->priv->signal_tree);
		
      dummy = glade_signal_is_dummy (signal);
      if (dummy)
	{
	  gtk_style_context_get_color (context, 
				       GTK_STATE_FLAG_INSENSITIVE, &color);
	  g_object_set (renderer, 
			"style", PANGO_STYLE_ITALIC,
			"foreground-rgba", &color,
			NULL);
	}
      else
	{
	  gtk_style_context_get_color (context, 
				       GTK_STATE_FLAG_NORMAL,
				       &color);
	  g_object_set (renderer,
			"style", PANGO_STYLE_NORMAL,
			"foreground-rgba", &color,
			NULL);
	}
      g_object_set (renderer,
		    "visible", TRUE,
		    "sensitive", TRUE,
		    "editable", TRUE,
		    NULL);

      g_object_unref (signal);
    }
  else
    {
      g_object_set (renderer, 
		    "visible", FALSE,
		    "editable", FALSE,
		    NULL);
    }
}

static void
glade_signal_editor_detail_cell_data_func (GtkTreeViewColumn *column,
                                           GtkCellRenderer *renderer,
                                           GtkTreeModel *model,
                                           GtkTreeIter *iter,
                                           gpointer data)
{
  GladeSignalEditor *editor = GLADE_SIGNAL_EDITOR (data);
  GladeSignal *signal;

  gtk_tree_model_get (model, iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
		      -1);
  if (signal &&
      (glade_signal_class_get_flags (glade_signal_get_class (signal)) & G_SIGNAL_DETAILED))
    {
      GdkRGBA color;
      gboolean dummy;
      GtkStyleContext* context = gtk_widget_get_style_context (editor->priv->signal_tree);

      dummy = glade_signal_is_dummy (signal);
      if (dummy || !glade_signal_get_detail (signal))
	{
	  gtk_style_context_get_color (context, 
				       GTK_STATE_FLAG_INSENSITIVE, &color);
	  g_object_set (renderer,
			"style", PANGO_STYLE_ITALIC,
			"foreground-rgba", &color,
			NULL);
	}
      else
	{
	  gtk_style_context_get_color (context, 
				       GTK_STATE_FLAG_NORMAL,
				       &color);
	  g_object_set (renderer,
			"style", PANGO_STYLE_NORMAL,
			"foreground-rgba", &color,
			NULL);
	}

      g_object_set (renderer,
                    "sensitive", TRUE,
                    "visible", TRUE,
                    "editable", TRUE,
                    NULL);

      g_object_unref (signal);
    }
  else
    {
      g_object_set (renderer,
                    "editable", FALSE,
                    "sensitive", FALSE,
		    "visible", FALSE,
		    NULL);
    }
}

static void
glade_signal_editor_data_cell_data_func (GtkTreeViewColumn *column,
                                         GtkCellRenderer *renderer,
                                         GtkTreeModel *model,
                                         GtkTreeIter *iter,
                                         gpointer data)
{
  GladeSignalEditor *editor = GLADE_SIGNAL_EDITOR (data);
  GladeSignal *signal;
  GdkRGBA color;

  gtk_tree_model_get (model, iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
		      -1);
  if (signal)
    {
      gboolean dummy;
		
      dummy = glade_signal_is_dummy (signal);
      g_object_set (renderer, 
		    "sensitive", !dummy,
		    "visible", TRUE,
		    NULL);

      if (GTK_IS_CELL_RENDERER_TEXT (renderer))
	{
	  GtkStyleContext* context = gtk_widget_get_style_context (editor->priv->signal_tree);

	  if (dummy || !glade_signal_get_userdata (signal))
	    {
	      gtk_style_context_get_color (context, GTK_STATE_FLAG_INSENSITIVE, &color);
	      g_object_set (renderer, 
			    "style", PANGO_STYLE_ITALIC,
			    "foreground-rgba", &color,
			    NULL);
	    }
	  else
	    {
	      gtk_style_context_get_color (context, 
					   GTK_STATE_FLAG_NORMAL,
					   &color);
	      g_object_set (renderer,
			    "style", PANGO_STYLE_NORMAL,
			    "foreground-rgba", &color,
			    NULL);
	    }
	}

      g_object_unref (signal);
    }
  else
    {
      g_object_set (renderer, 
		    "sensitive", FALSE,		              
		    "visible", FALSE,
		    NULL);
    }
}

static void
glade_signal_editor_warning_cell_data_func (GtkTreeViewColumn *column,
                                            GtkCellRenderer *renderer,
                                            GtkTreeModel *model,
                                            GtkTreeIter *iter,
                                            gpointer data)
{
  GladeSignal *signal;
  gboolean visible = FALSE;

  gtk_tree_model_get (model, iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
		      -1);
  if (signal)
    {
      const gchar* warning = glade_signal_get_support_warning (signal);
      visible = warning && strlen (warning);
      g_object_unref (signal);
    }
		
  g_object_set (renderer, 
		"visible", visible,
		NULL);
}

static void
glade_signal_editor_devhelp_cell_data_func (GtkTreeViewColumn *column,
                                            GtkCellRenderer *renderer,
                                            GtkTreeModel *model,
                                            GtkTreeIter *iter,
                                            gpointer data)
{
  GladeSignal *signal;

  gtk_tree_model_get (model, iter,
		      GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
		      -1);
  if (signal)
    {
      const GladeSignalClass *class = glade_signal_get_class (signal);
      GladeWidgetAdaptor *adaptor = glade_signal_class_get_adaptor (class);
      gchar *book;

      g_object_get (adaptor, "book", &book, NULL);
      g_object_set (renderer, 
		    "visible", book != NULL,
		    NULL);
      g_free (book);
      g_object_unref (signal);
    }
  else
    {
      g_object_set (renderer, 
		    "visible", FALSE,
		    NULL);
    }
}

GladeWidget*
glade_signal_editor_get_widget (GladeSignalEditor *editor)
{
    g_return_val_if_fail (GLADE_IS_SIGNAL_EDITOR(editor), NULL);
    return editor->priv->widget;
}

static void
glade_signal_editor_signal_activate (GtkTreeView       *tree_view,
                                     GtkTreePath       *path,
                                     GtkTreeViewColumn *column,
                                     GladeSignalEditor *editor)

{
  GladeSignalEditor *self = GLADE_SIGNAL_EDITOR (editor);
  if (self->priv->widget == NULL || column != self->priv->column_name)
    return;

  GladeSignal *signal = NULL;
  GtkTreeIter iter;
  gtk_tree_model_get_iter (self->priv->model,
                           &iter,
                           path);
  gtk_tree_model_get (self->priv->model, &iter,
                      GLADE_SIGNAL_COLUMN_SIGNAL, &signal,
                      -1);

  if(glade_signal_is_dummy (signal))
    return;

  g_signal_emit (editor, glade_signal_editor_signals[SIGNAL_ACTIVATED],
                 0, signal, NULL);

  g_object_unref (signal);
  return;
}

static void
glade_signal_editor_init (GladeSignalEditor *self)
{
  GtkWidget *scroll;
  GtkCellRenderer *renderer;
  GtkCellArea *cell_area;
  GladeSignalEditorPrivate *priv;
	
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GLADE_TYPE_SIGNAL_EDITOR, GladeSignalEditorPrivate);
  priv = self->priv;
	
  /* Create signal tree */
  priv->signal_tree = gtk_tree_view_new ();

  g_signal_connect (G_OBJECT (priv->signal_tree), "row-activated",
		    G_CALLBACK (glade_signal_editor_signal_activate),
		    self);

  /* Create columns */
  /* Signal name */
  priv->column_name = gtk_tree_view_column_new ();
	
  /* version warning */
  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (G_OBJECT (renderer), 
		"icon-name", GTK_STOCK_DIALOG_WARNING,
		"xalign", 0.0,
                NULL);
  gtk_tree_view_column_set_cell_data_func (priv->column_name, renderer,
					   glade_signal_editor_warning_cell_data_func,
					   self,
					   NULL);
  gtk_tree_view_column_pack_start (priv->column_name, renderer, FALSE);

  /* signal name */
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
		"ellipsize", PANGO_ELLIPSIZE_END,
		"width-chars", 20,
		NULL);
  gtk_tree_view_column_pack_end (priv->column_name, renderer, TRUE);
  gtk_tree_view_column_set_attributes (priv->column_name, renderer,
				       "text", GLADE_SIGNAL_COLUMN_NAME,
				       NULL);
  gtk_tree_view_column_set_cell_data_func (priv->column_name, renderer,
					   glade_signal_editor_name_cell_data_func,
					   self,
					   NULL);
  
  gtk_tree_view_column_set_resizable (priv->column_name, TRUE);
  gtk_tree_view_column_set_expand (priv->column_name, TRUE);
  
  gtk_tree_view_column_set_title (priv->column_name, _("Signal"));
  gtk_tree_view_append_column (GTK_TREE_VIEW (self->priv->signal_tree), priv->column_name);

  /* Signal detail */
  renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (renderer, "edited", G_CALLBACK(on_detail_edited), self);
  g_signal_connect (renderer, "editing-started", G_CALLBACK (on_detail_editing_started), self);
  priv->column_detail = gtk_tree_view_column_new_with_attributes (_("Detail"),
                                                                  renderer,
                                                                  "text", GLADE_SIGNAL_COLUMN_DETAIL,
                                                                  NULL);
  gtk_tree_view_column_set_cell_data_func (priv->column_detail, renderer,
					   glade_signal_editor_detail_cell_data_func,
					   self,
					   NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (self->priv->signal_tree), priv->column_detail);

  /* Signal handler */
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "editable", FALSE, NULL);
  g_signal_connect (renderer, "edited", G_CALLBACK(on_handler_edited), self);
  g_signal_connect (renderer, "editing-started", G_CALLBACK (on_handler_editing_started), self);
  priv->column_handler = gtk_tree_view_column_new_with_attributes (_("Handler"),
								   renderer,
								   "text", GLADE_SIGNAL_COLUMN_HANDLER,
								   NULL);
  gtk_tree_view_column_set_cell_data_func (priv->column_handler, renderer,
					   glade_signal_editor_handler_cell_data_func,
					   self,
					   NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (self->priv->signal_tree), priv->column_handler);
  
  /* Signal user_data */
  priv->renderer_userdata = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (priv->renderer_userdata),
		"editable", FALSE,
		"ellipsize", PANGO_ELLIPSIZE_END, 
		"width-chars", 10, NULL);

  cell_area = gtk_cell_area_box_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (cell_area),
			      priv->renderer_userdata,
			      TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (cell_area),
				  priv->renderer_userdata,
				  "text", GLADE_SIGNAL_COLUMN_OBJECT,
				  NULL);
	
  renderer = glade_cell_renderer_icon_new ();
  g_object_set (G_OBJECT (renderer), "icon-name", GTK_STOCK_EDIT, NULL);

  g_signal_connect (G_OBJECT (renderer), "activate",
		    G_CALLBACK (glade_signal_editor_user_data_activate),
		    self);
  gtk_cell_layout_pack_end (GTK_CELL_LAYOUT (cell_area), renderer, FALSE);
  gtk_cell_area_add_focus_sibling (cell_area,
				   renderer,
				   priv->renderer_userdata);
	
  priv->column_userdata = gtk_tree_view_column_new_with_area (cell_area);
  gtk_tree_view_column_set_title (priv->column_userdata, _("User data"));
  gtk_tree_view_column_set_cell_data_func (priv->column_userdata, priv->renderer_userdata,
					   glade_signal_editor_data_cell_data_func,
					   self, NULL);
  gtk_tree_view_column_set_cell_data_func (priv->column_userdata, renderer,
					   glade_signal_editor_data_cell_data_func,
					   self, NULL);

  gtk_tree_view_column_set_resizable (priv->column_userdata, TRUE);
  gtk_tree_view_column_set_expand (priv->column_userdata, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (self->priv->signal_tree), priv->column_userdata);

  /* Swap signal */
  renderer = gtk_cell_renderer_toggle_new ();
  g_signal_connect (renderer, "toggled", G_CALLBACK (on_swap_toggled), self);
  priv->column_swap = gtk_tree_view_column_new_with_attributes (_("Swap"),
								renderer,
								"active", GLADE_SIGNAL_COLUMN_SWAP,
								NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (self->priv->signal_tree), priv->column_swap);
  gtk_tree_view_column_set_cell_data_func (priv->column_swap, renderer,
					   glade_signal_editor_data_cell_data_func,
					   self,
					   NULL);
	
  /* After */
  cell_area = gtk_cell_area_box_new ();
  renderer = gtk_cell_renderer_toggle_new ();
  g_object_set (G_OBJECT (renderer), "xalign", 0.0, NULL);
  g_signal_connect (renderer, "toggled", G_CALLBACK (on_after_toggled), self);

  priv->column_after = gtk_tree_view_column_new_with_area (cell_area);
  gtk_tree_view_column_set_title (priv->column_after, _("After"));
  gtk_tree_view_append_column (GTK_TREE_VIEW (self->priv->signal_tree), priv->column_after);

  gtk_cell_area_box_pack_start  (GTK_CELL_AREA_BOX (cell_area),
				 renderer, FALSE, TRUE, FALSE);
  gtk_cell_area_attribute_connect (cell_area, renderer, "active", GLADE_SIGNAL_COLUMN_AFTER);
  gtk_tree_view_column_set_cell_data_func (priv->column_after, renderer,
					   glade_signal_editor_data_cell_data_func,
					   self, NULL);
	
  /* Devhelp */
  if (glade_util_have_devhelp ())
    {
      renderer = glade_cell_renderer_icon_new ();

      g_object_set (G_OBJECT (renderer), "activatable", TRUE, NULL);

      if (gtk_icon_theme_has_icon
	  (gtk_icon_theme_get_default (), GLADE_DEVHELP_ICON_NAME))
	g_object_set (G_OBJECT (renderer), "icon-name", GLADE_DEVHELP_ICON_NAME,
		      NULL);
      else
	g_object_set (G_OBJECT (renderer), "icon-name", GTK_STOCK_INFO, NULL);

      g_signal_connect (G_OBJECT (renderer), "activate",
			G_CALLBACK (glade_signal_editor_devhelp), self);

      gtk_cell_area_box_pack_start  (GTK_CELL_AREA_BOX (cell_area),
				     renderer, FALSE, TRUE, FALSE);

      gtk_tree_view_column_set_cell_data_func (priv->column_after, renderer,
					       glade_signal_editor_devhelp_cell_data_func,
					       self,
					       NULL);

    }
	
  /* Tooltips */
  gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (self->priv->signal_tree),
				    GLADE_SIGNAL_COLUMN_TOOLTIP);
  
  /* Create scrolled window */
  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll),
				       GTK_SHADOW_IN);
	
  gtk_container_add (GTK_CONTAINER (scroll), self->priv->signal_tree);
	
  gtk_box_pack_start (GTK_BOX (self), scroll, TRUE, TRUE, 0);

  /* Dnd */
  g_signal_connect_after (self->priv->signal_tree,
			  "drag-begin",
			  G_CALLBACK(glade_signal_editor_drag_begin),
			  self);

  /* Detail completion */
  priv->detail_store = gtk_list_store_new (1, G_TYPE_STRING);
  
  /* Handler completion */
  priv->handler_store = gtk_list_store_new (1, G_TYPE_STRING);

  /* Emit created signal */
  g_signal_emit_by_name (glade_app_get(), "signal-editor-created", self);
	
  gtk_widget_show_all (GTK_WIDGET(self));
}

static void
glade_signal_editor_class_init (GladeSignalEditorClass *klass)
{
  GObjectClass *object_class;

  glade_signal_editor_parent_class = g_type_class_peek_parent (klass);

  object_class = G_OBJECT_CLASS (klass);
  object_class->get_property = glade_signal_editor_get_property;
  object_class->set_property = glade_signal_editor_set_property;
  object_class->dispose = glade_signal_editor_dispose;

  klass->callback_suggestions = glade_signal_editor_callback_suggestions;
  klass->detail_suggestions = glade_signal_editor_detail_suggestions;
  
  g_type_class_add_private (klass, sizeof (GladeSignalEditorPrivate));

  /**
   * GladeSignalEditor::signal-activated:
   * @signal_editor: the object which received the signal
   *
   * Emitted when a item is activated in the GladeInspector.
   */
  glade_signal_editor_signals[SIGNAL_ACTIVATED] =
    g_signal_new ("signal-activated",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  GLADE_TYPE_SIGNAL /* Signal data formatted string */
                  );

  /**
   * GladeSignalEditor::callback-suggestions:
   * @editor: the object which received the signal
   * @signal: the #GladeSignal that needs callbacks suggestions
   * @suggestions: Return
   *
   * Emitted when the editor needs to show a list of callbacks suggestions to the user.
   * 
   * Returns wheter or not the event was handled.
   */
  glade_signal_editor_signals[CALLBACK_SUGGESTIONS] =
    g_signal_new ("callback-suggestions",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GladeSignalEditorClass, callback_suggestions),
                  _glade_strv_handled_accumulator,
                  NULL, _glade_marshal_BOXED__OBJECT,
                  G_TYPE_STRV, 1,
                  GLADE_TYPE_SIGNAL);

  /**
   * GladeSignalEditor::detail-suggestions:
   * @editor: the object which received the signal
   * @signal: the #GladeSignal that needs callbacks suggestions
   * @suggestions: Return
   *
   * Emitted when the editor needs to show a list of detail suggestions to the user.
   * 
   * Returns wheter or not the event was handled.
   */
  glade_signal_editor_signals[DETAIL_SUGGESTIONS] =
    g_signal_new ("detail-suggestions",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GladeSignalEditorClass, detail_suggestions),
                  _glade_strv_handled_accumulator,
                  NULL, _glade_marshal_BOXED__OBJECT,
                  G_TYPE_STRV, 1,
                  GLADE_TYPE_SIGNAL);   
  

  g_object_class_install_property (object_class,
                                   PROP_GLADE_WIDGET,
                                   g_param_spec_object ("glade-widget",
	                                                _("Glade Widget"),
	                                                _("The glade widget to edit signals"),
	                                                GTK_TYPE_TREE_MODEL,
	                                                G_PARAM_READWRITE));
}
