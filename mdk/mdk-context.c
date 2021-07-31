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
#include <stdio.h>

#include "mdk-pipewire.h"
#include "mdk-session.h"

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
};

G_DEFINE_FINAL_TYPE (MdkContext, mdk_context, G_TYPE_OBJECT)

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

  object_class->finalize = mdk_context_finalize;

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
session_ready_cb (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  MdkContext *context = user_data;
  g_autoptr (GError) error = NULL;

  if (!g_async_initable_init_finish (G_ASYNC_INITABLE (source_object),
                                     res,
                                     &error))
    {
      g_signal_emit (context, signals[ERROR], 0, error);
      return;
    }

  g_debug ("Session is ready");

  context->session = MDK_SESSION (source_object);

  g_signal_connect (context->session, "closed",
                    G_CALLBACK (on_session_closed), context);

  g_signal_emit (context, signals[READY], 0);
}

static void
init_session (MdkContext *context)
{
  g_async_initable_new_async (MDK_TYPE_SESSION,
                              G_PRIORITY_DEFAULT,
                              NULL,
                              session_ready_cb,
                              context,
                              "context", context,
                              NULL);
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
