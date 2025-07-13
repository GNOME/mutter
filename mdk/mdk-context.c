/*
 * Copyright (C) 2021-2024 Red Hat Inc.
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

#include "mdk-context.h"

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <stdio.h>

#include "mdk-pipewire.h"
#include "mdk-seat.h"
#include "mdk-session.h"

enum
{
  PROP_0,

  PROP_EMULATE_TOUCH,
  PROP_INHIBIT_SYSTEM_SHORTCUTS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  READY,
  ERROR,
  CLOSED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MdkContext
{
  GObject parent;

  MdkPipewire *pipewire;
  MdkSession *session;

  gboolean emulate_touch;
  gboolean inhibit_system_shortcuts;
};

G_DEFINE_FINAL_TYPE (MdkContext, mdk_context, G_TYPE_OBJECT)

static void
update_active_input_devices (MdkContext *context)
{
  MdkSession *session;
  MdkSeat *seat;

  session = context->session;
  if (!session)
    return;

  seat = mdk_session_get_default_seat (session);

  if (context->emulate_touch)
    {
      mdk_seat_bind_touch (seat);
      mdk_seat_unbind_pointer (seat);
      mdk_seat_unbind_keyboard (seat);
    }
  else
    {
      mdk_seat_unbind_touch (seat);
      mdk_seat_bind_pointer (seat);
      mdk_seat_bind_keyboard (seat);
    }
}

static void
mdk_context_set_emulate_touch (MdkContext *context,
                               gboolean    emulate_touch)
{
  if (context->emulate_touch == emulate_touch)
    return;

  context->emulate_touch = emulate_touch;

  update_active_input_devices (context);
}

static void
mdk_context_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MdkContext *context = MDK_CONTEXT (object);

  switch (prop_id)
    {
    case PROP_EMULATE_TOUCH:
      mdk_context_set_emulate_touch (context, g_value_get_boolean (value));
      break;
    case PROP_INHIBIT_SYSTEM_SHORTCUTS:
      context->inhibit_system_shortcuts = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_context_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  MdkContext *context = MDK_CONTEXT (object);

  switch (prop_id)
    {
    case PROP_EMULATE_TOUCH:
      g_value_set_boolean (value, context->emulate_touch);
      break;
    case PROP_INHIBIT_SYSTEM_SHORTCUTS:
      g_value_set_boolean (value, context->inhibit_system_shortcuts);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_context_finalize (GObject *object)
{
  MdkContext *context = MDK_CONTEXT (object);

  g_clear_object (&context->session);

  G_OBJECT_CLASS (mdk_context_parent_class)->finalize (object);
}

static void
mdk_context_class_init (MdkContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = mdk_context_set_property;
  object_class->get_property = mdk_context_get_property;
  object_class->finalize = mdk_context_finalize;

  obj_props[PROP_EMULATE_TOUCH] =
    g_param_spec_boolean ("emulate-touch", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);
  obj_props[PROP_INHIBIT_SYSTEM_SHORTCUTS] =
    g_param_spec_boolean ("inhibit-system-shortcuts", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[READY] = g_signal_new ("ready",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 0);
  signals[ERROR] = g_signal_new ("error",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 1,
                                 G_TYPE_ERROR);
  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}

static void
mdk_context_init (MdkContext *context)
{
}

static void
on_session_closed (MdkSession *session,
                   MdkContext *context)
{
  g_signal_emit (context, signals[CLOSED], 0);
}

static void
init_session (MdkContext *context)
{
  g_autoptr (GError) error = NULL;
  MdkSession *session;

  session = g_initable_new (MDK_TYPE_SESSION,
                            NULL, &error,
                            "context", context,
                            NULL);
  if (!session)
    {
      g_signal_emit (context, signals[ERROR], 0, error);
      return;
    }

  g_debug ("Session is ready");

  context->session = session;
  update_active_input_devices (context);

  g_signal_connect (context->session, "closed",
                    G_CALLBACK (on_session_closed), context);

  g_signal_emit (context, signals[READY], 0);
}

MdkContext *
mdk_context_new (void)
{
  MdkContext *context;

  context = g_object_new (MDK_TYPE_CONTEXT, NULL);

  return context;
}

void
mdk_context_activate (MdkContext *context)
{
  g_autoptr (GError) error = NULL;

  context->pipewire = mdk_pipewire_new (context, &error);
  if (!context->pipewire)
    {
      g_signal_emit (context, signals[ERROR], 0, error);
      return;
    }

  init_session (context);
}

MdkSession *
mdk_context_get_session (MdkContext *context)
{
  return context->session;
}

MdkPipewire *
mdk_context_get_pipewire (MdkContext *context)
{
  return context->pipewire;
}

gboolean
mdk_context_get_emulate_touch (MdkContext *context)
{
  return context->emulate_touch;
}

gboolean
mdk_context_get_inhibit_system_shortcuts (MdkContext *context)
{
  return context->inhibit_system_shortcuts;
}
