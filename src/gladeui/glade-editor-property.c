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
 *   Tristan Van Berkom <tvb@gnome.org>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/**
 * SECTION:glade-editor-property
 * @Short_Description: A generic widget to edit a #GladeProperty.
 *
 * The #GladeEditorProperty is a factory that will create the correct
 * control for the #GladePropertyClass it was created for and provides
 * a simple unified api to them.
 */

#include <stdio.h>
#include <string.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "glade.h"
#include "glade-widget.h"
#include "glade-editor-property.h"
#include "glade-property.h"
#include "glade-command.h"
#include "glade-project.h"
#include "glade-popup.h"
#include "glade-builtins.h"
#include "glade-marshallers.h"
#include "glade-displayable-values.h"
#include "glade-named-icon-chooser-dialog.h"

enum
{
  PROP_0,
  PROP_PROPERTY_CLASS,
  PROP_USE_COMMAND
};

enum
{
  CHANGED,
  COMMIT,
  LAST_SIGNAL
};

static GtkTableClass *table_class;
static GladeEditorPropertyClass *editor_property_class;

static guint glade_eprop_signals[LAST_SIGNAL] = { 0, };

#define GLADE_PROPERTY_TABLE_ROW_SPACING 2
#define FLAGS_COLUMN_SETTING             0
#define FLAGS_COLUMN_SYMBOL              1
#define FLAGS_COLUMN_VALUE               2

struct _GladeEditorPropertyPrivate
{
  GladePropertyClass *klass;          /* The property class this GladeEditorProperty was created for */
  GladeProperty      *property;       /* The currently loaded property */

  GtkWidget          *item_label;     /* The property name portion of the eprop */
  GtkWidget          *label;          /* The actual property name label */
  GtkWidget          *warning;        /* Icon to show warnings */
  GtkWidget          *input;          /* Input part of property (need to set sensitivity seperately)  */
  GtkWidget          *check;          /* Check button for optional properties. */

  gulong              tooltip_id;     /* signal connection id for tooltip changes        */
  gulong              sensitive_id;   /* signal connection id for sensitivity changes    */
  gulong              changed_id;     /* signal connection id for value changes          */
  gulong              enabled_id;     /* signal connection id for enable/disable changes */
  gulong              state_id;       /* signal connection id for state changes          */
	
  gboolean            loading;        /* True during glade_editor_property_load calls, this
				       * is used to avoid feedback from input widgets.
				       */
  guint               committing : 1; /* True while the editor property itself is applying
				       * the property with glade_editor_property_commit_no_callback ().
				       */
  guint               use_command : 1; /* Whether we should use the glade command interface
					* or skip directly to GladeProperty interface.
					* (used for query dialogs).
					*/
};

G_DEFINE_TYPE (GladeEditorProperty, glade_editor_property, GTK_TYPE_HBOX);

/*******************************************************************************
                               GladeEditorPropertyClass
 *******************************************************************************/

/* declare this forwardly for the finalize routine */
static void glade_editor_property_load_common (GladeEditorProperty *eprop,
                                               GladeProperty *property);

static void
glade_editor_property_commit_common (GladeEditorProperty *eprop,
                                     GValue *value)
{
  if (eprop->priv->use_command == FALSE)
    glade_property_set_value (eprop->priv->property, value);
  else
    glade_command_set_property_value (eprop->priv->property, value);

  /* If the value was denied by a verify function, we'll have to
   * reload the real value.
   */
  if (!glade_property_equals_value (eprop->priv->property, value))
    glade_editor_property_load (eprop, eprop->priv->property);
}

void
glade_editor_property_commit_no_callback (GladeEditorProperty *eprop,
                                          GValue *value)
{
  g_return_if_fail (GLADE_IS_EDITOR_PROPERTY (eprop));

  if (eprop->priv->committing)
    return;

  g_signal_handler_block (G_OBJECT (eprop->priv->property), eprop->priv->changed_id);
  eprop->priv->committing = TRUE;
  glade_editor_property_commit (eprop, value);
  eprop->priv->committing = FALSE;
  g_signal_handler_unblock (G_OBJECT (eprop->priv->property), eprop->priv->changed_id);
}

GtkWidget *
glade_editor_property_get_item_label  (GladeEditorProperty *eprop)
{
  g_return_val_if_fail (GLADE_IS_EDITOR_PROPERTY (eprop), NULL);

  return eprop->priv->item_label;
}

GladePropertyClass *
glade_editor_property_get_pclass (GladeEditorProperty *eprop)
{
  g_return_val_if_fail (GLADE_IS_EDITOR_PROPERTY (eprop), NULL);

  return eprop->priv->klass;
}

GladeProperty *
glade_editor_property_get_property (GladeEditorProperty *eprop)
{
  g_return_val_if_fail (GLADE_IS_EDITOR_PROPERTY (eprop), NULL);

  return eprop->priv->property;
}

gboolean
glade_editor_property_loading (GladeEditorProperty *eprop)
{
  g_return_val_if_fail (GLADE_IS_EDITOR_PROPERTY (eprop), FALSE);

  return eprop->priv->loading;
}

static void
glade_editor_property_tooltip_cb (GladeProperty *property,
                                  const gchar *tooltip,
                                  const gchar *insensitive,
                                  const gchar *support,
                                  GladeEditorProperty *eprop)
{
  const gchar *choice_tooltip;

  if (glade_property_get_sensitive (property))
    choice_tooltip = tooltip;
  else
    choice_tooltip = insensitive;

  gtk_widget_set_tooltip_text (eprop->priv->input, choice_tooltip);
  gtk_widget_set_tooltip_text (eprop->priv->label, choice_tooltip);
  gtk_widget_set_tooltip_text (eprop->priv->warning, support);
}

static void
glade_editor_property_sensitivity_cb (GladeProperty *property,
                                      GParamSpec *pspec,
                                      GladeEditorProperty *eprop)
{
  GladeEditorPropertyPrivate *priv = eprop->priv;
  gboolean property_enabled = glade_property_get_enabled (property);
  gboolean sensitive = glade_property_get_sensitive (priv->property);
  gboolean support_sensitive =
    (glade_property_get_state (priv->property) & GLADE_STATE_SUPPORT_DISABLED) == 0;

  gtk_widget_set_sensitive (priv->input,
                            sensitive && support_sensitive && property_enabled);

  if (priv->item_label)
    gtk_widget_set_sensitive (priv->item_label,
                              sensitive && support_sensitive && property_enabled);
  if (priv->check)
    gtk_widget_set_sensitive (priv->check, sensitive && support_sensitive);
}

static void
glade_editor_property_value_changed_cb (GladeProperty *property,
                                        GValue *old_value,
                                        GValue *value,
                                        GladeEditorProperty *eprop)
{
  g_assert (eprop->priv->property == property);
  glade_editor_property_load (eprop, eprop->priv->property);
}

static void
glade_editor_property_fix_label (GladeEditorProperty *eprop)
{
  gchar *text = NULL;

  if (!eprop->priv->property)
    return;

  /* refresh label */
  if ((glade_property_get_state (eprop->priv->property) & GLADE_STATE_CHANGED) != 0)
    text = g_strdup_printf ("<b>%s:</b>", glade_property_class_get_name (eprop->priv->klass));
  else
    text = g_strdup_printf ("%s:", glade_property_class_get_name (eprop->priv->klass));
  gtk_label_set_markup (GTK_LABEL (eprop->priv->label), text);
  g_free (text);

  /* refresh icon */
  if ((glade_property_get_state (eprop->priv->property) & GLADE_STATE_UNSUPPORTED) != 0)
    gtk_widget_show (eprop->priv->warning);
  else
    gtk_widget_hide (eprop->priv->warning);

  /* check sensitivity */
  glade_editor_property_sensitivity_cb (eprop->priv->property, NULL, eprop);
}

static void
glade_editor_property_state_cb (GladeProperty *property,
                                GParamSpec *pspec,
                                GladeEditorProperty *eprop)
{
  glade_editor_property_fix_label (eprop);
}

static void
glade_editor_property_enabled_cb (GladeProperty *property,
                                  GParamSpec *pspec,
                                  GladeEditorProperty *eprop)
{
  gboolean enabled;
  g_assert (eprop->priv->property == property);

  if (glade_property_class_optional (eprop->priv->klass))
    {
      enabled = glade_property_get_enabled (property);

      /* sensitive = enabled && support enabled && sensitive */
      if (enabled == FALSE)
        gtk_widget_set_sensitive (eprop->priv->input, FALSE);
      else if (glade_property_get_sensitive (property) ||
               (glade_property_get_state (property) & GLADE_STATE_SUPPORT_DISABLED) != 0)
        gtk_widget_set_sensitive (eprop->priv->input, TRUE);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (eprop->priv->check), enabled);
    }
}

static void
glade_editor_property_enabled_toggled_cb (GtkWidget *check,
                                          GladeEditorProperty *eprop)
{
  glade_property_set_enabled (eprop->priv->property,
                              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
                                                            (check)));
}

static gboolean
glade_editor_property_button_pressed (GtkWidget *widget,
                                      GdkEventButton *event,
                                      GladeEditorProperty *eprop)
{
  if (glade_popup_is_popup_event (event))
    {
      glade_popup_property_pop (eprop->priv->property, event);
      return TRUE;
    }
  return FALSE;
}


static GObject *
glade_editor_property_constructor (GType type,
                                   guint n_construct_properties,
                                   GObjectConstructParam *construct_properties)
{
  GObject *obj;
  GladeEditorProperty *eprop;
  GtkWidget *hbox;

  /* Invoke parent constructor (eprop->priv->klass should be resolved by this point) . */
  obj = G_OBJECT_CLASS (table_class)->constructor
      (type, n_construct_properties, construct_properties);

  eprop = GLADE_EDITOR_PROPERTY (obj);

  /* Create hbox and possibly check button
   */
  if (glade_property_class_optional (eprop->priv->klass))
    {
      eprop->priv->check = gtk_check_button_new ();
      gtk_widget_show (eprop->priv->check);
      gtk_box_pack_start (GTK_BOX (eprop), eprop->priv->check, FALSE, FALSE, 0);
      g_signal_connect (G_OBJECT (eprop->priv->check), "toggled",
                        G_CALLBACK (glade_editor_property_enabled_toggled_cb),
                        eprop);
    }

  /* Create the class specific input widget and add it */
  eprop->priv->input = GLADE_EDITOR_PROPERTY_GET_CLASS (eprop)->create_input (eprop);
  gtk_widget_show (eprop->priv->input);

  /* Create the warning icon */
  eprop->priv->warning = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING,
                                             GTK_ICON_SIZE_MENU);
  gtk_widget_set_no_show_all (eprop->priv->warning, TRUE);

  /* Create & setup label */
  eprop->priv->item_label = gtk_event_box_new ();
  eprop->priv->label = gtk_label_new (NULL);
  gtk_event_box_set_visible_window (GTK_EVENT_BOX (eprop->priv->item_label), FALSE);

  g_object_ref_sink (eprop->priv->item_label);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

  gtk_label_set_line_wrap (GTK_LABEL (eprop->priv->label), TRUE);
  gtk_label_set_width_chars (GTK_LABEL (eprop->priv->label), 10);
  gtk_label_set_line_wrap_mode (GTK_LABEL (eprop->priv->label), PANGO_WRAP_WORD_CHAR);

  gtk_misc_set_alignment (GTK_MISC (eprop->priv->label), 0.0, 0.5);

  gtk_box_pack_start (GTK_BOX (hbox), eprop->priv->label, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), eprop->priv->warning, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (eprop->priv->item_label), hbox);
  gtk_widget_show_all (eprop->priv->item_label);

  glade_editor_property_fix_label (eprop);

  g_signal_connect (G_OBJECT (eprop->priv->item_label), "button-press-event",
                    G_CALLBACK (glade_editor_property_button_pressed), eprop);
  g_signal_connect (G_OBJECT (eprop->priv->input), "button-press-event",
                    G_CALLBACK (glade_editor_property_button_pressed), eprop);

  if (gtk_widget_get_halign (eprop->priv->input) != GTK_ALIGN_FILL)
    gtk_box_pack_start (GTK_BOX (eprop), eprop->priv->input, FALSE, TRUE, 0);
  else
    gtk_box_pack_start (GTK_BOX (eprop), eprop->priv->input, TRUE, TRUE, 0);

  
  return obj;
}

static void
glade_editor_property_finalize (GObject *object)
{
  GladeEditorProperty *eprop = GLADE_EDITOR_PROPERTY (object);

  /* detatch from loaded property */
  glade_editor_property_load_common (eprop, NULL);

  G_OBJECT_CLASS (table_class)->finalize (object);
}

static void
glade_editor_property_dispose (GObject *object)
{
  GladeEditorProperty *eprop = GLADE_EDITOR_PROPERTY (object);

  if (eprop->priv->item_label)
    {
      g_object_unref (eprop->priv->item_label);
      eprop->priv->item_label = NULL;
    }

  G_OBJECT_CLASS (table_class)->dispose (object);
}

static void
glade_editor_property_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GladeEditorProperty *eprop = GLADE_EDITOR_PROPERTY (object);

  switch (prop_id)
    {
      case PROP_PROPERTY_CLASS:
        eprop->priv->klass = g_value_get_pointer (value);
        break;
      case PROP_USE_COMMAND:
        eprop->priv->use_command = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
glade_editor_property_real_get_property (GObject *object,
					 guint prop_id,
					 GValue *value,
                                         GParamSpec *pspec)
{
  GladeEditorProperty *eprop = GLADE_EDITOR_PROPERTY (object);

  switch (prop_id)
    {
      case PROP_PROPERTY_CLASS:
        g_value_set_pointer (value, eprop->priv->klass);
        break;
      case PROP_USE_COMMAND:
        g_value_set_boolean (value, eprop->priv->use_command);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
glade_eprop_property_finalized (GladeEditorProperty *eprop,
                                GladeProperty *where_property_was)
{
  eprop->priv->tooltip_id = 0;
  eprop->priv->sensitive_id = 0;
  eprop->priv->changed_id = 0;
  eprop->priv->enabled_id = 0;
  eprop->priv->state_id = 0;
  eprop->priv->property = NULL;

  glade_editor_property_load (eprop, NULL);
}

static void
glade_editor_property_load_common (GladeEditorProperty *eprop,
                                   GladeProperty *property)
{
  /* NOTE THIS CODE IS FINALIZE SAFE */

  /* disconnect anything from previously loaded property */
  if (eprop->priv->property != property && eprop->priv->property != NULL)
    {
      if (eprop->priv->tooltip_id > 0)
        g_signal_handler_disconnect (eprop->priv->property, eprop->priv->tooltip_id);
      if (eprop->priv->sensitive_id > 0)
        g_signal_handler_disconnect (eprop->priv->property, eprop->priv->sensitive_id);
      if (eprop->priv->changed_id > 0)
        g_signal_handler_disconnect (eprop->priv->property, eprop->priv->changed_id);
      if (eprop->priv->state_id > 0)
        g_signal_handler_disconnect (eprop->priv->property, eprop->priv->state_id);
      if (eprop->priv->enabled_id > 0)
        g_signal_handler_disconnect (eprop->priv->property, eprop->priv->enabled_id);

      eprop->priv->tooltip_id = 0;
      eprop->priv->sensitive_id = 0;
      eprop->priv->changed_id = 0;
      eprop->priv->enabled_id = 0;
      eprop->priv->state_id = 0;

      /* Unref it here */
      g_object_weak_unref (G_OBJECT (eprop->priv->property),
                           (GWeakNotify) glade_eprop_property_finalized, eprop);


      /* For a reason I cant quite tell yet, this is the only
       * safe way to nullify the property member of the eprop
       * without leeking signal connections to properties :-/
       */
      if (property == NULL)
        {
          eprop->priv->property = NULL;
        }
    }

  /* Connect new stuff, deal with tooltip
   */
  if (eprop->priv->property != property && property != NULL)
    {
      GladePropertyClass *pclass = glade_property_get_class (property);

      eprop->priv->property = property;

      eprop->priv->tooltip_id =
          g_signal_connect (G_OBJECT (eprop->priv->property),
                            "tooltip-changed",
                            G_CALLBACK (glade_editor_property_tooltip_cb),
                            eprop);
      eprop->priv->sensitive_id =
          g_signal_connect (G_OBJECT (eprop->priv->property),
                            "notify::sensitive",
                            G_CALLBACK (glade_editor_property_sensitivity_cb),
                            eprop);
      eprop->priv->changed_id =
          g_signal_connect (G_OBJECT (eprop->priv->property),
                            "value-changed",
                            G_CALLBACK (glade_editor_property_value_changed_cb),
                            eprop);
      eprop->priv->enabled_id =
          g_signal_connect (G_OBJECT (eprop->priv->property),
                            "notify::enabled",
                            G_CALLBACK (glade_editor_property_enabled_cb),
                            eprop);
      eprop->priv->state_id =
          g_signal_connect (G_OBJECT (eprop->priv->property),
                            "notify::state",
                            G_CALLBACK (glade_editor_property_state_cb), eprop);


      /* In query dialogs when the user hits cancel, 
       * these babies go away (so better stay protected).
       */
      g_object_weak_ref (G_OBJECT (eprop->priv->property),
                         (GWeakNotify) glade_eprop_property_finalized, eprop);

      /* Load initial tooltips
       */
      glade_editor_property_tooltip_cb
	(property, glade_property_class_get_tooltip (pclass),
	   glade_propert_get_insensitive_tooltip (property),
	   glade_property_get_support_warning (property), eprop);

      /* Load initial enabled state
       */
      glade_editor_property_enabled_cb (property, NULL, eprop);

      /* Load initial sensitive state.
       */
      glade_editor_property_sensitivity_cb (property, NULL, eprop);

      /* Load intial label state
       */
      glade_editor_property_state_cb (property, NULL, eprop);
    }
}

static void
glade_editor_property_init (GladeEditorProperty *eprop)
{
  eprop->priv =
    G_TYPE_INSTANCE_GET_PRIVATE ((eprop),
				 GLADE_TYPE_EDITOR_PROPERTY,
				 GladeEditorPropertyPrivate);

}

static void
glade_editor_property_class_init (GladeEditorPropertyClass *eprop_class)
{
  GObjectClass *object_class;
  g_return_if_fail (eprop_class != NULL);

  /* Both parent classes assigned here.
   */
  editor_property_class = eprop_class;
  table_class = g_type_class_peek_parent (eprop_class);
  object_class = G_OBJECT_CLASS (eprop_class);

  /* GObjectClass */
  object_class->constructor = glade_editor_property_constructor;
  object_class->finalize = glade_editor_property_finalize;
  object_class->dispose = glade_editor_property_dispose;
  object_class->get_property = glade_editor_property_real_get_property;
  object_class->set_property = glade_editor_property_set_property;

  /* Class methods */
  eprop_class->load = glade_editor_property_load_common;
  eprop_class->commit = glade_editor_property_commit_common;
  eprop_class->create_input = NULL;


  /**
   * GladeEditorProperty::value-changed:
   * @gladeeditorproperty: the #GladeEditorProperty which changed value
   * @arg1: the #GladeProperty that's value changed.
   *
   * Emitted when a contained property changes value
   */
  glade_eprop_signals[CHANGED] =
      g_signal_new ("value-changed",
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GladeEditorPropertyClass, changed),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__OBJECT,
                    G_TYPE_NONE, 1, GLADE_TYPE_PROPERTY);

  /**
   * GladeEditorProperty::commit:
   * @gladeeditorproperty: the #GladeEditorProperty which changed value
   * @arg1: the new #GValue to commit.
   *
   * Emitted when a property's value is committed, can be useful to serialize
   * commands before and after the property's commit command from custom editors.
   */
  glade_eprop_signals[COMMIT] =
      g_signal_new ("commit",
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GladeEditorPropertyClass, commit),
                    NULL, NULL,
                    _glade_marshal_VOID__POINTER,
                    G_TYPE_NONE, 1, G_TYPE_POINTER);

  /* Properties */
  g_object_class_install_property
      (object_class, PROP_PROPERTY_CLASS,
       g_param_spec_pointer
       ("property-class", _("Property Class"),
        _("The GladePropertyClass this GladeEditorProperty was created for"),
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property
      (object_class, PROP_USE_COMMAND,
       g_param_spec_boolean
       ("use-command", _("Use Command"),
        _("Whether we should use the command API for the undo/redo stack"),
        FALSE, G_PARAM_READWRITE));

  g_type_class_add_private (eprop_class, sizeof (GladeEditorPropertyPrivate));
}

/*******************************************************************************
                        GladeEditorPropertyNumericClass
 *******************************************************************************/
typedef struct
{
  GladeEditorProperty parent_instance;

  GtkWidget *spin;
} GladeEPropNumeric;

GLADE_MAKE_EPROP (GladeEPropNumeric, glade_eprop_numeric)
#define GLADE_EPROP_NUMERIC(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_NUMERIC, GladeEPropNumeric))
#define GLADE_EPROP_NUMERIC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_NUMERIC, GladeEPropNumericClass))
#define GLADE_IS_EPROP_NUMERIC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_NUMERIC))
#define GLADE_IS_EPROP_NUMERIC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_NUMERIC))
#define GLADE_EPROP_NUMERIC_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_NUMERIC, GladeEPropNumericClass))
     static void glade_eprop_numeric_finalize (GObject *object)
{
  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}

static void
glade_eprop_numeric_load (GladeEditorProperty *eprop, GladeProperty *property)
{
  gfloat val = 0.0F;
  GladeEPropNumeric *eprop_numeric = GLADE_EPROP_NUMERIC (eprop);
  GParamSpec *pspec;
  GValue *value;

  /* Chain up first */
  editor_property_class->load (eprop, property);

  if (property)
    {
      value = glade_property_inline_value (property);
      pspec = glade_property_class_get_pspec (eprop->priv->klass);

      if (G_IS_PARAM_SPEC_INT (pspec))
        val = (gfloat) g_value_get_int (value);
      else if (G_IS_PARAM_SPEC_UINT (pspec))
        val = (gfloat) g_value_get_uint (value);
      else if (G_IS_PARAM_SPEC_LONG (pspec))
        val = (gfloat) g_value_get_long (value);
      else if (G_IS_PARAM_SPEC_ULONG (pspec))
        val = (gfloat) g_value_get_ulong (value);
      else if (G_IS_PARAM_SPEC_INT64 (pspec))
        val = (gfloat) g_value_get_int64 (value);
      else if (G_IS_PARAM_SPEC_UINT64 (pspec))
        val = (gfloat) g_value_get_uint64 (value);
      else if (G_IS_PARAM_SPEC_DOUBLE (pspec))
        val = (gfloat) g_value_get_double (value);
      else if (G_IS_PARAM_SPEC_FLOAT (pspec))
        val = g_value_get_float (value);
      else
        g_warning ("Unsupported type %s\n",
                   g_type_name (G_PARAM_SPEC_TYPE (pspec)));
      gtk_spin_button_set_value (GTK_SPIN_BUTTON (eprop_numeric->spin), val);
    }
}


static void
glade_eprop_numeric_changed (GtkWidget *spin, GladeEditorProperty *eprop)
{
  GValue val = { 0, };
  GParamSpec *pspec;

  if (eprop->priv->loading)
    return;

  pspec = glade_property_class_get_pspec (eprop->priv->klass);
  g_value_init (&val, pspec->value_type);

  if (G_IS_PARAM_SPEC_INT (pspec))
    g_value_set_int (&val, gtk_spin_button_get_value_as_int
                     (GTK_SPIN_BUTTON (spin)));
  else if (G_IS_PARAM_SPEC_UINT (pspec))
    g_value_set_uint (&val, gtk_spin_button_get_value_as_int
                      (GTK_SPIN_BUTTON (spin)));
  else if (G_IS_PARAM_SPEC_LONG (pspec))
    g_value_set_long (&val, (glong) gtk_spin_button_get_value_as_int
                      (GTK_SPIN_BUTTON (spin)));
  else if (G_IS_PARAM_SPEC_ULONG (pspec))
    g_value_set_ulong (&val, (gulong) gtk_spin_button_get_value_as_int
                       (GTK_SPIN_BUTTON (spin)));
  else if (G_IS_PARAM_SPEC_INT64 (pspec))
    g_value_set_int64 (&val, (gint64) gtk_spin_button_get_value_as_int
                       (GTK_SPIN_BUTTON (spin)));
  else if (G_IS_PARAM_SPEC_UINT64 (pspec))
    g_value_set_uint64 (&val, (guint64) gtk_spin_button_get_value_as_int
                        (GTK_SPIN_BUTTON (spin)));
  else if (G_IS_PARAM_SPEC_FLOAT (pspec))
    g_value_set_float (&val, (gfloat) gtk_spin_button_get_value
                       (GTK_SPIN_BUTTON (spin)));
  else if (G_IS_PARAM_SPEC_DOUBLE (pspec))
    g_value_set_double (&val, gtk_spin_button_get_value
                        (GTK_SPIN_BUTTON (spin)));
  else
    g_warning ("Unsupported type %s\n",
               g_type_name (G_PARAM_SPEC_TYPE (pspec)));

  glade_editor_property_commit_no_callback (eprop, &val);
  g_value_unset (&val);
}

static GtkWidget *
glade_eprop_numeric_create_input (GladeEditorProperty *eprop)
{
  GladeEPropNumeric *eprop_numeric = GLADE_EPROP_NUMERIC (eprop);
  GtkAdjustment *adjustment;
  GParamSpec *pspec;

  pspec      = glade_property_class_get_pspec (eprop->priv->klass);
  adjustment = glade_property_class_make_adjustment (eprop->priv->klass);
  eprop_numeric->spin = 
    gtk_spin_button_new (adjustment, 4,
			 G_IS_PARAM_SPEC_FLOAT (pspec) ||
			 G_IS_PARAM_SPEC_DOUBLE (pspec) ? 2 : 0);
  gtk_widget_set_halign (eprop_numeric->spin, GTK_ALIGN_START);
  gtk_widget_set_valign (eprop_numeric->spin, GTK_ALIGN_CENTER);

  gtk_widget_show (eprop_numeric->spin);

  /* Limit the size of the spin if max allowed value is too big */
  if (gtk_adjustment_get_upper (adjustment) > 9999999999999999.0)
    gtk_entry_set_width_chars (GTK_ENTRY (eprop_numeric->spin), 16);

  g_signal_connect (G_OBJECT (eprop_numeric->spin), "value_changed",
                    G_CALLBACK (glade_eprop_numeric_changed), eprop);

  return eprop_numeric->spin;
}

/*******************************************************************************
                        GladeEditorPropertyEnumClass
 *******************************************************************************/
typedef struct
{
  GladeEditorProperty parent_instance;

  GtkWidget *combo_box;
} GladeEPropEnum;

GLADE_MAKE_EPROP (GladeEPropEnum, glade_eprop_enum)
#define GLADE_EPROP_ENUM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_ENUM, GladeEPropEnum))
#define GLADE_EPROP_ENUM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_ENUM, GladeEPropEnumClass))
#define GLADE_IS_EPROP_ENUM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_ENUM))
#define GLADE_IS_EPROP_ENUM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_ENUM))
#define GLADE_EPROP_ENUM_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_ENUM, GladeEPropEnumClass))
     static void glade_eprop_enum_finalize (GObject *object)
{
  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}

static void
glade_eprop_enum_load (GladeEditorProperty *eprop, GladeProperty *property)
{
  GladeEPropEnum *eprop_enum = GLADE_EPROP_ENUM (eprop);
  GParamSpec *pspec;
  GEnumClass *eclass;
  guint i;
  gint value;

  /* Chain up first */
  editor_property_class->load (eprop, property);

  if (property)
    {
      pspec  = glade_property_class_get_pspec (eprop->priv->klass);
      eclass = g_type_class_ref (pspec->value_type);
      value  = g_value_get_enum (glade_property_inline_value (property));

      for (i = 0; i < eclass->n_values; i++)
        if (eclass->values[i].value == value)
          break;

      gtk_combo_box_set_active (GTK_COMBO_BOX (eprop_enum->combo_box),
                                i < eclass->n_values ? i : 0);
      g_type_class_unref (eclass);
    }
}

static void
glade_eprop_enum_changed (GtkWidget *combo_box, GladeEditorProperty *eprop)
{
  gint ival;
  GValue val = { 0, };
  GParamSpec *pspec;
  GtkTreeModel *tree_model;
  GtkTreeIter iter;

  if (eprop->priv->loading)
    return;

  tree_model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
  gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), &iter);
  gtk_tree_model_get (tree_model, &iter, 1, &ival, -1);

  pspec    = glade_property_class_get_pspec (eprop->priv->klass);

  g_value_init (&val, pspec->value_type);
  g_value_set_enum (&val, ival);

  glade_editor_property_commit_no_callback (eprop, &val);
  g_value_unset (&val);
}

static GtkWidget *
glade_eprop_enum_create_input (GladeEditorProperty *eprop)
{
  GladeEPropEnum *eprop_enum = GLADE_EPROP_ENUM (eprop);
  GladePropertyClass *klass;
  GParamSpec *pspec;
  GEnumClass *eclass;
  GtkListStore *list_store;
  GtkTreeIter iter;
  GtkCellRenderer *cell_renderer;
  guint i;

  klass  = eprop->priv->klass;
  pspec  = glade_property_class_get_pspec (klass);
  eclass = g_type_class_ref (pspec->value_type);

  list_store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);

  gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store), &iter);

  for (i = 0; i < eclass->n_values; i++)
    {
      const gchar *value_name;

      if (glade_displayable_value_is_disabled (pspec->value_type,
                                               eclass->values[i].value_nick))
        continue;

      value_name = glade_get_displayable_value (pspec->value_type,
                                                eclass->values[i].value_nick);
      if (value_name == NULL)
        value_name = eclass->values[i].value_nick;

      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter, 0, value_name, 1,
                          eclass->values[i].value, -1);
    }

  eprop_enum->combo_box = gtk_combo_box_new_with_model (GTK_TREE_MODEL (list_store));
  gtk_widget_set_halign (eprop_enum->combo_box, GTK_ALIGN_START);
  gtk_widget_set_valign (eprop_enum->combo_box, GTK_ALIGN_CENTER);
  
  cell_renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (eprop_enum->combo_box),
                              cell_renderer, TRUE);
  gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (eprop_enum->combo_box),
                                 cell_renderer, "text", 0);

  g_signal_connect (G_OBJECT (eprop_enum->combo_box), "changed",
                    G_CALLBACK (glade_eprop_enum_changed), eprop);

  gtk_widget_show_all (eprop_enum->combo_box);

  g_type_class_unref (eclass);

  return eprop_enum->combo_box;
}

/*******************************************************************************
                        GladeEditorPropertyFlagsClass
 *******************************************************************************/
typedef struct
{
  GladeEditorProperty parent_instance;

  GtkTreeModel *model;
  GtkWidget *entry;
} GladeEPropFlags;

GLADE_MAKE_EPROP (GladeEPropFlags, glade_eprop_flags)
#define GLADE_EPROP_FLAGS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_FLAGS, GladeEPropFlags))
#define GLADE_EPROP_FLAGS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_FLAGS, GladeEPropFlagsClass))
#define GLADE_IS_EPROP_FLAGS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_FLAGS))
#define GLADE_IS_EPROP_FLAGS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_FLAGS))
#define GLADE_EPROP_FLAGS_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_FLAGS, GladeEPropFlagsClass))
     static void glade_eprop_flags_finalize (GObject *object)
{
  GladeEPropFlags *eprop_flags = GLADE_EPROP_FLAGS (object);

  g_object_unref (G_OBJECT (eprop_flags->model));

  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}

static void
glade_eprop_flags_load (GladeEditorProperty *eprop, GladeProperty *property)
{
  GladeEPropFlags *eprop_flags = GLADE_EPROP_FLAGS (eprop);
  GFlagsClass *klass;
  GParamSpec  *pspec;
  guint flag_num, value;
  GString *string = g_string_new (NULL);

  /* Chain up first */
  editor_property_class->load (eprop, property);

  gtk_list_store_clear (GTK_LIST_STORE (eprop_flags->model));

  if (property)
    {
      /* Populate the model with the flags. */
      klass = g_type_class_ref (G_VALUE_TYPE (glade_property_inline_value (property)));
      value = g_value_get_flags (glade_property_inline_value (property));
      pspec = glade_property_class_get_pspec (eprop->priv->klass);

      /* Step through each of the flags in the class. */
      for (flag_num = 0; flag_num < klass->n_values; flag_num++)
        {
          GtkTreeIter iter;
          guint mask;
          gboolean setting;
          const gchar *value_name;

          if (glade_displayable_value_is_disabled (pspec->value_type,
                                                   klass->values[flag_num].value_nick))
            continue;
          
          mask = klass->values[flag_num].value;
          setting = ((value & mask) == mask) ? TRUE : FALSE;

          value_name = glade_get_displayable_value
              (pspec->value_type, klass->values[flag_num].value_nick);

          if (value_name == NULL)
            value_name = klass->values[flag_num].value_name;

          /* Setup string for property label */
          if (setting)
            {
              if (string->len > 0)
                g_string_append (string, " | ");
              g_string_append (string, value_name);
            }

          /* Add a row to represent the flag. */
          gtk_list_store_append (GTK_LIST_STORE (eprop_flags->model), &iter);
          gtk_list_store_set (GTK_LIST_STORE (eprop_flags->model), &iter,
                              FLAGS_COLUMN_SETTING, setting,
                              FLAGS_COLUMN_SYMBOL, value_name,
                              FLAGS_COLUMN_VALUE, mask, -1);

        }
      g_type_class_unref (klass);
    }

  gtk_entry_set_text (GTK_ENTRY (eprop_flags->entry), string->str);

  g_string_free (string, TRUE);
}


static void
flag_toggled_direct (GtkCellRendererToggle *cell,
                     gchar *path_string,
                     GladeEditorProperty *eprop)
{
  GtkTreeIter iter;
  guint new_value = 0;
  gboolean selected;
  GValue *gvalue;
  gboolean valid;

  GladeEPropFlags *eprop_flags = GLADE_EPROP_FLAGS (eprop);

  if (!eprop->priv->property)
    return;

  gvalue = glade_property_inline_value (eprop->priv->property);

  gtk_tree_model_get_iter_from_string (eprop_flags->model, &iter, path_string);

  gtk_tree_model_get (eprop_flags->model, &iter,
                      FLAGS_COLUMN_SETTING, &selected, -1);

  selected = selected ? FALSE : TRUE;

  gtk_list_store_set (GTK_LIST_STORE (eprop_flags->model), &iter,
                      FLAGS_COLUMN_SETTING, selected, -1);

  valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (eprop_flags->model), &iter);

  /* Step through each of the flags in the class, checking if
     the corresponding toggle in the dialog is selected, If it
     is, OR the flags' mask with the new value. */
  while (valid)
    {
      gboolean setting;
      guint value;

      gtk_tree_model_get (GTK_TREE_MODEL (eprop_flags->model), &iter,
                          FLAGS_COLUMN_SETTING, &setting,
                          FLAGS_COLUMN_VALUE, &value, -1);

      if (setting) new_value |= value;

      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (eprop_flags->model), &iter);
    }

  /* If the new_value is different from the old value, we need
     to update the property. */
  if (new_value != g_value_get_flags (gvalue))
    {
      GValue val = { 0, };

      g_value_init (&val, G_VALUE_TYPE (gvalue));
      g_value_set_flags (&val, new_value);

      glade_editor_property_commit_no_callback (eprop, &val);
      g_value_unset (&val);
    }
}

static GtkWidget *
glade_eprop_flags_create_treeview (GladeEditorProperty *eprop)
{
  GtkWidget *scrolled_window;
  GtkWidget *tree_view;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GladeEPropFlags *eprop_flags = GLADE_EPROP_FLAGS (eprop);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                       GTK_SHADOW_IN);
  gtk_widget_show (scrolled_window);



  tree_view =
      gtk_tree_view_new_with_model (GTK_TREE_MODEL (eprop_flags->model));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree_view), FALSE);
  gtk_widget_show (tree_view);
  gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

  column = gtk_tree_view_column_new ();

  renderer = gtk_cell_renderer_toggle_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "active", FLAGS_COLUMN_SETTING, NULL);

  g_signal_connect (renderer, "toggled",
                    G_CALLBACK (flag_toggled_direct), eprop);


  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "text", FLAGS_COLUMN_SYMBOL, NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);



  return scrolled_window;
}

static void
glade_eprop_flags_show_dialog (GladeEditorProperty *eprop)
{
  GtkWidget *window = gtk_widget_get_toplevel (GTK_WIDGET (eprop));
  GtkWidget *dialog;
  GtkWidget *view;
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *content_area;
  GtkWidget *action_area;

  dialog = gtk_dialog_new_with_buttons (_("Select Fields"),
                                        GTK_WINDOW (window),
                                        GTK_DIALOG_MODAL,
                                        GTK_STOCK_CLOSE,
                                        GTK_RESPONSE_CLOSE,
                                        NULL);

  gtk_window_set_default_size (GTK_WINDOW (dialog), 300, 400);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

  /* HIG spacings */
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_set_spacing (GTK_BOX (content_area), 2);      /* 2 * 5 + 2 = 12 */
  action_area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
  gtk_container_set_border_width (GTK_CONTAINER (action_area), 5);
  gtk_box_set_spacing (GTK_BOX (action_area), 6);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);

  view = glade_eprop_flags_create_treeview (eprop);

  label = gtk_label_new_with_mnemonic (_("_Select individual fields:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label),
                                 gtk_bin_get_child (GTK_BIN (view)));

  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), view, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (content_area), vbox, TRUE, TRUE, 0);

  gtk_widget_show (label);
  gtk_widget_show (view);
  gtk_widget_show (vbox);

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}


static GtkWidget *
glade_eprop_flags_create_input (GladeEditorProperty *eprop)
{
  GladeEPropFlags *eprop_flags = GLADE_EPROP_FLAGS (eprop);

  if (!eprop_flags->model)
    eprop_flags->model = GTK_TREE_MODEL (gtk_list_store_new (3, G_TYPE_BOOLEAN,
                                                             G_TYPE_STRING,
                                                             G_TYPE_UINT));

  eprop_flags->entry = gtk_entry_new ();
  gtk_editable_set_editable (GTK_EDITABLE (eprop_flags->entry), FALSE);
  gtk_entry_set_icon_from_stock (GTK_ENTRY (eprop_flags->entry), 
                                 GTK_ENTRY_ICON_SECONDARY,
                                 GTK_STOCK_EDIT);

  g_signal_connect_swapped (eprop_flags->entry, "icon-release",
                            G_CALLBACK (glade_eprop_flags_show_dialog),
                            eprop);

  return eprop_flags->entry;
}

/*******************************************************************************
                        GladeEditorPropertyColorClass
 *******************************************************************************/
typedef struct
{
  GladeEditorProperty parent_instance;

  GtkWidget *cbutton;
  GtkWidget *entry;
} GladeEPropColor;

GLADE_MAKE_EPROP (GladeEPropColor, glade_eprop_color)
#define GLADE_EPROP_COLOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_COLOR, GladeEPropColor))
#define GLADE_EPROP_COLOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_COLOR, GladeEPropColorClass))
#define GLADE_IS_EPROP_COLOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_COLOR))
#define GLADE_IS_EPROP_COLOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_COLOR))
#define GLADE_EPROP_COLOR_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_COLOR, GladeEPropColorClass))
static void glade_eprop_color_finalize (GObject *object)
{
  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}

static void
glade_eprop_color_load (GladeEditorProperty *eprop, GladeProperty *property)
{
  GladeEPropColor *eprop_color = GLADE_EPROP_COLOR (eprop);
  GParamSpec *pspec;
  GdkColor *color;
  GdkRGBA *rgba;
  gchar *text;

  /* Chain up first */
  editor_property_class->load (eprop, property);

  pspec = glade_property_class_get_pspec (eprop->priv->klass);

  if (property)
    {
      if ((text = glade_property_make_string (property)) != NULL)
        {
          gtk_entry_set_text (GTK_ENTRY (eprop_color->entry), text);
          g_free (text);
        }
      else
        gtk_entry_set_text (GTK_ENTRY (eprop_color->entry), "");

      if (pspec->value_type == GDK_TYPE_COLOR)
	{
	  if ((color = g_value_get_boxed (glade_property_inline_value (property))) != NULL)
	    gtk_color_button_set_color (GTK_COLOR_BUTTON (eprop_color->cbutton), color);
	  else
	    {
	      GdkColor black = { 0, };

	      /* Manually fill it with black for an NULL value.
	       */
	      if (gdk_color_parse ("Black", &black))
		gtk_color_button_set_color (GTK_COLOR_BUTTON (eprop_color->cbutton), &black);
	    }
	}
      else if (pspec->value_type == GDK_TYPE_RGBA)
	{
	  if ((rgba = g_value_get_boxed (glade_property_inline_value (property))) != NULL)
	    gtk_color_button_set_rgba (GTK_COLOR_BUTTON (eprop_color->cbutton), rgba);
	  else
	    {
	      GdkRGBA black = { 0, };

	      /* Manually fill it with black for an NULL value.
	       */
	      if (gdk_rgba_parse (&black, "Black"))
		gtk_color_button_set_rgba (GTK_COLOR_BUTTON (eprop_color->cbutton), &black);
	    }
	}
    }
}

static void
glade_eprop_color_changed (GtkWidget *button, GladeEditorProperty *eprop)
{
  GdkColor color = { 0, };
  GdkRGBA rgba = { 0, };
  GValue value = { 0, };
  GParamSpec *pspec;

  if (eprop->priv->loading)
    return;

  pspec = glade_property_class_get_pspec (eprop->priv->klass);
  g_value_init (&value, pspec->value_type);

  if (pspec->value_type == GDK_TYPE_COLOR)
    {
      gtk_color_button_get_color (GTK_COLOR_BUTTON (button), &color);

      g_value_set_boxed (&value, &color);
    }
  else if (pspec->value_type == GDK_TYPE_RGBA)
    {
      gtk_color_button_get_rgba (GTK_COLOR_BUTTON (button), &rgba);

      g_value_set_boxed (&value, &rgba);
    }

  glade_editor_property_commit_no_callback (eprop, &value);
  g_value_unset (&value);
}

static GtkWidget *
glade_eprop_color_create_input (GladeEditorProperty *eprop)
{
  GladeEPropColor *eprop_color = GLADE_EPROP_COLOR (eprop);
  GtkWidget *hbox;
  GParamSpec *pspec;

  pspec  = glade_property_class_get_pspec (eprop->priv->klass);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign (hbox, GTK_ALIGN_START);
  gtk_widget_set_valign (hbox, GTK_ALIGN_CENTER);
  
  eprop_color->entry = gtk_entry_new ();
  gtk_editable_set_editable (GTK_EDITABLE (eprop_color->entry), FALSE);
  gtk_widget_show (eprop_color->entry);
  gtk_box_pack_start (GTK_BOX (hbox), eprop_color->entry, TRUE, TRUE, 0);

  eprop_color->cbutton = gtk_color_button_new ();
  gtk_widget_show (eprop_color->cbutton);
  gtk_box_pack_start (GTK_BOX (hbox), eprop_color->cbutton, FALSE, FALSE, 0);

  if (pspec->value_type == GDK_TYPE_RGBA)
    gtk_color_button_set_use_alpha (GTK_COLOR_BUTTON (eprop_color->cbutton), TRUE);

  g_signal_connect (G_OBJECT (eprop_color->cbutton), "color-set",
                    G_CALLBACK (glade_eprop_color_changed), eprop);

  return hbox;
}

/*******************************************************************************
                        GladeEditorPropertyNamedIconClass
 *******************************************************************************/
typedef struct
{
  GladeEditorProperty parent_instance;

  GtkWidget *entry;
  gchar *current_context;
} GladeEPropNamedIcon;

GLADE_MAKE_EPROP (GladeEPropNamedIcon, glade_eprop_named_icon)
#define GLADE_EPROP_NAMED_ICON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_NAMED_ICON, GladeEPropNamedIcon))
#define GLADE_EPROP_NAMED_ICON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_NAMED_ICON, GladeEPropNamedIconClass))
#define GLADE_IS_EPROP_NAMED_ICON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_NAMED_ICON))
#define GLADE_IS_EPROP_NAMED_ICON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_NAMED_ICON))
#define GLADE_EPROP_NAMED_ICON_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_NAMED_ICON, GladeEPropNamedIconClass))
     static void glade_eprop_named_icon_finalize (GObject *object)
{
  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}

static void
glade_eprop_named_icon_load (GladeEditorProperty *eprop,
                             GladeProperty *property)
{
  GladeEPropNamedIcon *eprop_named_icon = GLADE_EPROP_NAMED_ICON (eprop);
  GtkEntry *entry;
  gchar *text;

  /* Chain up first */
  editor_property_class->load (eprop, property);

  if (property == NULL)
    return;

  entry = GTK_ENTRY (eprop_named_icon->entry);
  text = glade_property_make_string (property);

  gtk_entry_set_text (entry, text ? text : "");

  g_free (text);
}

static void
glade_eprop_named_icon_changed_common (GladeEditorProperty *eprop,
                                       const gchar *text,
                                       gboolean use_command)
{
  GValue *val;
  gchar *prop_text;

  val = g_new0 (GValue, 1);

  g_value_init (val, G_TYPE_STRING);

  glade_property_get (eprop->priv->property, &prop_text);

  /* Here we try not to modify the project state by not 
   * modifying a null value for an unchanged property.
   */
  if (prop_text == NULL && text && text[0] == '\0')
    g_value_set_string (val, NULL);
  else if (text == NULL && prop_text && prop_text == '\0')
    g_value_set_string (val, "");
  else
    g_value_set_string (val, text);

  glade_editor_property_commit (eprop, val);
  g_value_unset (val);
  g_free (val);
}

static void
glade_eprop_named_icon_changed (GtkWidget *entry, GladeEditorProperty *eprop)
{
  gchar *text;

  if (eprop->priv->loading)
    return;

  text = gtk_editable_get_chars (GTK_EDITABLE (entry), 0, -1);
  glade_eprop_named_icon_changed_common (eprop, text, eprop->priv->use_command);

  g_free (text);
}

static gboolean
glade_eprop_named_icon_focus_out (GtkWidget *entry,
                                  GdkEventFocus *event,
                                  GladeEditorProperty *eprop)
{
  glade_eprop_named_icon_changed (entry, eprop);
  return FALSE;
}

static void
glade_eprop_named_icon_activate (GtkEntry *entry, GladeEPropNamedIcon *eprop)
{
  glade_eprop_named_icon_changed (GTK_WIDGET (entry),
                                  GLADE_EDITOR_PROPERTY (eprop));
}

static void
chooser_response (GladeNamedIconChooserDialog *dialog,
                  gint response_id,
                  GladeEPropNamedIcon *eprop)
{
  gchar *icon_name;

  switch (response_id)
    {

      case GTK_RESPONSE_OK:

        g_free (eprop->current_context);
        eprop->current_context =
            glade_named_icon_chooser_dialog_get_context (dialog);
        icon_name = glade_named_icon_chooser_dialog_get_icon_name (dialog);

        gtk_entry_set_text (GTK_ENTRY (eprop->entry), icon_name);
        gtk_widget_destroy (GTK_WIDGET (dialog));

        g_free (icon_name);

        glade_eprop_named_icon_changed (eprop->entry,
                                        GLADE_EDITOR_PROPERTY (eprop));

        break;

      case GTK_RESPONSE_CANCEL:

        gtk_widget_destroy (GTK_WIDGET (dialog));
        break;

      case GTK_RESPONSE_HELP:

        break;

      case GTK_RESPONSE_DELETE_EVENT:
        gtk_widget_destroy (GTK_WIDGET (dialog));
    }
}

static void
glade_eprop_named_icon_show_chooser_dialog (GladeEditorProperty *eprop)
{
  GtkWidget *dialog;

  dialog = glade_named_icon_chooser_dialog_new (_("Select Named Icon"),
                                                GTK_WINDOW
                                                (gtk_widget_get_toplevel
                                                 (GTK_WIDGET (eprop))),
                                                GTK_STOCK_CANCEL,
                                                GTK_RESPONSE_CANCEL,
                                                GTK_STOCK_OK, GTK_RESPONSE_OK,
                                                NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  glade_named_icon_chooser_dialog_set_context (GLADE_NAMED_ICON_CHOOSER_DIALOG
                                               (dialog),
                                               GLADE_EPROP_NAMED_ICON (eprop)->
                                               current_context);

  glade_named_icon_chooser_dialog_set_icon_name (GLADE_NAMED_ICON_CHOOSER_DIALOG
                                                 (dialog),
                                                 gtk_entry_get_text (GTK_ENTRY
                                                                     (GLADE_EPROP_NAMED_ICON
                                                                      (eprop)->
                                                                      entry)));


  g_signal_connect (dialog, "response", G_CALLBACK (chooser_response), eprop);

  gtk_widget_show (dialog);

}

static GtkWidget *
glade_eprop_named_icon_create_input (GladeEditorProperty *eprop)
{
  GladeEPropNamedIcon *eprop_named_icon = GLADE_EPROP_NAMED_ICON (eprop);
  
  eprop_named_icon->entry = gtk_entry_new ();
  gtk_widget_set_valign (eprop_named_icon->entry, GTK_ALIGN_CENTER);
  gtk_entry_set_icon_from_stock (GTK_ENTRY (eprop_named_icon->entry), 
                                 GTK_ENTRY_ICON_SECONDARY,
                                 GTK_STOCK_EDIT);

  eprop_named_icon->current_context = NULL;

  g_signal_connect (G_OBJECT (eprop_named_icon->entry), "activate",
                    G_CALLBACK (glade_eprop_named_icon_activate), eprop);

  g_signal_connect (G_OBJECT (eprop_named_icon->entry), "focus-out-event",
                    G_CALLBACK (glade_eprop_named_icon_focus_out), eprop);

  g_signal_connect_swapped (eprop_named_icon->entry, "icon-release",
                            G_CALLBACK (glade_eprop_named_icon_show_chooser_dialog),
                            eprop);

  return eprop_named_icon->entry;
}



/*******************************************************************************
                        GladeEditorPropertyTextClass
 *******************************************************************************/
typedef struct
{
  GladeEditorProperty parent_instance;

  GtkWidget *text_entry;
  GtkTreeModel *store;
} GladeEPropText;

GLADE_MAKE_EPROP (GladeEPropText, glade_eprop_text)
#define GLADE_EPROP_TEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_TEXT, GladeEPropText))
#define GLADE_EPROP_TEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_TEXT, GladeEPropTextClass))
#define GLADE_IS_EPROP_TEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_TEXT))
#define GLADE_IS_EPROP_TEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_TEXT))
#define GLADE_EPROP_TEXT_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_TEXT, GladeEPropTextClass))

static void
glade_eprop_text_finalize (GObject *object)
{
  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}

static gchar *
text_buffer_get_text (GtkTextBuffer *buffer)
{
  GtkTextIter start, end;
  gchar *retval;
  
  gtk_text_buffer_get_bounds (buffer, &start, &end);
  retval = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

  if (retval && retval[0] == '\0')
    {
      g_free (retval);
      return NULL;
    }

  return retval;
}

static void
glade_eprop_text_load (GladeEditorProperty *eprop, GladeProperty *property)
{
  GladeEPropText *eprop_text = GLADE_EPROP_TEXT (eprop);
  GParamSpec *pspec;

  /* Chain up first */
  editor_property_class->load (eprop, property);

  if (property == NULL)
    return;

  pspec = glade_property_class_get_pspec (eprop->priv->klass);

  if (GTK_IS_COMBO_BOX (eprop_text->text_entry))
    {
      if (gtk_combo_box_get_has_entry (GTK_COMBO_BOX (eprop_text->text_entry)))
        {
          GtkWidget *entry = gtk_bin_get_child (GTK_BIN (eprop_text->text_entry));
          gchar *text = glade_property_make_string (property);

          gtk_entry_set_text (GTK_ENTRY (entry), text ? text : "");
          g_free (text);
        }
      else
        {
          gchar *text = glade_property_make_string (property);
          gint value = text ?
              glade_utils_enum_value_from_string (GLADE_TYPE_STOCK, text) : 0;

          /* Set active iter... */
          gtk_combo_box_set_active (GTK_COMBO_BOX (eprop_text->text_entry),
                                    value);
          g_free (text);
        }
    }
  else if (GTK_IS_ENTRY (eprop_text->text_entry))
    {
      GtkEntry *entry = GTK_ENTRY (eprop_text->text_entry);
      gchar *text = glade_property_make_string (property);

      gtk_entry_set_text (entry, text ? text : "");
      g_free (text);
    }
  else if (GTK_IS_TEXT_VIEW (eprop_text->text_entry))
    {
      GtkTextBuffer *buffer;

      buffer =
          gtk_text_view_get_buffer (GTK_TEXT_VIEW (eprop_text->text_entry));

      if (pspec->value_type == G_TYPE_STRV ||
          pspec->value_type == G_TYPE_VALUE_ARRAY)
        {
	  GladePropertyClass *pclass = glade_property_get_class (property);
          gchar *text = glade_widget_adaptor_string_from_value
	    (glade_property_class_get_adaptor (pclass),
	     pclass, glade_property_inline_value (property));
          gchar *old_text = text_buffer_get_text (buffer);

          /* Only update it if necessary, see notes bellow */
          if (g_strcmp0 (text, old_text))
            gtk_text_buffer_set_text (buffer, text ? text : "", -1);

          g_free (text);
        }
      else
        {
          gchar *text = glade_property_make_string (property);
          gchar *old_text = text_buffer_get_text (buffer);

          /* NOTE: GtkTextBuffer does not like to be updated from a "changed"
           * signal callback. It prints a iterator warning and moves the cursor
           * to the end.
           */
          if (g_strcmp0 (text, old_text))
            gtk_text_buffer_set_text (buffer, text ? text : "", -1);

          g_free (old_text);
          g_free (text);
        }
    }
  else
    {
      g_warning ("BUG! Invalid Text Widget type.");
    }
}

static void
glade_eprop_text_changed_common (GladeEditorProperty *eprop,
                                 const gchar *text,
                                 gboolean use_command)
{
  GValue *val;
  GParamSpec *pspec;
  gchar *prop_text;

  pspec = glade_property_class_get_pspec (eprop->priv->klass);

  if (pspec->value_type == G_TYPE_STRV ||
      pspec->value_type == G_TYPE_VALUE_ARRAY ||
      pspec->value_type == GDK_TYPE_PIXBUF)
    {
      GladeWidget *gwidget = glade_property_get_widget (eprop->priv->property);

      val = glade_property_class_make_gvalue_from_string (eprop->priv->klass, 
							  text, 
							  glade_widget_get_project (gwidget));
    }
  else
    {
      val = g_new0 (GValue, 1);

      g_value_init (val, G_TYPE_STRING);

      glade_property_get (eprop->priv->property, &prop_text);

      /* Here we try not to modify the project state by not 
       * modifying a null value for an unchanged property.
       */
      if (prop_text == NULL && text && text[0] == '\0')
        g_value_set_string (val, NULL);
      else if (text == NULL && prop_text && prop_text == '\0')
        g_value_set_string (val, "");
      else
        g_value_set_string (val, text);
    }

  glade_editor_property_commit_no_callback (eprop, val);
  g_value_unset (val);
  g_free (val);
}

static void
glade_eprop_text_changed (GtkWidget *entry, GladeEditorProperty *eprop)
{
  gchar *text;

  if (eprop->priv->loading)
    return;

  text = gtk_editable_get_chars (GTK_EDITABLE (entry), 0, -1);
  glade_eprop_text_changed_common (eprop, text, eprop->priv->use_command);

  g_free (text);
}

static void
glade_eprop_text_buffer_changed (GtkTextBuffer *buffer,
                                 GladeEditorProperty *eprop)
{
  gchar *text;

  if (eprop->priv->loading)
    return;

  text = text_buffer_get_text (buffer);
  glade_eprop_text_changed_common (eprop, text, eprop->priv->use_command);
  g_free (text);
}

/**
 * glade_editor_property_show_i18n_dialog:
 * @parent: The parent widget for the dialog.
 * @text: A read/write pointer to the text property
 * @context: A read/write pointer to the translation context
 * @comment: A read/write pointer to the translator comment
 * @translatable: A read/write pointer to the translatable setting]
 *
 * Runs a dialog and updates the provided values.
 *
 * Returns: %TRUE if OK was selected.
 */
gboolean
glade_editor_property_show_i18n_dialog (GtkWidget *parent,
                                        gchar **text,
                                        gchar **context,
                                        gchar **comment,
                                        gboolean *translatable)
{
  GtkWidget *dialog;
  GtkWidget *vbox, *hbox;
  GtkWidget *label;
  GtkWidget *sw;
  GtkWidget *alignment;
  GtkWidget *text_view, *comment_view, *context_view;
  GtkTextBuffer *text_buffer, *comment_buffer, *context_buffer = NULL;
  GtkWidget *translatable_button;
  GtkWidget *content_area, *action_area;
  gint res;

  g_return_val_if_fail (text && context && comment && translatable, FALSE);

  dialog = gtk_dialog_new_with_buttons (_("Edit Text"),
                                        parent ?
                                        GTK_WINDOW (gtk_widget_get_toplevel
                                                    (parent)) : NULL,
                                        GTK_DIALOG_MODAL, GTK_STOCK_CANCEL,
                                        GTK_RESPONSE_CANCEL, GTK_STOCK_OK,
                                        GTK_RESPONSE_OK, NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_OK,
                                           GTK_RESPONSE_CANCEL, -1);

  /* HIG spacings */
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_set_spacing (GTK_BOX (content_area), 2);      /* 2 * 5 + 2 = 12 */
  action_area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
  gtk_container_set_border_width (GTK_CONTAINER (action_area), 5);
  gtk_box_set_spacing (GTK_BOX (action_area), 6);


  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
  gtk_widget_show (vbox);

  gtk_box_pack_start (GTK_BOX (content_area), vbox, TRUE, TRUE, 0);

  /* Text */
  label = gtk_label_new_with_mnemonic (_("_Text:"));
  gtk_widget_show (label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_show (sw);
  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
  gtk_widget_set_size_request (sw, 400, 200);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);

  text_view = gtk_text_view_new ();
  gtk_widget_show (text_view);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), text_view);

  gtk_container_add (GTK_CONTAINER (sw), text_view);

  text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));

  if (*text)
    {
      gtk_text_buffer_set_text (text_buffer, *text, -1);
    }

  /* Translatable and context prefix. */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_show (hbox);

  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  /* Translatable */
  translatable_button = gtk_check_button_new_with_mnemonic (_("T_ranslatable"));
  gtk_widget_show (translatable_button);
  gtk_box_pack_start (GTK_BOX (hbox), translatable_button, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (translatable_button),
                                *translatable);
  gtk_widget_set_tooltip_text (translatable_button,
                               _("Whether this property is translatable"));


  /* Context. */
  alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 12, 0, 0, 0);
  gtk_widget_show (alignment);

  label = gtk_label_new_with_mnemonic (_("Conte_xt for translation:"));
  gtk_widget_show (label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_container_add (GTK_CONTAINER (alignment), label);
  gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text (alignment,
                               _("For short and ambiguous strings: type a word here to differentiate "
				 "the meaning of this string from the meaning of other occurrences of "
				 "the same string"));

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_show (sw);
  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);

  context_view = gtk_text_view_new ();
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (context_view), GTK_WRAP_WORD);
  gtk_widget_show (context_view);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), context_view);

  gtk_container_add (GTK_CONTAINER (sw), context_view);

  context_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (context_view));

  if (*context)
    {
      gtk_text_buffer_set_text (context_buffer, *context, -1);
    }

  /* Comments. */
  alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 12, 0, 0, 0);
  gtk_widget_show (alignment);

  label = gtk_label_new_with_mnemonic (_("Co_mments for translators:"));
  gtk_widget_show (label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_container_add (GTK_CONTAINER (alignment), label);
  gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_show (sw);
  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);

  comment_view = gtk_text_view_new ();
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (comment_view), GTK_WRAP_WORD);
  gtk_widget_show (comment_view);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), comment_view);

  gtk_container_add (GTK_CONTAINER (sw), comment_view);

  comment_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (comment_view));

  if (*comment)
    {
      gtk_text_buffer_set_text (comment_buffer, *comment, -1);
    }

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  if (res == GTK_RESPONSE_OK)
    {
      g_free ((gpointer) * text);
      g_free ((gpointer) * context);
      g_free ((gpointer) * comment);

      /* Get the new values for translatable */
      *translatable = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
                                                    (translatable_button));

      /* Comment, text and context */
      *comment = text_buffer_get_text (comment_buffer);
      *text = text_buffer_get_text (text_buffer);
      *context = text_buffer_get_text (context_buffer);

      gtk_widget_destroy (dialog);
      return TRUE;
    }

  gtk_widget_destroy (dialog);
  return FALSE;
}

static void
glade_eprop_text_show_i18n_dialog (GladeEditorProperty *eprop)
{
  GladeEditorPropertyPrivate *priv = eprop->priv;
  gchar *text = glade_property_make_string (priv->property);
  gchar *context = g_strdup (glade_property_i18n_get_context (priv->property));
  gchar *comment = g_strdup (glade_property_i18n_get_comment (priv->property));
  gboolean translatable = glade_property_i18n_get_translatable (priv->property);

  if (glade_editor_property_show_i18n_dialog
      (GTK_WIDGET (eprop), &text, &context, &comment, &translatable))
    {
      glade_command_set_i18n (priv->property, translatable, context, comment);
      glade_eprop_text_changed_common (eprop, text, priv->use_command);

      glade_editor_property_load (eprop, priv->property);
    }

  g_free (text);
  g_free (context);
  g_free (comment);
}

gboolean
glade_editor_property_show_resource_dialog (GladeProject *project,
                                            GtkWidget *parent,
                                            gchar **filename)
{

  GtkWidget *dialog;
  GtkWidget *action_area;
  gchar *folder;

  g_return_val_if_fail (filename != NULL, FALSE);

  dialog =
      gtk_file_chooser_dialog_new (_
                                   ("Select a file from the project resource directory"),
                                   parent ?
                                   GTK_WINDOW (gtk_widget_get_toplevel (parent))
                                   : NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
                                   GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                   GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_OK,
                                           GTK_RESPONSE_CANCEL, -1);

  /* HIG spacings */
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  gtk_box_set_spacing (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 2); /* 2 * 5 + 2 = 12 */
  action_area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
  gtk_container_set_border_width (GTK_CONTAINER (action_area), 5);
  gtk_box_set_spacing (GTK_BOX (action_area), 6);

  folder = glade_project_resource_fullpath (project, ".");
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), folder);
  g_free (folder);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
      gchar *name;

      name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

      *filename = name ? g_path_get_basename (name) : NULL;

      g_free (name);
      gtk_widget_destroy (dialog);
      return TRUE;
    }

  gtk_widget_destroy (dialog);

  return FALSE;
}

static void
glade_eprop_text_show_resource_dialog (GladeEditorProperty *eprop)
{
  GladeWidget  *widget  = glade_property_get_widget (eprop->priv->property);
  GladeProject *project = glade_widget_get_project (widget);
  gchar *text = NULL;

  if (glade_editor_property_show_resource_dialog (project, GTK_WIDGET (eprop), &text))
    {
      glade_eprop_text_changed_common (eprop, text, eprop->priv->use_command);

      glade_editor_property_load (eprop, eprop->priv->property);

      g_free (text);
    }
}

enum
{
  COMBO_COLUMN_TEXT = 0,
  COMBO_COLUMN_PIXBUF,
  COMBO_LAST_COLUMN
};

static GtkListStore *
glade_eprop_text_create_store (GType enum_type)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GEnumClass *eclass;
  guint i;

  eclass = g_type_class_ref (enum_type);

  store = gtk_list_store_new (COMBO_LAST_COLUMN, G_TYPE_STRING, G_TYPE_STRING);

  for (i = 0; i < eclass->n_values; i++)
    {
      const gchar *displayable =
          glade_get_displayable_value (enum_type, eclass->values[i].value_nick);
      if (!displayable)
        displayable = eclass->values[i].value_nick;

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
                          COMBO_COLUMN_TEXT, displayable,
                          COMBO_COLUMN_PIXBUF, eclass->values[i].value_nick,
                          -1);
    }

  g_type_class_unref (eclass);

  return store;
}

static void
eprop_text_stock_changed (GtkComboBox *combo, GladeEditorProperty *eprop)
{
  GladeEPropText *eprop_text = GLADE_EPROP_TEXT (eprop);
  GtkTreeIter iter;
  gchar *text = NULL;
  const gchar *str;

  if (eprop->priv->loading)
    return;

  if (gtk_combo_box_get_active_iter (combo, &iter))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (eprop_text->store), &iter,
                          COMBO_COLUMN_PIXBUF, &text, -1);
      glade_eprop_text_changed_common (eprop, text, eprop->priv->use_command);
      g_free (text);
    }
  else if (gtk_combo_box_get_has_entry (combo))
    {
      str =
          gtk_entry_get_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (combo))));
      glade_eprop_text_changed_common (eprop, str, eprop->priv->use_command);
    }
}

static GtkWidget *
glade_eprop_text_create_input (GladeEditorProperty *eprop)
{
  GladeEPropText *eprop_text = GLADE_EPROP_TEXT (eprop);
  GladePropertyClass *klass;
  GParamSpec *pspec;
  GtkWidget *hbox;

  klass = eprop->priv->klass;
  pspec = glade_property_class_get_pspec (klass);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  if (glade_property_class_stock (klass) || 
      glade_property_class_stock_icon (klass))
    {
      GtkCellRenderer *renderer;
      GtkWidget *child;
      GtkWidget *combo = gtk_combo_box_new_with_entry ();

      gtk_widget_set_halign (hbox, GTK_ALIGN_START);
      gtk_widget_set_valign (hbox, GTK_ALIGN_CENTER);
      
      eprop_text->store = (GtkTreeModel *)
          glade_eprop_text_create_store (glade_property_class_stock (klass) ? 
					 GLADE_TYPE_STOCK : GLADE_TYPE_STOCK_IMAGE);

      gtk_combo_box_set_model (GTK_COMBO_BOX (combo),
                               GTK_TREE_MODEL (eprop_text->store));

      /* let the comboboxentry prepend its intrusive cell first... */
      gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (combo),
                                           COMBO_COLUMN_TEXT);

      renderer = gtk_cell_renderer_pixbuf_new ();
      gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, FALSE);
      gtk_cell_layout_reorder (GTK_CELL_LAYOUT (combo), renderer, 0);
      gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), renderer,
                                      "stock-id", COMBO_COLUMN_PIXBUF, NULL);

      /* Dont allow custom items where an actual GTK+ stock item is expected
       * (i.e. real items come with labels) */
      child = gtk_bin_get_child (GTK_BIN (combo));
      if (glade_property_class_stock (klass))
        gtk_editable_set_editable (GTK_EDITABLE (child), FALSE);
      else
        gtk_editable_set_editable (GTK_EDITABLE (child), TRUE);

      gtk_widget_show (combo);
      gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);
      g_signal_connect (G_OBJECT (combo), "changed",
                        G_CALLBACK (eprop_text_stock_changed), eprop);


      eprop_text->text_entry = combo;
    }
  else if (glade_property_class_multiline (klass) ||
           pspec->value_type == G_TYPE_STRV ||
           pspec->value_type == G_TYPE_VALUE_ARRAY)
    {
      GtkWidget *swindow;
      GtkTextBuffer *buffer;

      swindow = gtk_scrolled_window_new (NULL, NULL);
      gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (swindow), 128);
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
                                      GTK_POLICY_AUTOMATIC,
                                      GTK_POLICY_AUTOMATIC);
      gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (swindow),
                                           GTK_SHADOW_IN);

      eprop_text->text_entry = gtk_text_view_new ();
      buffer =
          gtk_text_view_get_buffer (GTK_TEXT_VIEW (eprop_text->text_entry));

      gtk_container_add (GTK_CONTAINER (swindow), eprop_text->text_entry);
      gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (swindow), TRUE, TRUE, 0);

      gtk_widget_show_all (swindow);

      g_signal_connect (G_OBJECT (buffer), "changed",
                        G_CALLBACK (glade_eprop_text_buffer_changed), eprop);

    }
  else
    {
      eprop_text->text_entry = gtk_entry_new ();
      gtk_widget_show (eprop_text->text_entry);

      gtk_box_pack_start (GTK_BOX (hbox), eprop_text->text_entry, TRUE, TRUE, 0);

      g_signal_connect (G_OBJECT (eprop_text->text_entry), "changed",
                        G_CALLBACK (glade_eprop_text_changed), eprop);

      if (pspec->value_type == GDK_TYPE_PIXBUF)
        {
          gtk_entry_set_icon_from_stock (GTK_ENTRY (eprop_text->text_entry),
                                         GTK_ENTRY_ICON_SECONDARY,
                                         GTK_STOCK_OPEN);

          g_signal_connect_swapped (eprop_text->text_entry, "icon-release",
                                    G_CALLBACK (glade_eprop_text_show_resource_dialog),
                                    eprop);
        }
    }

  if (glade_property_class_translatable (klass))
    {
      if (GTK_IS_ENTRY (eprop_text->text_entry))
        {
          gtk_entry_set_icon_from_stock (GTK_ENTRY (eprop_text->text_entry), 
                                         GTK_ENTRY_ICON_SECONDARY,
                                         GTK_STOCK_EDIT);
          g_signal_connect_swapped (eprop_text->text_entry, "icon-release",
                                    G_CALLBACK (glade_eprop_text_show_i18n_dialog),
                                    eprop);
        }
      else
        {
          GtkWidget *button = gtk_button_new ();
          gtk_button_set_image (GTK_BUTTON (button),
                                gtk_image_new_from_stock (GTK_STOCK_EDIT,
                                                          GTK_ICON_SIZE_MENU));
          gtk_widget_show (button);
          gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
          g_signal_connect_swapped (button, "clicked",
                                    G_CALLBACK (glade_eprop_text_show_i18n_dialog),
                                    eprop);
        }
    }
  return hbox;
}

/*******************************************************************************
                        GladeEditorPropertyBoolClass
 *******************************************************************************/
typedef struct
{
  GladeEditorProperty parent_instance;

  GtkWidget *gswitch;
} GladeEPropBool;

GLADE_MAKE_EPROP (GladeEPropBool, glade_eprop_bool)
#define GLADE_EPROP_BOOL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_BOOL, GladeEPropBool))
#define GLADE_EPROP_BOOL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_BOOL, GladeEPropBoolClass))
#define GLADE_IS_EPROP_BOOL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_BOOL))
#define GLADE_IS_EPROP_BOOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_BOOL))
#define GLADE_EPROP_BOOL_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_BOOL, GladeEPropBoolClass))

static void
glade_eprop_bool_finalize (GObject *object)
{
  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}

static void
glade_eprop_bool_load (GladeEditorProperty *eprop, GladeProperty *property)
{
  /* Chain up first */
  editor_property_class->load (eprop, property);

  if (property)
    {
      GladeEPropBool *eprop_bool = GLADE_EPROP_BOOL (eprop);
      gboolean state = g_value_get_boolean (glade_property_inline_value (property));
      gtk_switch_set_active (GTK_SWITCH (eprop_bool->gswitch), state);
    }
}

static void
glade_eprop_bool_active_notify (GObject             *gobject,
                                GParamSpec          *pspec,
                                GladeEditorProperty *eprop)
{
  GValue val = { 0, };

  if (eprop->priv->loading)
    return;

  g_value_init (&val, G_TYPE_BOOLEAN);
  g_value_set_boolean (&val, gtk_switch_get_active (GTK_SWITCH (gobject)));

  glade_editor_property_commit_no_callback (eprop, &val);

  g_value_unset (&val);
}

static GtkWidget *
glade_eprop_bool_create_input (GladeEditorProperty *eprop)
{
  GladeEPropBool *eprop_bool = GLADE_EPROP_BOOL (eprop);
  
  eprop_bool->gswitch = gtk_switch_new ();
  gtk_widget_set_halign (eprop_bool->gswitch, GTK_ALIGN_START);
  gtk_widget_set_valign (eprop_bool->gswitch, GTK_ALIGN_CENTER);

  g_signal_connect (eprop_bool->gswitch, "notify::active",
                    G_CALLBACK (glade_eprop_bool_active_notify), eprop);

  return eprop_bool->gswitch;
}


/*******************************************************************************
                        GladeEditorPropertyUnicharClass
 *******************************************************************************/
typedef struct
{
  GladeEditorProperty parent_instance;

  GtkWidget *entry;
} GladeEPropUnichar;

GLADE_MAKE_EPROP (GladeEPropUnichar, glade_eprop_unichar)
#define GLADE_EPROP_UNICHAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_UNICHAR, GladeEPropUnichar))
#define GLADE_EPROP_UNICHAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_UNICHAR, GladeEPropUnicharClass))
#define GLADE_IS_EPROP_UNICHAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_UNICHAR))
#define GLADE_IS_EPROP_UNICHAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_UNICHAR))
#define GLADE_EPROP_UNICHAR_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_UNICHAR, GladeEPropUnicharClass))
     static void glade_eprop_unichar_finalize (GObject *object)
{
  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}

static void
glade_eprop_unichar_load (GladeEditorProperty *eprop, GladeProperty *property)
{
  GladeEPropUnichar *eprop_unichar = GLADE_EPROP_UNICHAR (eprop);

  /* Chain up first */
  editor_property_class->load (eprop, property);

  if (property && GTK_IS_ENTRY (eprop_unichar->entry))
    {
      GtkEntry *entry = GTK_ENTRY (eprop_unichar->entry);
      gchar utf8st[8];
      gint n;

      if ((n = g_unichar_to_utf8 (g_value_get_uint (glade_property_inline_value (property)), utf8st)))
        {
          utf8st[n] = '\0';
          gtk_entry_set_text (entry, utf8st);
        }
    }
}


static void
glade_eprop_unichar_changed (GtkWidget *entry, GladeEditorProperty *eprop)
{
  const gchar *text;

  if (eprop->priv->loading)
    return;

  if ((text = gtk_entry_get_text (GTK_ENTRY (entry))) != NULL)
    {
      gunichar unich = g_utf8_get_char (text);
      GValue val = { 0, };

      g_value_init (&val, G_TYPE_UINT);
      g_value_set_uint (&val, unich);

      glade_editor_property_commit_no_callback (eprop, &val);

      g_value_unset (&val);
    }
}

static void
glade_eprop_unichar_delete (GtkEditable *editable,
                            gint start_pos,
                            gint end_pos,
                            GladeEditorProperty *eprop)
{
  if (eprop->priv->loading)
    return;
  gtk_editable_select_region (editable, 0, -1);
  g_signal_stop_emission_by_name (G_OBJECT (editable), "delete_text");
}

static void
glade_eprop_unichar_insert (GtkWidget *entry,
                            const gchar *text,
                            gint length,
                            gint *position,
                            GladeEditorProperty *eprop)
{
  if (eprop->priv->loading)
    return;
  g_signal_handlers_block_by_func
      (G_OBJECT (entry), G_CALLBACK (glade_eprop_unichar_changed), eprop);
  g_signal_handlers_block_by_func
      (G_OBJECT (entry), G_CALLBACK (glade_eprop_unichar_insert), eprop);
  g_signal_handlers_block_by_func
      (G_OBJECT (entry), G_CALLBACK (glade_eprop_unichar_delete), eprop);

  gtk_editable_delete_text (GTK_EDITABLE (entry), 0, -1);
  *position = 0;
  gtk_editable_insert_text (GTK_EDITABLE (entry), text, 1, position);

  g_signal_handlers_unblock_by_func
      (G_OBJECT (entry), G_CALLBACK (glade_eprop_unichar_changed), eprop);
  g_signal_handlers_unblock_by_func
      (G_OBJECT (entry), G_CALLBACK (glade_eprop_unichar_insert), eprop);
  g_signal_handlers_unblock_by_func
      (G_OBJECT (entry), G_CALLBACK (glade_eprop_unichar_delete), eprop);

  g_signal_stop_emission_by_name (G_OBJECT (entry), "insert_text");

  glade_eprop_unichar_changed (entry, eprop);
}

static GtkWidget *
glade_eprop_unichar_create_input (GladeEditorProperty *eprop)
{
  GladeEPropUnichar *eprop_unichar = GLADE_EPROP_UNICHAR (eprop);

  eprop_unichar->entry = gtk_entry_new ();
  gtk_widget_set_halign (eprop_unichar->entry, GTK_ALIGN_START);
  gtk_widget_set_valign (eprop_unichar->entry, GTK_ALIGN_CENTER);

  /* it's 2 to prevent spirious beeps... */
  gtk_entry_set_max_length (GTK_ENTRY (eprop_unichar->entry), 2);

  g_signal_connect (G_OBJECT (eprop_unichar->entry), "changed",
                    G_CALLBACK (glade_eprop_unichar_changed), eprop);
  g_signal_connect (G_OBJECT (eprop_unichar->entry), "insert_text",
                    G_CALLBACK (glade_eprop_unichar_insert), eprop);
  g_signal_connect (G_OBJECT (eprop_unichar->entry), "delete_text",
                    G_CALLBACK (glade_eprop_unichar_delete), eprop);
  return eprop_unichar->entry;
}

/*******************************************************************************
                        GladeEditorPropertyObjectClass
 *******************************************************************************/
enum
{
  OBJ_COLUMN_WIDGET = 0,
  OBJ_COLUMN_WIDGET_NAME,
  OBJ_COLUMN_WIDGET_CLASS,
  OBJ_COLUMN_SELECTED,
  OBJ_COLUMN_SELECTABLE,
  OBJ_NUM_COLUMNS
};

#define GLADE_RESPONSE_CLEAR  42
#define GLADE_RESPONSE_CREATE 43

typedef struct
{
  GladeEditorProperty parent_instance;

  GtkWidget *entry;
} GladeEPropObject;

GLADE_MAKE_EPROP (GladeEPropObject, glade_eprop_object)
#define GLADE_EPROP_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_OBJECT, GladeEPropObject))
#define GLADE_EPROP_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_OBJECT, GladeEPropObjectClass))
#define GLADE_IS_EPROP_OBJECT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_OBJECT))
#define GLADE_IS_EPROP_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_OBJECT))
#define GLADE_EPROP_OBJECT_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_OBJECT, GladeEPropObjectClass))
     static void glade_eprop_object_finalize (GObject *object)
{
  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}


static gchar *
glade_eprop_object_name (const gchar *name,
                         GtkTreeStore *model, GtkTreeIter *parent_iter)
{
  GtkTreePath *path;
  GString *string;
  gint i;

  string = g_string_new (name);

  if (parent_iter)
    {
      path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), parent_iter);
      for (i = 0; i < gtk_tree_path_get_depth (path); i++)
        g_string_prepend (string, "    ");
    }

  return g_string_free (string, FALSE);
}

static gboolean
search_list (GList * list, gpointer data)
{
  return g_list_find (list, data) != NULL;
}


/*
 * Note that widgets is a list of GtkWidgets, while what we store
 * in the model are the associated GladeWidgets.
 */
static void
glade_eprop_object_populate_view_real (GtkTreeStore *model,
                                       GtkTreeIter *parent_iter,
                                       GList *widgets,
                                       GList *selected_widgets,
                                       GList *exception_widgets,
                                       GType object_type,
                                       gboolean parentless)
{
  GList *children, *list;
  GtkTreeIter iter;
  gboolean good_type, has_decendant;

  for (list = widgets; list; list = list->next)
    {
      GladeWidget *widget;
      GladeWidgetAdaptor *adaptor;

      if ((widget = glade_widget_get_from_gobject (list->data)) != NULL)
        {
	  adaptor = glade_widget_get_adaptor (widget);

          has_decendant = 
	    !parentless && glade_widget_has_decendant (widget, object_type);

          good_type = (glade_widget_adaptor_get_object_type (adaptor) == object_type ||
                       g_type_is_a (glade_widget_adaptor_get_object_type (adaptor), object_type));

          if (parentless)
            good_type = good_type && !GWA_IS_TOPLEVEL (adaptor);

          if (good_type || has_decendant)
            {
              gtk_tree_store_append (model, &iter, parent_iter);
              gtk_tree_store_set
                  (model, &iter,
                   OBJ_COLUMN_WIDGET, widget,
                   OBJ_COLUMN_WIDGET_NAME,
                   glade_eprop_object_name (glade_widget_get_name (widget), model, parent_iter),
                   OBJ_COLUMN_WIDGET_CLASS, glade_widget_adaptor_get_title (adaptor),
                   /* Selectable if its a compatible type and
                    * its not itself.
                    */
                   OBJ_COLUMN_SELECTABLE,
                   good_type && !search_list (exception_widgets, widget),
                   OBJ_COLUMN_SELECTED,
                   good_type && search_list (selected_widgets, widget), -1);
            }

          if (has_decendant &&
              (children = glade_widget_adaptor_get_children
               (adaptor, glade_widget_get_object (widget))) != NULL)
            {
              GtkTreeIter *copy = NULL;

              copy = gtk_tree_iter_copy (&iter);
              glade_eprop_object_populate_view_real (model, copy, children,
                                                     selected_widgets,
                                                     exception_widgets,
                                                     object_type, parentless);
              gtk_tree_iter_free (copy);

              g_list_free (children);
            }
        }
    }
}

static void
glade_eprop_object_populate_view (GladeProject *project,
                                  GtkTreeView *view,
                                  GList *selected,
                                  GList *exceptions,
                                  GType object_type,
                                  gboolean parentless)
{
  GtkTreeStore *model = (GtkTreeStore *) gtk_tree_view_get_model (view);
  GList *list, *toplevels = NULL;

  /* Make a list of only the toplevel widgets */
  for (list = (GList *) glade_project_get_objects (project); list;
       list = list->next)
    {
      GObject *object = G_OBJECT (list->data);
      GladeWidget *gwidget = glade_widget_get_from_gobject (object);
      g_assert (gwidget);

      if (glade_widget_get_parent (gwidget) == NULL)
        toplevels = g_list_append (toplevels, object);
    }

  /* add the widgets and recurse */
  glade_eprop_object_populate_view_real (model, NULL, toplevels, selected,
                                         exceptions, object_type, parentless);
  g_list_free (toplevels);
}

static gboolean
glade_eprop_object_clear_iter (GtkTreeModel *model,
                               GtkTreePath *path,
                               GtkTreeIter *iter,
                               gpointer data)
{
  gtk_tree_store_set (GTK_TREE_STORE (model), iter,
                      OBJ_COLUMN_SELECTED, FALSE, -1);
  return FALSE;
}

static gboolean
glade_eprop_object_selected_widget (GtkTreeModel *model,
                                    GtkTreePath *path,
                                    GtkTreeIter *iter,
                                    GladeWidget **ret)
{
  gboolean selected;
  GladeWidget *widget;

  gtk_tree_model_get (model, iter,
                      OBJ_COLUMN_SELECTED, &selected,
                      OBJ_COLUMN_WIDGET, &widget, -1);

  if (selected)
    {
      *ret = widget;
      return TRUE;
    }
  return FALSE;
}

static void
glade_eprop_object_selected (GtkCellRendererToggle *cell,
                             gchar *path_str,
                             GtkTreeModel *model)
{
  GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
  GtkTreeIter iter;
  gboolean enabled, radio;

  radio = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (model), "radio-list"));


  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter, OBJ_COLUMN_SELECTED, &enabled, -1);

  /* Clear the rest of the view first
   */
  if (radio)
    gtk_tree_model_foreach (model, glade_eprop_object_clear_iter, NULL);

  gtk_tree_store_set (GTK_TREE_STORE (model), &iter,
                      OBJ_COLUMN_SELECTED, radio ? TRUE : !enabled, -1);

  gtk_tree_path_free (path);
}

static GtkWidget *
glade_eprop_object_view (gboolean radio)
{
  GtkWidget *view_widget;
  GtkTreeModel *model;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  model = (GtkTreeModel *) gtk_tree_store_new (OBJ_NUM_COLUMNS, G_TYPE_OBJECT,  /* The GladeWidget  */
                                               G_TYPE_STRING,   /* The GladeWidget's name */
                                               G_TYPE_STRING,   /* The GladeWidgetClass title */
                                               G_TYPE_BOOLEAN,  /* Whether this row is selected or not */
                                               G_TYPE_BOOLEAN); /* Whether this GladeWidget is 
                                                                 * of an acceptable type and 
                                                                 * therefore can be selected.
                                                                 */

  g_object_set_data (G_OBJECT (model), "radio-list", GINT_TO_POINTER (radio));

  view_widget = gtk_tree_view_new_with_model (model);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view_widget), FALSE);

  /* Pass ownership to the view */
  g_object_unref (G_OBJECT (model));
  g_object_set (G_OBJECT (view_widget), "enable-search", FALSE, NULL);

        /********************* fake invisible column *********************/
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer), "editable", FALSE, "visible", FALSE, NULL);

  column = gtk_tree_view_column_new_with_attributes (NULL, renderer, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (view_widget), column);

  gtk_tree_view_column_set_visible (column, FALSE);
  gtk_tree_view_set_expander_column (GTK_TREE_VIEW (view_widget), column);

        /************************ selected column ************************/
  renderer = gtk_cell_renderer_toggle_new ();
  g_object_set (G_OBJECT (renderer),
                "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
                "activatable", TRUE, "radio", radio, NULL);
  g_signal_connect (renderer, "toggled",
                    G_CALLBACK (glade_eprop_object_selected), model);
  gtk_tree_view_insert_column_with_attributes
      (GTK_TREE_VIEW (view_widget), 0,
       NULL, renderer,
       "visible", OBJ_COLUMN_SELECTABLE,
       "sensitive", OBJ_COLUMN_SELECTABLE, "active", OBJ_COLUMN_SELECTED, NULL);

        /********************* widget name column *********************/
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer), "editable", FALSE, NULL);
  gtk_tree_view_insert_column_with_attributes
      (GTK_TREE_VIEW (view_widget), 1,
       _("Name"), renderer, "text", OBJ_COLUMN_WIDGET_NAME, NULL);

        /***************** widget class title column ******************/
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "editable", FALSE,
                "style", PANGO_STYLE_ITALIC, "foreground", "Gray", NULL);
  gtk_tree_view_insert_column_with_attributes
      (GTK_TREE_VIEW (view_widget), 2,
       _("Class"), renderer, "text", OBJ_COLUMN_WIDGET_CLASS, NULL);

  return view_widget;
}


static gchar *
glade_eprop_object_dialog_title (GladeEditorProperty *eprop)
{
  GladeWidgetAdaptor *adaptor;
  GParamSpec *pspec;
  const gchar *format;

  pspec = glade_property_class_get_pspec (eprop->priv->klass);

  if (glade_property_class_parentless_widget (eprop->priv->klass))
    format = GLADE_IS_PARAM_SPEC_OBJECTS (pspec) ?
        _("Choose parentless %s type objects in this project") :
        _("Choose a parentless %s in this project");
  else
    format = GLADE_IS_PARAM_SPEC_OBJECTS (pspec) ?
        _("Choose %s type objects in this project") :
        _("Choose a %s in this project");

  if (GLADE_IS_PARAM_SPEC_OBJECTS (pspec))
    return g_strdup_printf (format, g_type_name
                            (glade_param_spec_objects_get_type
                             (GLADE_PARAM_SPEC_OBJECTS (pspec))));
  else if ((adaptor =
            glade_widget_adaptor_get_by_type (pspec->value_type)) != NULL)
    return g_strdup_printf (format, glade_widget_adaptor_get_title (adaptor));

  /* Fallback on type name (which would look like "GtkButton"
   * instead of "Button" and maybe not translated).
   */
  return g_strdup_printf (format, g_type_name (pspec->value_type));
}


gboolean
glade_editor_property_show_object_dialog (GladeProject *project,
                                          const gchar *title,
                                          GtkWidget *parent,
                                          GType object_type,
                                          GladeWidget *exception,
                                          GladeWidget **object)
{
  GtkWidget *dialog;
  GtkWidget *vbox, *label, *sw;
  GtkWidget *tree_view;
  GtkWidget *content_area;
  GtkWidget *action_area;
  GList *selected_list = NULL, *exception_list = NULL;
  gint res;

  g_return_val_if_fail (object != NULL, -1);

  if (!parent)
    parent = glade_app_get_window ();

  dialog = gtk_dialog_new_with_buttons (title,
                                        GTK_WINDOW (parent),
                                        GTK_DIALOG_MODAL,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_CLEAR, GLADE_RESPONSE_CLEAR,
                                        GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

  gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_OK,
                                           GTK_RESPONSE_CANCEL,
                                           GLADE_RESPONSE_CLEAR, -1);

  gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 500);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  /* HIG settings */
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_set_spacing (GTK_BOX (content_area), 2);      /* 2 * 5 + 2 = 12 */
  action_area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
  gtk_container_set_border_width (GTK_CONTAINER (action_area), 5);
  gtk_box_set_spacing (GTK_BOX (action_area), 6);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_show (vbox);

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);

  gtk_box_pack_start (GTK_BOX (content_area), vbox, TRUE, TRUE, 0);

  /* Checklist */
  label = gtk_label_new_with_mnemonic (_("O_bjects:"));
  gtk_widget_show (label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_show (sw);
  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
  gtk_widget_set_size_request (sw, 400, 200);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);


  if (*object)
    selected_list = g_list_prepend (selected_list, *object);

  if (exception)
    exception_list = g_list_prepend (exception_list, exception);

  tree_view = glade_eprop_object_view (TRUE);
  glade_eprop_object_populate_view (project,
                                    GTK_TREE_VIEW (tree_view),
                                    selected_list, exception_list,
                                    object_type, FALSE);
  g_list_free (selected_list);
  g_list_free (exception_list);

  gtk_tree_view_expand_all (GTK_TREE_VIEW (tree_view));

  gtk_widget_show (tree_view);
  gtk_container_add (GTK_CONTAINER (sw), tree_view);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), tree_view);

  /* Run the dialog */
  res = gtk_dialog_run (GTK_DIALOG (dialog));
  if (res == GTK_RESPONSE_OK)
    {
      GladeWidget *selected = NULL;

      gtk_tree_model_foreach
          (gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view)),
           (GtkTreeModelForeachFunc)
           glade_eprop_object_selected_widget, &selected);

      *object = selected;
    }
  else if (res == GLADE_RESPONSE_CLEAR)
    *object = NULL;

  gtk_widget_destroy (dialog);

  return (res == GTK_RESPONSE_OK || res == GLADE_RESPONSE_CLEAR);
}


static void
glade_eprop_object_show_dialog (GladeEditorProperty *eprop)
{
  GtkWidget *dialog, *parent;
  GtkWidget *vbox, *label, *sw;
  GtkWidget *tree_view;
  GtkWidget *content_area;
  GtkWidget *action_area;
  GladeProject *project;
  GladeWidget  *widget;
  GParamSpec *pspec;
  gchar *title = glade_eprop_object_dialog_title (eprop);
  gint res;
  GladeWidgetAdaptor *create_adaptor = NULL;
  GList *selected_list = NULL, *exception_list = NULL;

  widget  = glade_property_get_widget (eprop->priv->property);
  project = glade_widget_get_project (widget);
  parent  = gtk_widget_get_toplevel (GTK_WIDGET (eprop));
  pspec   = glade_property_class_get_pspec (eprop->priv->klass);

  if (glade_property_class_create_type (eprop->priv->klass))
    create_adaptor =
      glade_widget_adaptor_get_by_name (glade_property_class_create_type (eprop->priv->klass));
  if (!create_adaptor &&
      G_TYPE_IS_INSTANTIATABLE (pspec->value_type) && !G_TYPE_IS_ABSTRACT (pspec->value_type))
    create_adaptor = glade_widget_adaptor_get_by_type (pspec->value_type);

  if (create_adaptor)
    {
      dialog = gtk_dialog_new_with_buttons (title,
                                            GTK_WINDOW (parent),
                                            GTK_DIALOG_MODAL,
                                            GTK_STOCK_CANCEL,
                                            GTK_RESPONSE_CANCEL,
                                            GTK_STOCK_CLEAR,
                                            GLADE_RESPONSE_CLEAR, _("_New"),
                                            GLADE_RESPONSE_CREATE, GTK_STOCK_OK,
                                            GTK_RESPONSE_OK, NULL);
      g_free (title);

      gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                               GTK_RESPONSE_OK,
                                               GLADE_RESPONSE_CREATE,
                                               GTK_RESPONSE_CANCEL,
                                               GLADE_RESPONSE_CLEAR, -1);
    }
  else
    {
      dialog = gtk_dialog_new_with_buttons (title,
                                            GTK_WINDOW (parent),
                                            GTK_DIALOG_MODAL,
                                            GTK_STOCK_CANCEL,
                                            GTK_RESPONSE_CANCEL,
                                            GTK_STOCK_CLEAR,
                                            GLADE_RESPONSE_CLEAR, GTK_STOCK_OK,
                                            GTK_RESPONSE_OK, NULL);
      g_free (title);

      gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                               GTK_RESPONSE_OK,
                                               GTK_RESPONSE_CANCEL,
                                               GLADE_RESPONSE_CLEAR, -1);
    }

  gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 500);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  /* HIG settings */
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_set_spacing (GTK_BOX (content_area), 2);      /* 2 * 5 + 2 = 12 */
  action_area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
  gtk_container_set_border_width (GTK_CONTAINER (action_area), 5);
  gtk_box_set_spacing (GTK_BOX (action_area), 6);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_show (vbox);

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);

  gtk_box_pack_start (GTK_BOX (content_area), vbox, TRUE, TRUE, 0);

  /* Checklist */
  label = gtk_label_new_with_mnemonic (_("O_bjects:"));
  gtk_widget_show (label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_show (sw);
  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
  gtk_widget_set_size_request (sw, 400, 200);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);


  exception_list = g_list_prepend (exception_list, widget);
  if (g_value_get_object (glade_property_inline_value (eprop->priv->property)))
    selected_list = g_list_prepend (selected_list,
                                    glade_widget_get_from_gobject
                                    (g_value_get_object
                                     (glade_property_inline_value (eprop->priv->property))));

  tree_view = glade_eprop_object_view (TRUE);
  glade_eprop_object_populate_view (project, GTK_TREE_VIEW (tree_view),
                                    selected_list, exception_list,
                                    pspec->value_type,
                                    glade_property_class_parentless_widget (eprop->priv->klass));
  g_list_free (selected_list);
  g_list_free (exception_list);


  gtk_tree_view_expand_all (GTK_TREE_VIEW (tree_view));

  gtk_widget_show (tree_view);
  gtk_container_add (GTK_CONTAINER (sw), tree_view);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), tree_view);


  /* Run the dialog */
  res = gtk_dialog_run (GTK_DIALOG (dialog));
  if (res == GTK_RESPONSE_OK)
    {
      GladeWidget *selected = NULL;

      gtk_tree_model_foreach
          (gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view)),
           (GtkTreeModelForeachFunc)
           glade_eprop_object_selected_widget, &selected);

      if (selected)
        {
          GValue *value;

          glade_project_selection_set (project, 
				       glade_widget_get_object (widget),
                                       TRUE);

          value = glade_property_class_make_gvalue_from_string
	    (eprop->priv->klass, glade_widget_get_name (selected), project);

          /* Unparent the widget so we can reuse it for this property */
          if (glade_property_class_parentless_widget (eprop->priv->klass))
            {
              GObject *new_object, *old_object = NULL;
              GladeWidget *new_widget;
              GladeProperty *old_ref;

              if (!G_IS_PARAM_SPEC_OBJECT (pspec))
                g_warning
                    ("Parentless widget property should be of object type");
              else
                {
                  glade_property_get (eprop->priv->property, &old_object);
                  new_object = g_value_get_object (value);
                  new_widget = glade_widget_get_from_gobject (new_object);

                  if (new_object && old_object != new_object)
                    {
                      if ((old_ref =
                           glade_widget_get_parentless_widget_ref (new_widget)))
                        {
                          glade_command_push_group (_("Setting %s of %s to %s"),
                                                    glade_property_class_get_name (eprop->priv->klass),
						    glade_widget_get_name (widget), 
						    glade_widget_get_name (new_widget));
                          glade_command_set_property (old_ref, NULL);
                          glade_editor_property_commit (eprop, value);
                          glade_command_pop_group ();
                        }
                      else
                        glade_editor_property_commit (eprop, value);
                    }
                }
            }
          else
            glade_editor_property_commit (eprop, value);

          g_value_unset (value);
          g_free (value);
        }
    }
  else if (res == GLADE_RESPONSE_CREATE)
    {
      GValue *value;
      GladeWidget *new_widget;

      /* translators: Creating 'a widget' for 'a property' of 'a widget' */
      glade_command_push_group (_("Creating %s for %s of %s"),
                                glade_widget_adaptor_get_name (create_adaptor),
                                glade_property_class_get_name (eprop->priv->klass),
                                glade_widget_get_name (widget));

      /* Dont bother if the user canceled the widget */
      if ((new_widget =
           glade_command_create (create_adaptor, NULL, NULL, project)) != NULL)
        {
          glade_project_selection_set (project, glade_widget_get_object (widget), TRUE);

          value = glade_property_class_make_gvalue_from_string
	    (eprop->priv->klass, glade_widget_get_name (new_widget), project);

          glade_editor_property_commit (eprop, value);

          g_value_unset (value);
          g_free (value);
        }

      glade_command_pop_group ();
    }
  else if (res == GLADE_RESPONSE_CLEAR)
    {
      GValue *value = 
	glade_property_class_make_gvalue_from_string (eprop->priv->klass, NULL, project);

      glade_editor_property_commit (eprop, value);

      g_value_unset (value);
      g_free (value);
    }

  gtk_widget_destroy (dialog);
}


static void
glade_eprop_object_load (GladeEditorProperty *eprop, GladeProperty *property)
{
  GladeEPropObject *eprop_object = GLADE_EPROP_OBJECT (eprop);
  gchar *obj_name;

  /* Chain up first */
  editor_property_class->load (eprop, property);

  if (property == NULL)
    return;

  if ((obj_name = glade_widget_adaptor_string_from_value
       (glade_property_class_get_adaptor (eprop->priv->klass),
        eprop->priv->klass, glade_property_inline_value (property))) != NULL)
    {
      gtk_entry_set_text (GTK_ENTRY (eprop_object->entry), obj_name);
      g_free (obj_name);
    }
  else
    gtk_entry_set_text (GTK_ENTRY (eprop_object->entry), "");

}

static GtkWidget *
glade_eprop_object_create_input (GladeEditorProperty *eprop)
{
  GladeEPropObject *eprop_object = GLADE_EPROP_OBJECT (eprop);

  eprop_object->entry = gtk_entry_new ();
  gtk_widget_set_valign (eprop_object->entry, GTK_ALIGN_CENTER);
  gtk_editable_set_editable (GTK_EDITABLE (eprop_object->entry), FALSE);
  gtk_entry_set_icon_from_stock (GTK_ENTRY (eprop_object->entry), 
                                 GTK_ENTRY_ICON_SECONDARY,
                                 GTK_STOCK_EDIT);
  g_signal_connect_swapped (eprop_object->entry, "icon-release",
                            G_CALLBACK (glade_eprop_object_show_dialog), eprop);
  
  return eprop_object->entry;
}


/*******************************************************************************
                        GladeEditorPropertyObjectsClass
 *******************************************************************************/

typedef struct
{
  GladeEditorProperty parent_instance;

  GtkWidget *entry;
} GladeEPropObjects;

GLADE_MAKE_EPROP (GladeEPropObjects, glade_eprop_objects)
#define GLADE_EPROP_OBJECTS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GLADE_TYPE_EPROP_OBJECTS, GladeEPropObjects))
#define GLADE_EPROP_OBJECTS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GLADE_TYPE_EPROP_OBJECTS, GladeEPropObjectsClass))
#define GLADE_IS_EPROP_OBJECTS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GLADE_TYPE_EPROP_OBJECTS))
#define GLADE_IS_EPROP_OBJECTS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GLADE_TYPE_EPROP_OBJECTS))
#define GLADE_EPROP_OBJECTS_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GLADE_EPROP_OBJECTS, GladeEPropObjectsClass))
     static void glade_eprop_objects_finalize (GObject *object)
{
  /* Chain up */
  G_OBJECT_CLASS (editor_property_class)->finalize (object);
}

static void
glade_eprop_objects_load (GladeEditorProperty *eprop, GladeProperty *property)
{
  GladeEPropObjects *eprop_objects = GLADE_EPROP_OBJECTS (eprop);
  gchar *obj_name;

  /* Chain up first */
  editor_property_class->load (eprop, property);

  if (property == NULL)
    return;

  if ((obj_name = glade_widget_adaptor_string_from_value
       (glade_property_class_get_adaptor (eprop->priv->klass),
        eprop->priv->klass, glade_property_inline_value (property))) != NULL)
    {
      gtk_entry_set_text (GTK_ENTRY (eprop_objects->entry), obj_name);
      g_free (obj_name);
    }
  else
    gtk_entry_set_text (GTK_ENTRY (eprop_objects->entry), "");

}

static gboolean
glade_eprop_objects_selected_widget (GtkTreeModel *model,
                                     GtkTreePath *path,
                                     GtkTreeIter *iter,
                                     GList **ret)
{
  gboolean selected;
  GladeWidget *widget;

  gtk_tree_model_get (model, iter,
                      OBJ_COLUMN_SELECTED, &selected,
                      OBJ_COLUMN_WIDGET, &widget, -1);


  if (selected)
    {
      *ret = g_list_append (*ret, glade_widget_get_object (widget));
      g_object_unref (widget);
    }

  return FALSE;
}

static void
glade_eprop_objects_show_dialog (GladeEditorProperty *eprop)
{
  GtkWidget *dialog, *parent;
  GtkWidget *vbox, *label, *sw;
  GtkWidget *tree_view;
  GladeWidget *widget;
  GladeProject *project;
  GParamSpec   *pspec;
  gchar *title = glade_eprop_object_dialog_title (eprop);
  gint res;
  GList *selected_list = NULL, *exception_list = NULL, *selected_objects = NULL, *l;

  /* It's improbable but possible the editor is visible with no
   * property selected, in this case avoid crashes */
  if (!eprop->priv->property)
    return;

  widget  = glade_property_get_widget (eprop->priv->property);
  project = glade_widget_get_project (widget);
  parent  = gtk_widget_get_toplevel (GTK_WIDGET (eprop));
  pspec   = glade_property_class_get_pspec (eprop->priv->klass);

  dialog = gtk_dialog_new_with_buttons (title,
                                        GTK_WINDOW (parent),
                                        GTK_DIALOG_MODAL |
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_STOCK_CLEAR, GLADE_RESPONSE_CLEAR,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
  g_free (title);

  gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 500);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_show (vbox);

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);

  gtk_box_pack_start (GTK_BOX
                      (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox,
                      TRUE, TRUE, 0);

  /* Checklist */
  label = gtk_label_new (_("Objects:"));
  gtk_widget_show (label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_show (sw);
  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
  gtk_widget_set_size_request (sw, 400, 200);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);

  tree_view = glade_eprop_object_view (FALSE);

  /* Dont allow selecting the widget owning this property (perhaps this is wrong) */
  exception_list = g_list_prepend (exception_list, widget);

  /* Build the list of already selected objects */
  glade_property_get (eprop->priv->property, &selected_objects);
  for (l = selected_objects; l; l = l->next)
    selected_list = g_list_prepend (selected_list, glade_widget_get_from_gobject (l->data));

  glade_eprop_object_populate_view (project, GTK_TREE_VIEW (tree_view),
                                    selected_list, exception_list,
                                    glade_param_spec_objects_get_type (GLADE_PARAM_SPEC_OBJECTS (pspec)),
                                    glade_property_class_parentless_widget (eprop->priv->klass));
  g_list_free (selected_list);
  g_list_free (exception_list);

  gtk_tree_view_expand_all (GTK_TREE_VIEW (tree_view));

  gtk_widget_show (tree_view);
  gtk_container_add (GTK_CONTAINER (sw), tree_view);

  /* Run the dialog */
  res = gtk_dialog_run (GTK_DIALOG (dialog));
  if (res == GTK_RESPONSE_OK)
    {
      GValue *value;
      GList *selected = NULL;

      gtk_tree_model_foreach
          (gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view)),
           (GtkTreeModelForeachFunc)
           glade_eprop_objects_selected_widget, &selected);

      value = glade_property_class_make_gvalue (eprop->priv->klass, selected);

      glade_editor_property_commit (eprop, value);

      g_value_unset (value);
      g_free (value);
    }
  else if (res == GLADE_RESPONSE_CLEAR)
    {
      GValue *value = glade_property_class_make_gvalue (eprop->priv->klass, NULL);

      glade_editor_property_commit (eprop, value);

      g_value_unset (value);
      g_free (value);
    }
  gtk_widget_destroy (dialog);
}

static GtkWidget *
glade_eprop_objects_create_input (GladeEditorProperty *eprop)
{
  GladeEPropObjects *eprop_objects = GLADE_EPROP_OBJECTS (eprop);

  eprop_objects->entry = gtk_entry_new ();
  gtk_widget_set_valign (eprop_objects->entry, GTK_ALIGN_CENTER);
  gtk_editable_set_editable (GTK_EDITABLE (eprop_objects->entry), FALSE);
  gtk_entry_set_icon_from_stock (GTK_ENTRY (eprop_objects->entry), 
                                 GTK_ENTRY_ICON_SECONDARY,
                                 GTK_STOCK_EDIT);
  g_signal_connect_swapped (eprop_objects->entry, "icon-release",
                            G_CALLBACK (glade_eprop_objects_show_dialog), eprop);
  
  return eprop_objects->entry;
}

/*******************************************************************************
                                     API
 *******************************************************************************/
/**
 * glade_editor_property_commit:
 * @eprop: A #GladeEditorProperty
 * @value: The #GValue to commit
 *
 * Commits @value to the property currently being edited by @eprop.
 *
 */
void
glade_editor_property_commit (GladeEditorProperty *eprop, GValue *value)
{
  g_return_if_fail (GLADE_IS_EDITOR_PROPERTY (eprop));
  g_return_if_fail (G_IS_VALUE (value));

  g_signal_emit (G_OBJECT (eprop), glade_eprop_signals[COMMIT], 0, value);
}

/**
 * glade_editor_property_load:
 * @eprop: A #GladeEditorProperty
 * @property: A #GladeProperty
 *
 * Loads @property values into @eprop and connects.
 * (the editor property will watch the property's value
 * until its loaded with another property or %NULL)
 */
void
glade_editor_property_load (GladeEditorProperty *eprop,
                            GladeProperty *property)
{
  g_return_if_fail (GLADE_IS_EDITOR_PROPERTY (eprop));
  g_return_if_fail (property == NULL || GLADE_IS_PROPERTY (property));

  eprop->priv->loading = TRUE;
  GLADE_EDITOR_PROPERTY_GET_CLASS (eprop)->load (eprop, property);
  eprop->priv->loading = FALSE;
}


/**
 * glade_editor_property_load_by_widget:
 * @eprop: A #GladeEditorProperty
 * @widget: A #GladeWidget
 *
 * Convenience function to load the appropriate #GladeProperty into
 * @eprop from @widget
 */
void
glade_editor_property_load_by_widget (GladeEditorProperty *eprop,
                                      GladeWidget *widget)
{
  GladeProperty *property = NULL;

  g_return_if_fail (GLADE_IS_EDITOR_PROPERTY (eprop));
  g_return_if_fail (widget == NULL || GLADE_IS_WIDGET (widget));

  if (widget)
    {
      /* properties are allowed to be missing on some internal widgets */
      if (glade_property_class_get_is_packing (eprop->priv->klass))
        property = glade_widget_get_pack_property (widget, glade_property_class_id (eprop->priv->klass));
      else
        property = glade_widget_get_property (widget, glade_property_class_id (eprop->priv->klass));

      glade_editor_property_load (eprop, property);

      if (property)
        {
          g_assert (eprop->priv->klass == glade_property_get_class (property));

          gtk_widget_show (GTK_WIDGET (eprop));
          gtk_widget_show (GTK_WIDGET (eprop->priv->item_label));
        }
      else
        {
          gtk_widget_hide (GTK_WIDGET (eprop));
          gtk_widget_hide (GTK_WIDGET (eprop->priv->item_label));
        }
    }
  else
    glade_editor_property_load (eprop, NULL);
}
