/*
 * Copyright (C) 2022 Red Hat Inc
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include "meta/window.h"

#include <string.h>
#include <X11/Xutil.h>
#include <X11/extensions/sync.h>

typedef struct
{
  MetaWindow *window;
  Window xwindow;
  /* XSync update counter */
  XSyncCounter sync_request_counter;
  int64_t sync_request_serial;
  int64_t sync_request_wait_serial;
  guint sync_request_timeout_id;
  /* alarm monitoring client's _NET_WM_SYNC_REQUEST_COUNTER */
  XSyncAlarm sync_request_alarm;

  int64_t frame_drawn_time;
  GList *frames;

  /* if TRUE, the we have the new form of sync request counter which
   * also handles application frames */
  guint extended_sync_request_counter : 1;
  guint disabled : 1;
  /* If set, the client needs to be sent a _NET_WM_FRAME_DRAWN
   * client message for one or more messages in ->frames */
  guint needs_frame_drawn : 1;
} MetaSyncCounter;

void meta_sync_counter_init (MetaSyncCounter *sync_counter,
                             MetaWindow      *window,
                             Window           xwindow);

void meta_sync_counter_clear (MetaSyncCounter *sync_counter);

void meta_sync_counter_set_counter (MetaSyncCounter *sync_counter,
                                    XSyncCounter     counter,
                                    gboolean         extended);

void meta_sync_counter_create_sync_alarm (MetaSyncCounter *sync_counter);

void meta_sync_counter_destroy_sync_alarm (MetaSyncCounter *sync_counter);

gboolean meta_sync_counter_has_sync_alarm (MetaSyncCounter *sync_counter);

void meta_sync_counter_send_request (MetaSyncCounter *sync_counter);

void meta_sync_counter_update (MetaSyncCounter *sync_counter,
                               int64_t          new_counter_value);

gboolean meta_sync_counter_is_waiting (MetaSyncCounter *sync_counter);

gboolean meta_sync_counter_is_waiting_response (MetaSyncCounter *sync_counter);

void meta_sync_counter_queue_frame_drawn (MetaSyncCounter *sync_counter);

void meta_sync_counter_assign_counter_to_frames (MetaSyncCounter *sync_counter,
                                                 int64_t          counter);

void meta_sync_counter_complete_frame (MetaSyncCounter  *sync_counter,
                                       ClutterFrameInfo *frame_info,
                                       int64_t           presentation_time);

void meta_sync_counter_finish_incomplete (MetaSyncCounter *sync_counter);

void meta_sync_counter_send_frame_drawn (MetaSyncCounter *sync_counter);
