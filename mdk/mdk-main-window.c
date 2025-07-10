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

#include "mdk-main-window.h"

#include "mdk-context.h"
#include "mdk-launcher.h"
#include "mdk-window.h"

struct _MdkMainWindow
{
  MdkWindow parent;

  GMenu *launchers;
};

G_DEFINE_TYPE (MdkMainWindow, mdk_main_window, MDK_TYPE_WINDOW)

static void
update_launchers_menu (MdkMainWindow *main_window)
{
  MdkContext *context = mdk_window_get_context (MDK_WINDOW (main_window));
  GPtrArray *launchers = mdk_context_get_launchers (context);
  size_t i;

  g_menu_remove_all (main_window->launchers);
  for (i = 0; i < launchers->len; i++)
    {
      MdkLauncher *launcher = g_ptr_array_index (launchers, i);
      g_autofree char *action = NULL;

      action = mdk_launcher_get_action (launcher);
      g_menu_append (main_window->launchers,
                     mdk_launcher_get_name (launcher),
                     action);
    }
}

static void
mdk_main_window_constructed (GObject *object)
{
  MdkMainWindow *main_window = MDK_MAIN_WINDOW (object);
  MdkContext *context = mdk_window_get_context (MDK_WINDOW (object));

  g_signal_connect_swapped (context, "launchers-changed",
                            G_CALLBACK (update_launchers_menu), main_window);
  update_launchers_menu (main_window);

  G_OBJECT_CLASS (mdk_main_window_parent_class)->constructed (object);
}

static void
mdk_main_window_dispose (GObject *object)
{
  gtk_widget_dispose_template (GTK_WIDGET (object), MDK_TYPE_MAIN_WINDOW);

  G_OBJECT_CLASS (mdk_main_window_parent_class)->dispose (object);
}

static void
mdk_main_window_class_init (MdkMainWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = mdk_main_window_constructed;
  object_class->dispose = mdk_main_window_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/ui/mdk-main-window.ui");
  gtk_widget_class_bind_template_child (widget_class, MdkMainWindow,
                                        launchers);
}

static void
mdk_main_window_init (MdkMainWindow *main_window)
{
  gtk_widget_init_template (GTK_WIDGET (main_window));
}
