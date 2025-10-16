/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2018 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "meta-startup-notification-x11.h"

#include <gio/gdesktopappinfo.h>

#include "core/display-private.h"
#include "core/startup-notification-private.h"
#include "mtk/mtk-x11.h"
#include "x11/meta-x11-display-private.h"

#ifdef HAVE_STARTUP_NOTIFICATION

#include <libsn/sn.h>

enum
{
  PROP_SEQ_X11_0,
  PROP_SEQ_X11_SEQ,
  N_SEQ_X11_PROPS
};

struct _MetaStartupSequenceX11
{
  MetaStartupSequence parent_instance;
  SnStartupSequence *seq;
};

struct _MetaX11StartupNotification
{
  SnDisplay *sn_display;
  SnMonitorContext *sn_context;
};

static GParamSpec *seq_x11_props[N_SEQ_X11_PROPS];

static GList *displays;

G_DEFINE_TYPE (MetaStartupSequenceX11,
               meta_startup_sequence_x11,
               META_TYPE_STARTUP_SEQUENCE)

static void
meta_startup_sequence_x11_complete (MetaStartupSequence *seq)
{
  MetaStartupSequenceX11 *seq_x11;

  seq_x11 = META_STARTUP_SEQUENCE_X11 (seq);
  sn_startup_sequence_complete (seq_x11->seq);
}

static void
meta_startup_sequence_x11_finalize (GObject *object)
{
  MetaStartupSequenceX11 *seq_x11;

  seq_x11 = META_STARTUP_SEQUENCE_X11 (object);
  sn_startup_sequence_unref (seq_x11->seq);

  G_OBJECT_CLASS (meta_startup_sequence_x11_parent_class)->finalize (object);
}

static void
meta_startup_sequence_x11_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  MetaStartupSequenceX11 *seq_x11;

  seq_x11 = META_STARTUP_SEQUENCE_X11 (object);

  switch (prop_id)
    {
    case PROP_SEQ_X11_SEQ:
      seq_x11->seq = g_value_get_pointer (value);
      sn_startup_sequence_ref (seq_x11->seq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_startup_sequence_x11_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  MetaStartupSequenceX11 *seq_x11;

  seq_x11 = META_STARTUP_SEQUENCE_X11 (object);

  switch (prop_id)
    {
    case PROP_SEQ_X11_SEQ:
      g_value_set_pointer (value, seq_x11->seq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_startup_sequence_x11_init (MetaStartupSequenceX11 *seq)
{
}

static void
meta_startup_sequence_x11_class_init (MetaStartupSequenceX11Class *klass)
{
  MetaStartupSequenceClass *seq_class;
  GObjectClass *object_class;

  seq_class = META_STARTUP_SEQUENCE_CLASS (klass);
  seq_class->complete = meta_startup_sequence_x11_complete;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = meta_startup_sequence_x11_finalize;
  object_class->set_property = meta_startup_sequence_x11_set_property;
  object_class->get_property = meta_startup_sequence_x11_get_property;

  seq_x11_props[PROP_SEQ_X11_SEQ] =
    g_param_spec_pointer ("seq", NULL, NULL,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_SEQ_X11_PROPS,
                                     seq_x11_props);
}

static MetaStartupSequence *
meta_startup_sequence_x11_new (MetaDisplay       *display,
                               SnStartupSequence *seq)
{
  gint64 timestamp;

  timestamp = sn_startup_sequence_get_timestamp (seq);
  return g_object_new (META_TYPE_STARTUP_SEQUENCE_X11,
                       "display", display,
                       "id", sn_startup_sequence_get_id (seq),
                       "icon-name", sn_startup_sequence_get_icon_name (seq),
                       "application-id", sn_startup_sequence_get_application_id (seq),
                       "wmclass", sn_startup_sequence_get_wmclass (seq),
                       "name", sn_startup_sequence_get_name (seq),
                       "workspace", sn_startup_sequence_get_workspace (seq),
                       "timestamp", timestamp,
                       "seq", seq,
                       NULL);
}

static MetaDisplay *
find_display (Display *xdisplay)
{
  GList *l;

  for (l = displays; l; l = l->next)
    {
      MetaDisplay *display = l->data;
      MetaX11Display *x11_display;

      x11_display = display->x11_display;
      if (!x11_display)
        continue;

      if (x11_display->xdisplay != xdisplay)
        continue;

      return display;
    }

  return NULL;
}

static void
sn_error_trap_push (SnDisplay *sn_display,
                    Display   *xdisplay)
{
  MetaDisplay *display;

  display = find_display (xdisplay);
  if (display != NULL)
    mtk_x11_error_trap_push (display->x11_display->xdisplay);
}

static void
sn_error_trap_pop (SnDisplay *sn_display,
                   Display   *xdisplay)
{
  MetaDisplay *display;

  display = find_display (xdisplay);
  if (display != NULL)
    mtk_x11_error_trap_pop (display->x11_display->xdisplay);
}

static void
meta_startup_notification_sn_event (SnMonitorEvent *event,
                                    void           *user_data)
{
  MetaX11Display *x11_display = user_data;
  MetaDisplay *display = meta_x11_display_get_display (x11_display);
  MetaStartupNotification *sn = display->startup_notification;
  MetaStartupSequence *seq;
  SnStartupSequence *sequence;

  sequence = sn_monitor_event_get_startup_sequence (event);

  sn_startup_sequence_ref (sequence);

  switch (sn_monitor_event_get_type (event))
    {
    case SN_MONITOR_EVENT_INITIATED:
      {
        const char *wmclass;

        wmclass = sn_startup_sequence_get_wmclass (sequence);

        meta_topic (META_DEBUG_STARTUP,
                    "Received startup initiated for %s wmclass %s",
                    sn_startup_sequence_get_id (sequence),
                    wmclass ? wmclass : "(unset)");

        seq = meta_startup_sequence_x11_new (display, sequence);
        meta_startup_notification_add_sequence (sn, seq);
        g_object_unref (seq);
      }
      break;

    case SN_MONITOR_EVENT_COMPLETED:
      {
        meta_topic (META_DEBUG_STARTUP,
                    "Received startup completed for %s",
                    sn_startup_sequence_get_id (sequence));

        seq = meta_startup_notification_lookup_sequence (sn, sn_startup_sequence_get_id (sequence));
        if (seq)
          {
            meta_startup_sequence_complete (seq);
            meta_startup_notification_remove_sequence (sn, seq);
          }
      }
      break;

    case SN_MONITOR_EVENT_CHANGED:
      meta_topic (META_DEBUG_STARTUP,
                  "Received startup changed for %s",
                  sn_startup_sequence_get_id (sequence));
      break;

    case SN_MONITOR_EVENT_CANCELED:
      meta_topic (META_DEBUG_STARTUP,
                  "Received startup canceled for %s",
                  sn_startup_sequence_get_id (sequence));
      break;
    }

  sn_startup_sequence_unref (sequence);
}

static void
on_x11_display_closing (MetaDisplay *display)
{
  g_signal_handlers_disconnect_by_func (display, on_x11_display_closing, NULL);
  displays = g_list_remove (displays, display);
}
#endif /* HAVE_STARTUP_NOTIFICATION */

void
meta_x11_startup_notification_init (MetaX11Display *x11_display)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  MetaX11StartupNotification *x11_sn;
  MetaDisplay *display;

  x11_sn = g_new0 (MetaX11StartupNotification, 1);
  x11_sn->sn_display = sn_display_new (x11_display->xdisplay,
                                       sn_error_trap_push,
                                       sn_error_trap_pop);
  x11_sn->sn_context =
    sn_monitor_context_new (x11_sn->sn_display,
                            meta_x11_display_get_screen_number (x11_display),
                            meta_startup_notification_sn_event,
                            x11_display,
                            NULL);

  x11_display->startup_notification = x11_sn;

  display = meta_x11_display_get_display (x11_display);
  if (!g_list_find (displays, display))
    {
      displays = g_list_prepend (displays, display);
      g_signal_connect (display, "x11-display-closing",
                        G_CALLBACK (on_x11_display_closing), NULL);
    }
#endif
}

void
meta_x11_startup_notification_release (MetaX11Display *x11_display)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  MetaX11StartupNotification *x11_sn = x11_display->startup_notification;

  x11_display->startup_notification = NULL;

  if (x11_sn)
    {
      sn_monitor_context_unref (x11_sn->sn_context);
      sn_display_unref (x11_sn->sn_display);
      g_free (x11_sn);
    }
#endif
}

gboolean
meta_x11_startup_notification_handle_xevent (MetaX11Display *x11_display,
                                             XEvent         *xevent)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  MetaX11StartupNotification *x11_sn = x11_display->startup_notification;

  if (!x11_sn)
    return FALSE;

  return sn_display_process_event (x11_sn->sn_display, xevent);
#else
  return FALSE;
#endif
}

#ifdef HAVE_STARTUP_NOTIFICATION
typedef void (* SetAppIdFunc) (SnLauncherContext *context,
                               const char        *app_id);
#endif

gchar *
meta_x11_startup_notification_launch (MetaX11Display *x11_display,
                                      GAppInfo       *app_info,
                                      uint32_t        timestamp,
                                      int             workspace)
{
  gchar *startup_id = NULL;
#ifdef HAVE_STARTUP_NOTIFICATION
  MetaX11StartupNotification *x11_sn = x11_display->startup_notification;
  SnLauncherContext *sn_launcher;
  int screen;

  screen = meta_x11_display_get_screen_number (x11_display);
  sn_launcher = sn_launcher_context_new (x11_sn->sn_display, screen);

  sn_launcher_context_set_workspace (sn_launcher, workspace);

  if (app_info)
    {
      sn_launcher_context_set_name (sn_launcher, g_app_info_get_name (app_info));
      sn_launcher_context_set_binary_name (sn_launcher,
                                           g_app_info_get_executable (app_info));
    }

  if (G_IS_DESKTOP_APP_INFO (app_info))
    {
      const char *application_id;
      static SetAppIdFunc launcher_context_set_application_id = NULL;

      application_id =
        g_desktop_app_info_get_filename (G_DESKTOP_APP_INFO (app_info));

      /* This here is a terrible workaround to bypass a libsn bug that is not
       * likely to get fixed at this point.
       * sn_launcher_context_set_application_id is correctly defined in the
       * sn-launcher.h file, but it's mistakenly called
       * sn_launcher_set_application_id in the C file.
       *
       * We look up the symbol instead, but still prefer the correctly named
       * function, if one were ever to be added.
       */
      if (application_id &&
          g_once_init_enter_pointer (&launcher_context_set_application_id))
        {
          GModule *self;
          SetAppIdFunc func = NULL;

          self = g_module_open (NULL, G_MODULE_BIND_MASK);

          if (!g_module_symbol (self, "sn_launcher_context_set_application_id",
                                (gpointer *) &func))
            {
              g_module_symbol (self, "sn_launcher_set_application_id",
                               (gpointer *) &func);
            }

          g_module_close (self);

          g_once_init_leave_pointer (&launcher_context_set_application_id,
                                     func);
        }

      if (launcher_context_set_application_id && application_id)
        launcher_context_set_application_id (sn_launcher, application_id);
    }

  sn_launcher_context_initiate (sn_launcher,
                                g_get_prgname (),
                                app_info ? g_app_info_get_name (app_info) : "",
                                timestamp);

  startup_id = g_strdup (sn_launcher_context_get_startup_id (sn_launcher));

  /* Fire and forget, we have a SnMonitor in addition */
  sn_launcher_context_unref (sn_launcher);
#endif /* HAVE_STARTUP_NOTIFICATION */

  return startup_id;
}
