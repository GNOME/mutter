/*
 * Copyright (C) 2025 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "mdk-launchers-editor.h"

#include "mdk-context.h"
#include "mdk-launcher-adder.h"
#include "mdk-launcher-entry.h"
#include "mdk-launcher.h"

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MdkLaunchersEditor
{
  AdwDialog parent;

  MdkContext *context;

  AdwPreferencesGroup *launchers_group;
  GList *entries;

  GtkButton *add_launcher;
};

G_DEFINE_TYPE (MdkLaunchersEditor, mdk_launchers_editor, ADW_TYPE_DIALOG)

static void
update_launchers (MdkLaunchersEditor *launchers_editor)
{
  GPtrArray *launchers;
  GList *l;
  size_t i;

  for (l = launchers_editor->entries; l; l = l->next)
    adw_preferences_group_remove (launchers_editor->launchers_group, l->data);
  g_clear_pointer (&launchers_editor->entries, g_list_free);

  launchers = mdk_context_get_launchers (launchers_editor->context);
  for (i = 0; i < launchers->len; i++)
    {
      MdkLauncher *launcher = g_ptr_array_index (launchers, i);
      MdkLauncherEntry *entry;

      entry = g_object_new (MDK_TYPE_LAUNCHER_ENTRY,
                            "launcher", launcher,
                            NULL);
      adw_preferences_group_add (launchers_editor->launchers_group,
                                 GTK_WIDGET (entry));
      launchers_editor->entries = g_list_append (launchers_editor->entries,
                                                 entry);
    }
}

static void
on_add_launcher_clicked (MdkLaunchersEditor *launchers_editor)
{
  AdwDialog *dialog;

  dialog = g_object_new (MDK_TYPE_LAUNCHER_ADDER,
                         "context", launchers_editor->context,
                         NULL);
  adw_dialog_present (dialog, GTK_WIDGET (launchers_editor));
}

static void
mdk_launchers_editor_dispose (GObject *object)
{
  MdkLaunchersEditor *launchers_editor = MDK_LAUNCHERS_EDITOR (object);

  g_list_free (launchers_editor->entries);

  gtk_widget_dispose_template (GTK_WIDGET (object), MDK_TYPE_LAUNCHERS_EDITOR);

  G_OBJECT_CLASS (mdk_launchers_editor_parent_class)->dispose (object);
}

static void
mdk_launchers_editor_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MdkLaunchersEditor *launchers_editor = MDK_LAUNCHERS_EDITOR (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      launchers_editor->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_launchers_editor_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MdkLaunchersEditor *launchers_editor = MDK_LAUNCHERS_EDITOR (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, launchers_editor->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_launchers_editor_constructed (GObject *object)
{
  MdkLaunchersEditor *launchers_editor = MDK_LAUNCHERS_EDITOR (object);

  g_signal_connect_object (launchers_editor->context,
                           "launchers-changed",
                           G_CALLBACK (update_launchers),
                           launchers_editor,
                           G_CONNECT_SWAPPED);
  update_launchers (launchers_editor);

  g_signal_connect_object (launchers_editor->add_launcher,
                           "clicked",
                           G_CALLBACK (on_add_launcher_clicked),
                           launchers_editor,
                           G_CONNECT_SWAPPED);

  G_OBJECT_CLASS (mdk_launchers_editor_parent_class)->constructed (object);
}

static void
mdk_launchers_editor_class_init (MdkLaunchersEditorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = mdk_launchers_editor_dispose;
  object_class->set_property = mdk_launchers_editor_set_property;
  object_class->get_property = mdk_launchers_editor_get_property;
  object_class->constructed = mdk_launchers_editor_constructed;

  obj_props[PROP_CONTEXT] = g_param_spec_object ("context", NULL, NULL,
                                                 MDK_TYPE_CONTEXT,
                                                 G_PARAM_CONSTRUCT_ONLY |
                                                 G_PARAM_READWRITE |
                                                 G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/ui/mdk-launchers-editor.ui");
  gtk_widget_class_bind_template_child (widget_class, MdkLaunchersEditor,
                                        launchers_group);
  gtk_widget_class_bind_template_child (widget_class, MdkLaunchersEditor,
                                        add_launcher);
}

static void
mdk_launchers_editor_init (MdkLaunchersEditor *launchers_editor)
{
  gtk_widget_init_template (GTK_WIDGET (launchers_editor));
}
