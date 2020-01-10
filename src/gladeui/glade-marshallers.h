
#ifndef ___glade_marshal_MARSHAL_H__
#define ___glade_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:POINTER,POINTER (./glade-marshallers.list:1) */
extern void _glade_marshal_VOID__POINTER_POINTER (GClosure     *closure,
                                                  GValue       *return_value,
                                                  guint         n_param_values,
                                                  const GValue *param_values,
                                                  gpointer      invocation_hint,
                                                  gpointer      marshal_data);

/* VOID:POINTER (./glade-marshallers.list:2) */
#define _glade_marshal_VOID__POINTER	g_cclosure_marshal_VOID__POINTER

/* VOID:STRING,ULONG,UINT,STRING (./glade-marshallers.list:3) */
extern void _glade_marshal_VOID__STRING_ULONG_UINT_STRING (GClosure     *closure,
                                                           GValue       *return_value,
                                                           guint         n_param_values,
                                                           const GValue *param_values,
                                                           gpointer      invocation_hint,
                                                           gpointer      marshal_data);

/* VOID:OBJECT (./glade-marshallers.list:4) */
#define _glade_marshal_VOID__OBJECT	g_cclosure_marshal_VOID__OBJECT

/* VOID:STRING (./glade-marshallers.list:5) */
#define _glade_marshal_VOID__STRING	g_cclosure_marshal_VOID__STRING

/* VOID:INT,INT (./glade-marshallers.list:6) */
extern void _glade_marshal_VOID__INT_INT (GClosure     *closure,
                                          GValue       *return_value,
                                          guint         n_param_values,
                                          const GValue *param_values,
                                          gpointer      invocation_hint,
                                          gpointer      marshal_data);

/* VOID:OBJECT,OBJECT (./glade-marshallers.list:7) */
extern void _glade_marshal_VOID__OBJECT_OBJECT (GClosure     *closure,
                                                GValue       *return_value,
                                                guint         n_param_values,
                                                const GValue *param_values,
                                                gpointer      invocation_hint,
                                                gpointer      marshal_data);

/* VOID:OBJECT,BOOLEAN (./glade-marshallers.list:8) */
extern void _glade_marshal_VOID__OBJECT_BOOLEAN (GClosure     *closure,
                                                 GValue       *return_value,
                                                 guint         n_param_values,
                                                 const GValue *param_values,
                                                 gpointer      invocation_hint,
                                                 gpointer      marshal_data);

/* VOID:STRING,STRING,STRING (./glade-marshallers.list:9) */
extern void _glade_marshal_VOID__STRING_STRING_STRING (GClosure     *closure,
                                                       GValue       *return_value,
                                                       guint         n_param_values,
                                                       const GValue *param_values,
                                                       gpointer      invocation_hint,
                                                       gpointer      marshal_data);

/* OBJECT:POINTER (./glade-marshallers.list:10) */
extern void _glade_marshal_OBJECT__POINTER (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* OBJECT:OBJECT,UINT (./glade-marshallers.list:11) */
extern void _glade_marshal_OBJECT__OBJECT_UINT (GClosure     *closure,
                                                GValue       *return_value,
                                                guint         n_param_values,
                                                const GValue *param_values,
                                                gpointer      invocation_hint,
                                                gpointer      marshal_data);

/* BOOLEAN:STRING (./glade-marshallers.list:12) */
extern void _glade_marshal_BOOLEAN__STRING (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* BOOLEAN:BOXED (./glade-marshallers.list:13) */
extern void _glade_marshal_BOOLEAN__BOXED (GClosure     *closure,
                                           GValue       *return_value,
                                           guint         n_param_values,
                                           const GValue *param_values,
                                           gpointer      invocation_hint,
                                           gpointer      marshal_data);

/* BOOLEAN:OBJECT (./glade-marshallers.list:14) */
extern void _glade_marshal_BOOLEAN__OBJECT (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* BOOLEAN:OBJECT,BOXED (./glade-marshallers.list:15) */
extern void _glade_marshal_BOOLEAN__OBJECT_BOXED (GClosure     *closure,
                                                  GValue       *return_value,
                                                  guint         n_param_values,
                                                  const GValue *param_values,
                                                  gpointer      invocation_hint,
                                                  gpointer      marshal_data);

/* BOOLEAN:OBJECT,POINTER (./glade-marshallers.list:16) */
extern void _glade_marshal_BOOLEAN__OBJECT_POINTER (GClosure     *closure,
                                                    GValue       *return_value,
                                                    guint         n_param_values,
                                                    const GValue *param_values,
                                                    gpointer      invocation_hint,
                                                    gpointer      marshal_data);

/* BOOLEAN:OBJECT,BOOLEAN (./glade-marshallers.list:17) */
extern void _glade_marshal_BOOLEAN__OBJECT_BOOLEAN (GClosure     *closure,
                                                    GValue       *return_value,
                                                    guint         n_param_values,
                                                    const GValue *param_values,
                                                    gpointer      invocation_hint,
                                                    gpointer      marshal_data);

/* BOOLEAN:OBJECT,UINT (./glade-marshallers.list:18) */
extern void _glade_marshal_BOOLEAN__OBJECT_UINT (GClosure     *closure,
                                                 GValue       *return_value,
                                                 guint         n_param_values,
                                                 const GValue *param_values,
                                                 gpointer      invocation_hint,
                                                 gpointer      marshal_data);

/* BOOLEAN:OBJECT,OBJECT (./glade-marshallers.list:19) */
extern void _glade_marshal_BOOLEAN__OBJECT_OBJECT (GClosure     *closure,
                                                   GValue       *return_value,
                                                   guint         n_param_values,
                                                   const GValue *param_values,
                                                   gpointer      invocation_hint,
                                                   gpointer      marshal_data);

/* BOOLEAN:OBJECT,STRING (./glade-marshallers.list:20) */
extern void _glade_marshal_BOOLEAN__OBJECT_STRING (GClosure     *closure,
                                                   GValue       *return_value,
                                                   guint         n_param_values,
                                                   const GValue *param_values,
                                                   gpointer      invocation_hint,
                                                   gpointer      marshal_data);

/* BOOLEAN:STRING,STRING,STRING,BOXED (./glade-marshallers.list:21) */
extern void _glade_marshal_BOOLEAN__STRING_STRING_STRING_BOXED (GClosure     *closure,
                                                                GValue       *return_value,
                                                                guint         n_param_values,
                                                                const GValue *param_values,
                                                                gpointer      invocation_hint,
                                                                gpointer      marshal_data);

/* BOOLEAN:STRING,BOXED,OBJECT (./glade-marshallers.list:22) */
extern void _glade_marshal_BOOLEAN__STRING_BOXED_OBJECT (GClosure     *closure,
                                                         GValue       *return_value,
                                                         guint         n_param_values,
                                                         const GValue *param_values,
                                                         gpointer      invocation_hint,
                                                         gpointer      marshal_data);

/* STRING:OBJECT (./glade-marshallers.list:23) */
extern void _glade_marshal_STRING__OBJECT (GClosure     *closure,
                                           GValue       *return_value,
                                           guint         n_param_values,
                                           const GValue *param_values,
                                           gpointer      invocation_hint,
                                           gpointer      marshal_data);

/* INT:OBJECT,BOXED (./glade-marshallers.list:24) */
extern void _glade_marshal_INT__OBJECT_BOXED (GClosure     *closure,
                                              GValue       *return_value,
                                              guint         n_param_values,
                                              const GValue *param_values,
                                              gpointer      invocation_hint,
                                              gpointer      marshal_data);

/* BOXED:OBJECT (./glade-marshallers.list:25) */
extern void _glade_marshal_BOXED__OBJECT (GClosure     *closure,
                                          GValue       *return_value,
                                          guint         n_param_values,
                                          const GValue *param_values,
                                          gpointer      invocation_hint,
                                          gpointer      marshal_data);

G_END_DECLS

#endif /* ___glade_marshal_MARSHAL_H__ */

