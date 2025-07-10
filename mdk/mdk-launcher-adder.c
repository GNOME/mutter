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

#include "mdk-launcher-adder.h"

#include <glib/gi18n-lib.h>

#include "mdk-context.h"
#include "mdk-launcher.h"

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MdkLauncherAdder
{
  AdwDialog parent;

  MdkContext *context;

  GtkWidget *type_combo_row;

  GtkSingleSelection *selection;

  GtkWidget *entry;
  GtkWidget *icon;
  GtkWidget *popup;
  GtkWidget *app_list;
  GtkWidget *add_button;


  GtkFilter *filter;
  gulong changed_id;

  MdkLauncherType launcher_type;

  GAppInfo *selected_app_info;

  char *search;
};

G_DEFINE_TYPE (MdkLauncherAdder, mdk_launcher_adder, ADW_TYPE_DIALOG)

static void
mdk_launcher_adder_dispose (GObject *object)
{
  MdkLauncherAdder *launcher_adder = MDK_LAUNCHER_ADDER (object);

  g_clear_pointer (&launcher_adder->popup, gtk_widget_unparent);
  g_clear_object (&launcher_adder->selection);
  g_clear_pointer (&launcher_adder->search, g_free);
  g_clear_object (&launcher_adder->selected_app_info);

  gtk_widget_dispose_template (GTK_WIDGET (object), MDK_TYPE_LAUNCHER_ADDER);

  G_OBJECT_CLASS (mdk_launcher_adder_parent_class)->dispose (object);
}

static void
mdk_launcher_adder_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MdkLauncherAdder *launcher_adder = MDK_LAUNCHER_ADDER (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      launcher_adder->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_launcher_adder_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MdkLauncherAdder *launcher_adder = MDK_LAUNCHER_ADDER (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, launcher_adder->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
resolves_to_executable (const char *string)
{
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) argv = NULL;
  g_autofree char *full_path = NULL;
  int argc;

  if (!g_shell_parse_argv (string, &argc, &argv, &error))
    {
      if (!g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING))
        g_warning ("Failed to parse string: %s", error->message);
      return FALSE;
    }

  full_path = g_find_program_in_path (argv[0]);
  return !!full_path;
}

static gboolean
has_desktop_launcher (MdkLauncherAdder *launcher_adder,
                      GAppInfo         *new_app_info)
{
  GPtrArray *launchers = mdk_context_get_launchers (launcher_adder->context);
  size_t i;

  for (i = 0; i < launchers->len; i++)
    {
      MdkLauncher *launcher = g_ptr_array_index (launchers, i);
      GAppInfo *app_info;

      if (mdk_launcher_get_type (launcher) != MDK_LAUNCHER_TYPE_DESKTOP)
        continue;

      app_info = mdk_launcher_get_app_info (launcher);
      if (g_strcmp0 (g_app_info_get_id (app_info),
                     g_app_info_get_id (new_app_info)) == 0)
        return TRUE;
    }

  return FALSE;
}

static gboolean
has_executable_launcher (MdkLauncherAdder *launcher_adder,
                         const char       *text)
{
  g_auto (GStrv) argv = NULL;
  GPtrArray *launchers = mdk_context_get_launchers (launcher_adder->context);
  size_t i;
  int argc;

  g_shell_parse_argv (text, &argc, &argv, NULL);

  for (i = 0; i < launchers->len; i++)
    {
      MdkLauncher *launcher = g_ptr_array_index (launchers, i);
      GStrv launcher_argv;

      if (mdk_launcher_get_type (launcher) != MDK_LAUNCHER_TYPE_EXEC)
        continue;

      launcher_argv = mdk_launcher_get_argv (launcher);
      if (g_strv_equal ((const char * const *) argv,
                        (const char * const *) launcher_argv))
        return TRUE;
    }

  return FALSE;
}

static void
update_add_button (MdkLauncherAdder *launcher_adder)
{
  switch (launcher_adder->launcher_type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      {
        GAppInfo *app_info = launcher_adder->selected_app_info;

        gtk_widget_set_sensitive (launcher_adder->add_button,
                                  !!launcher_adder->selected_app_info &&
                                  !has_desktop_launcher (launcher_adder,
                                                         app_info));
        return;
      }
    case MDK_LAUNCHER_TYPE_EXEC:
      {
        const char *text;

        text = gtk_editable_get_text (GTK_EDITABLE (launcher_adder->entry));
        gtk_widget_set_sensitive (launcher_adder->add_button,
                                  resolves_to_executable (text) &&
                                  !has_executable_launcher (launcher_adder, text));
        return;
      }
    }
  g_assert_not_reached ();
}

static void
suggestion_entry_row_activated (GtkListView      *list_view,
                                unsigned int      position,
                                MdkLauncherAdder *launcher_adder)
{
  GListModel *list_model = G_LIST_MODEL (gtk_list_view_get_model (list_view));
  GAppInfo *app_info = g_list_model_get_item (list_model, position);
  AdwEntryRow *entry_row = ADW_ENTRY_ROW (launcher_adder->entry);
  GIcon *gicon = g_app_info_get_icon (app_info);

  gtk_image_set_from_gicon (GTK_IMAGE (launcher_adder->icon), gicon);

  g_signal_handler_block (entry_row, launcher_adder->changed_id);
  gtk_editable_set_text (GTK_EDITABLE (entry_row),
                         g_app_info_get_display_name (app_info));
  g_signal_handler_unblock (entry_row, launcher_adder->changed_id);

  g_set_object (&launcher_adder->selected_app_info, app_info);

  gtk_popover_popdown (GTK_POPOVER (launcher_adder->popup));

  update_add_button (launcher_adder);
}

static void
text_changed_idle (gpointer user_data)
{
  MdkLauncherAdder *launcher_adder = MDK_LAUNCHER_ADDER (user_data);
  AdwEntryRow *entry_row = ADW_ENTRY_ROW (launcher_adder->entry);
  const char *text;

  text = gtk_editable_get_text (GTK_EDITABLE (launcher_adder->entry));

  g_free (launcher_adder->search);
  launcher_adder->search = g_strdup (text);

  gtk_image_set_from_icon_name (GTK_IMAGE (launcher_adder->icon),
                                "application-x-executable-symbolic");
  g_clear_object (&launcher_adder->selected_app_info);

  if (adw_entry_row_get_text_length (entry_row) == 0)
    gtk_popover_popdown (GTK_POPOVER (launcher_adder->popup));
  else if (launcher_adder->launcher_type == MDK_LAUNCHER_TYPE_DESKTOP)
    gtk_popover_popup (GTK_POPOVER (launcher_adder->popup));

  update_add_button (launcher_adder);

  gtk_filter_changed (launcher_adder->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
on_text_changed (GtkEditable      *editable,
                 GParamSpec       *pspec,
                 MdkLauncherAdder *launcher_adder)
{
  g_idle_add_once (text_changed_idle, launcher_adder);
}

static void
setup_item (GtkSignalListItemFactory *factory,
            GtkListItem              *list_item,
            gpointer                  data)
{
  GtkWidget *box;
  GtkWidget *icon;
  GtkWidget *label;

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  icon = gtk_image_new ();
  label = gtk_label_new (NULL);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);

  gtk_box_append (GTK_BOX (box), icon);
  gtk_box_append (GTK_BOX (box), label);

  gtk_list_item_set_child (list_item, box);
}

static void
bind_item (GtkSignalListItemFactory *factory,
           GtkListItem              *list_item,
           gpointer                  data)
{
  gpointer item;
  GtkWidget *box;
  GtkWidget *icon;
  GtkWidget *label;
  GValue value = G_VALUE_INIT;
  GAppInfo *app_info;

  item = gtk_list_item_get_item (list_item);

  box = gtk_list_item_get_child (list_item);
  icon = gtk_widget_get_first_child (box);
  label = gtk_widget_get_next_sibling (icon);

  app_info = G_APP_INFO (item);

  gtk_label_set_label (GTK_LABEL (label),
                       g_app_info_get_display_name (app_info));
  gtk_image_set_from_gicon (GTK_IMAGE (icon), g_app_info_get_icon (app_info));
  g_value_unset (&value);
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller,
                unsigned int           keyval,
                unsigned int           keycode,
                GdkModifierType        state,
                MdkLauncherAdder      *launcher_adder)
{
  if (state & (GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_CONTROL_MASK))
    return FALSE;

  if (keyval == GDK_KEY_Escape)
    {
      if (gtk_widget_get_mapped (launcher_adder->popup))
        {
          gtk_popover_popdown (GTK_POPOVER (launcher_adder->popup));
          return TRUE;
       }
    }

  return FALSE;
}

static gboolean
filter_func (gpointer item,
             gpointer user_data)
{
  MdkLauncherAdder *launcher_adder = MDK_LAUNCHER_ADDER (user_data);
  GAppInfo *app_info = G_APP_INFO (item);
  const char *string = g_app_info_get_display_name (app_info);

  if (!launcher_adder->search)
    return TRUE;

  return !!strcasestr (string, launcher_adder->search);
}

static void
init_app_auto_complete (MdkLauncherAdder *launcher_adder)
{
  GtkScrolledWindow *scrolled_window;
  GListStore *app_entries;
  g_autolist (GAppInfo) app_infos = NULL;
  GtkFilter *filter;
  GtkFilterListModel *filter_model;
  GtkSingleSelection *selection;
  g_autoptr (GtkListItemFactory) factory = NULL;
  GtkEventController *controller;
  GList *l;

  launcher_adder->popup = gtk_popover_new ();
  gtk_popover_set_position (GTK_POPOVER (launcher_adder->popup),
                            GTK_POS_BOTTOM);
  gtk_popover_set_autohide (GTK_POPOVER (launcher_adder->popup), FALSE);
  gtk_popover_set_has_arrow (GTK_POPOVER (launcher_adder->popup), FALSE);
  gtk_widget_set_halign (launcher_adder->popup, GTK_ALIGN_START);
  gtk_widget_add_css_class (launcher_adder->popup, "menu");
  gtk_widget_set_parent (launcher_adder->popup,
                         GTK_WIDGET (launcher_adder->entry));

  scrolled_window = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
  gtk_scrolled_window_set_policy (scrolled_window,
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_max_content_height (scrolled_window, 400);
  gtk_scrolled_window_set_propagate_natural_height (scrolled_window,
                                                    TRUE);

  gtk_popover_set_child (GTK_POPOVER (launcher_adder->popup),
                         GTK_WIDGET (scrolled_window));
  launcher_adder->app_list = gtk_list_view_new (NULL, NULL);
  gtk_list_view_set_single_click_activate (GTK_LIST_VIEW (launcher_adder->app_list),
                                           TRUE);

  g_signal_connect (launcher_adder->app_list, "activate",
                    G_CALLBACK (suggestion_entry_row_activated),
                    launcher_adder);
  gtk_scrolled_window_set_child (scrolled_window,
                                 launcher_adder->app_list);

  app_entries = g_list_store_new (G_TYPE_APP_INFO);
  app_infos = g_app_info_get_all ();
  for (l = app_infos; l; l = l->next)
    {
      GAppInfo *app_info = G_APP_INFO (l->data);

      g_list_store_append (app_entries, app_info);
    }

  filter = GTK_FILTER (gtk_custom_filter_new (filter_func,
                                              launcher_adder,
                                              NULL));
  filter_model =
    gtk_filter_list_model_new (G_LIST_MODEL (app_entries),
                               filter);
  g_set_object (&launcher_adder->filter, filter);

  selection = gtk_single_selection_new (G_LIST_MODEL (filter_model));
  gtk_single_selection_set_autoselect (selection, FALSE);
  gtk_single_selection_set_can_unselect (selection, TRUE);
  gtk_single_selection_set_selected (selection, GTK_INVALID_LIST_POSITION);
  g_set_object (&launcher_adder->selection, selection);
  gtk_list_view_set_model (GTK_LIST_VIEW (launcher_adder->app_list),
                           GTK_SELECTION_MODEL (selection));
  launcher_adder->changed_id = g_signal_connect (launcher_adder->entry,
                                                 "notify::text",
                                                 G_CALLBACK (on_text_changed),
                                                 launcher_adder);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (setup_item), launcher_adder);
  g_signal_connect (factory, "bind", G_CALLBACK (bind_item), launcher_adder);
  gtk_list_view_set_factory (GTK_LIST_VIEW (launcher_adder->app_list), factory);

  controller = gtk_event_controller_key_new ();
  g_signal_connect (controller, "key-pressed",
                    G_CALLBACK (on_key_pressed),
                    launcher_adder);
  gtk_widget_add_controller (launcher_adder->entry, controller);
}

static void
update_launcher_type (MdkLauncherAdder *launcher_adder)
{
  switch (launcher_adder->launcher_type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (launcher_adder->entry),
                                     _("Application"));
      update_add_button (launcher_adder);
      return;
    case MDK_LAUNCHER_TYPE_EXEC:
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (launcher_adder->entry),
                                     _("Executable"));
      gtk_popover_popdown (GTK_POPOVER (launcher_adder->popup));
      g_clear_object (&launcher_adder->selected_app_info);
      update_add_button (launcher_adder);
      return;
    }
  g_assert_not_reached ();
}

static void
on_type_selected (AdwComboRow      *type_combo_row,
                  GParamSpec       *pspec,
                  MdkLauncherAdder *launcher_adder)
{
  unsigned int selected_index;

  selected_index = adw_combo_row_get_selected (type_combo_row);
  launcher_adder->launcher_type = selected_index;

  update_launcher_type (launcher_adder);
}

static void
on_add_button_activated (MdkLauncherAdder *launcher_adder)
{
  switch (launcher_adder->launcher_type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      {
        GAppInfo *app_info = launcher_adder->selected_app_info;
        g_autofree char *app_id = NULL;
        const char * const * action_ids;
        const char *action;

        action_ids =
          g_desktop_app_info_list_actions (G_DESKTOP_APP_INFO (app_info));
        action = action_ids[0];

        app_id = mdk_get_app_id_from_app_info (app_info);
        mdk_context_add_launcher (launcher_adder->context,
                                  launcher_adder->launcher_type,
                                  app_id,
                                  action ? action : "");
        adw_dialog_close (ADW_DIALOG (launcher_adder));
        return;
      }
    case MDK_LAUNCHER_TYPE_EXEC:
      {
        const char *text;

        text = gtk_editable_get_text (GTK_EDITABLE (launcher_adder->entry));
        mdk_context_add_launcher (launcher_adder->context,
                                  launcher_adder->launcher_type,
                                  text, "");
        adw_dialog_close (ADW_DIALOG (launcher_adder));
        return;
      }
    }
  g_assert_not_reached ();
}

static void
mdk_launcher_adder_constructed (GObject *object)
{
  MdkLauncherAdder *launcher_adder = MDK_LAUNCHER_ADDER (object);

  launcher_adder->icon = g_object_ref_sink (gtk_image_new ());
  gtk_image_set_from_icon_name (GTK_IMAGE (launcher_adder->icon),
                                "application-x-executable-symbolic");
  adw_entry_row_add_prefix (ADW_ENTRY_ROW (launcher_adder->entry),
                            launcher_adder->icon);

  g_signal_connect (launcher_adder->type_combo_row, "notify::selected-item",
                    G_CALLBACK (on_type_selected),
                    launcher_adder);

  init_app_auto_complete (launcher_adder);

  g_signal_connect_object (launcher_adder->add_button,
                           "activated",
                           G_CALLBACK (on_add_button_activated),
                           launcher_adder,
                           G_CONNECT_SWAPPED);

  update_launcher_type (launcher_adder);

  adw_dialog_set_focus (ADW_DIALOG (launcher_adder),
                        launcher_adder->entry);

  G_OBJECT_CLASS (mdk_launcher_adder_parent_class)->constructed (object);
}

static void
mdk_launcher_adder_class_init (MdkLauncherAdderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = mdk_launcher_adder_dispose;
  object_class->set_property = mdk_launcher_adder_set_property;
  object_class->get_property = mdk_launcher_adder_get_property;
  object_class->constructed = mdk_launcher_adder_constructed;

  obj_props[PROP_CONTEXT] = g_param_spec_object ("context", NULL, NULL,
                                                 MDK_TYPE_CONTEXT,
                                                 G_PARAM_CONSTRUCT_ONLY |
                                                 G_PARAM_READWRITE |
                                                 G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/ui/mdk-launcher-adder.ui");
  gtk_widget_class_bind_template_child (widget_class, MdkLauncherAdder,
                                        entry);
  gtk_widget_class_bind_template_child (widget_class, MdkLauncherAdder,
                                        type_combo_row);
  gtk_widget_class_bind_template_child (widget_class, MdkLauncherAdder,
                                        add_button);
}

static void
mdk_launcher_adder_init (MdkLauncherAdder *launcher_adder)
{
  gtk_widget_init_template (GTK_WIDGET (launcher_adder));
}
