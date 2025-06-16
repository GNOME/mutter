/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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

/**
 * MetaLogicalMonitor:
 *
 * An abstraction for a monitor(set) and its configuration.
 *
 * A logical monitor is a group of one or more physical monitors that
 * must behave and be treated as single one. This happens, for example,
 * when 2 monitors are mirrored. Each physical monitor is represented
 * by a [class@Meta.Monitor].
 *
 * #MetaLogicalMonitor has a single viewport, with its owns transformations
 * (such as scaling), that are applied to all the [class@Meta.Monitor]s that
 * are grouped by it.
 *
 * #MetaLogicalMonitor provides an abstraction that makes it easy to handle
 * the specifics of setting up different [class@Meta.Monitor]s. It then can
 * be used more easily by #MetaRendererView.
 */

#include "config.h"

#include "backends/meta-logical-monitor-private.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-output.h"

typedef struct _MetaLogicalMonitorPrivate
{
  MetaMonitorManager *monitor_manager;

  MetaLogicalMonitorId *id;
} MetaLogicalMonitorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaLogicalMonitor,
                            meta_logical_monitor,
                            G_TYPE_OBJECT)

typedef struct
{
  MetaMonitorManager *monitor_manager;
  MetaLogicalMonitor *logical_monitor;
} AddMonitorFromConfigData;

static void
add_monitor_from_config (MetaMonitorConfig        *monitor_config,
                         AddMonitorFromConfigData *data)
{
  MetaMonitorSpec *monitor_spec;
  MetaMonitor *monitor;

  monitor_spec = monitor_config->monitor_spec;
  monitor = meta_monitor_manager_get_monitor_from_spec (data->monitor_manager,
                                                        monitor_spec);

  meta_logical_monitor_add_monitor (data->logical_monitor, monitor);
}

MetaLogicalMonitor *
meta_logical_monitor_new (MetaMonitorManager       *monitor_manager,
                          MetaLogicalMonitorConfig *logical_monitor_config,
                          int                       monitor_number)
{
  MetaLogicalMonitor *logical_monitor;
  MetaLogicalMonitorPrivate *priv;
  GList *monitor_configs;

  logical_monitor = g_object_new (META_TYPE_LOGICAL_MONITOR, NULL);
  priv = meta_logical_monitor_get_instance_private (logical_monitor);

  priv->monitor_manager = monitor_manager;

  monitor_configs = logical_monitor_config->monitor_configs;

  logical_monitor->number = monitor_number;
  logical_monitor->scale = logical_monitor_config->scale;
  logical_monitor->transform = logical_monitor_config->transform;
  logical_monitor->in_fullscreen = -1;
  logical_monitor->rect = logical_monitor_config->layout;

  logical_monitor->is_presentation = TRUE;
  g_list_foreach (monitor_configs, (GFunc) add_monitor_from_config,
                  &(AddMonitorFromConfigData) {
                    .monitor_manager = monitor_manager,
                    .logical_monitor = logical_monitor
                  });

  return logical_monitor;
}

static MtkMonitorTransform
derive_monitor_transform (MetaMonitor *monitor)
{
  MetaOutput *main_output;
  MetaCrtc *crtc;
  const MetaCrtcConfig *crtc_config;
  MtkMonitorTransform transform;

  main_output = meta_monitor_get_main_output (monitor);
  crtc = meta_output_get_assigned_crtc (main_output);
  crtc_config = meta_crtc_get_config (crtc);
  transform = crtc_config->transform;

  return meta_monitor_crtc_to_logical_transform (monitor, transform);
}

MetaLogicalMonitor *
meta_logical_monitor_new_derived (MetaMonitorManager *monitor_manager,
                                  MetaMonitor        *monitor,
                                  MtkRectangle        layout,
                                  float               scale,
                                  int                 monitor_number)
{
  MetaLogicalMonitor *logical_monitor;
  MetaLogicalMonitorPrivate *priv;
  MtkMonitorTransform transform;

  logical_monitor = g_object_new (META_TYPE_LOGICAL_MONITOR, NULL);
  priv = meta_logical_monitor_get_instance_private (logical_monitor);

  priv->monitor_manager = monitor_manager;

  transform = derive_monitor_transform (monitor);

  logical_monitor->number = monitor_number;
  logical_monitor->scale = scale;
  logical_monitor->transform = transform;
  logical_monitor->in_fullscreen = -1;
  logical_monitor->rect = layout;

  logical_monitor->is_presentation = TRUE;
  meta_logical_monitor_add_monitor (logical_monitor, monitor);

  return logical_monitor;
}

static MetaLogicalMonitorId *
generate_id (MetaLogicalMonitor *logical_monitor)
{
  MetaMonitor *monitor = g_list_first (logical_monitor->monitors)->data;
  MetaMonitorSpec *spec = meta_monitor_get_spec (monitor);

  if (g_strcmp0 (spec->vendor, "unknown") == 0 ||
      g_strcmp0 (spec->product, "unknown") == 0 ||
      g_strcmp0 (spec->serial, "unknown") == 0)
    {
      return (MetaLogicalMonitorId *) g_strdup_printf ("CONNECTOR:%s",
                                                       spec->connector);
    }
  else
    {
      return (MetaLogicalMonitorId *) g_strdup_printf ("EDID:%s:%s:%s",
                                                       spec->vendor,
                                                       spec->product,
                                                       spec->serial);
    }
}

void
meta_logical_monitor_add_monitor (MetaLogicalMonitor *logical_monitor,
                                  MetaMonitor        *monitor)
{
  MetaLogicalMonitorPrivate *priv =
    meta_logical_monitor_get_instance_private (logical_monitor);
  GList *l;
  gboolean is_presentation;

  is_presentation = logical_monitor->is_presentation;
  logical_monitor->monitors = g_list_append (logical_monitor->monitors,
                                             g_object_ref (monitor));

  for (l = logical_monitor->monitors; l; l = l->next)
    {
      MetaMonitor *other_monitor = l->data;
      GList *outputs;
      GList *l_output;

      outputs = meta_monitor_get_outputs (other_monitor);
      for (l_output = outputs; l_output; l_output = l_output->next)
        {
          MetaOutput *output = l_output->data;

          is_presentation = (is_presentation &&
                             meta_output_is_presentation (output));
        }
    }

  logical_monitor->is_presentation = is_presentation;

  if (!priv->id)
    priv->id = generate_id (logical_monitor);

  meta_monitor_set_logical_monitor (monitor, logical_monitor);
}

gboolean
meta_logical_monitor_is_primary (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->is_primary;
}

void
meta_logical_monitor_make_primary (MetaLogicalMonitor *logical_monitor)
{
  logical_monitor->is_primary = TRUE;
}

float
meta_logical_monitor_get_scale (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->scale;
}

MtkMonitorTransform
meta_logical_monitor_get_transform (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->transform;
}

MtkRectangle
meta_logical_monitor_get_layout (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->rect;
}

/**
 * meta_logical_monitor_get_number:
 * @logical_monitor: A #MetaLogicalMonitor
 *
 * Returns the [class@Meta.Monitor]s number which is compatible with the monitor
 * API on [class@Meta.Display] until the next monitors-changed.
 *
 * Returns: The [class@Meta.Monitor]s number
 */
int
meta_logical_monitor_get_number (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->number;
}

/**
 * meta_logical_monitor_get_monitors:
 * @logical_monitor: A #MetaLogicalMonitor
 *
 * Returns the list of [class@Meta.Monitor]s.
 *
 * Returns: (transfer none) (element-type Meta.Monitor):
 * The list of [class@Meta.Monitor]s.
 */
GList *
meta_logical_monitor_get_monitors (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->monitors;
}

typedef struct _ForeachCrtcData
{
  MetaLogicalMonitor *logical_monitor;
  MetaLogicalMonitorCrtcFunc func;
  gpointer user_data;
} ForeachCrtcData;

static gboolean
foreach_crtc (MetaMonitor         *monitor,
              MetaMonitorMode     *mode,
              MetaMonitorCrtcMode *monitor_crtc_mode,
              gpointer             user_data,
              GError             **error)
{
  ForeachCrtcData *data = user_data;

  data->func (data->logical_monitor,
              monitor,
              monitor_crtc_mode->output,
              meta_output_get_assigned_crtc (monitor_crtc_mode->output),
              data->user_data);

  return TRUE;
}

void
meta_logical_monitor_foreach_crtc (MetaLogicalMonitor        *logical_monitor,
                                   MetaLogicalMonitorCrtcFunc func,
                                   gpointer                   user_data)
{
  GList *l;

  for (l = logical_monitor->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorMode *mode;
      ForeachCrtcData data = {
        .logical_monitor = logical_monitor,
        .func = func,
        .user_data = user_data
      };

      mode = meta_monitor_get_current_mode (monitor);
      meta_monitor_mode_foreach_crtc (monitor, mode, foreach_crtc, &data, NULL);
    }
}

static void
meta_logical_monitor_init (MetaLogicalMonitor *logical_monitor)
{
}

static void
meta_logical_monitor_dispose (GObject *object)
{
  MetaLogicalMonitor *logical_monitor = META_LOGICAL_MONITOR (object);

  g_clear_list (&logical_monitor->monitors, g_object_unref);

  G_OBJECT_CLASS (meta_logical_monitor_parent_class)->dispose (object);
}

static void
meta_logical_monitor_finalize (GObject *object)
{
  MetaLogicalMonitor *logical_monitor = META_LOGICAL_MONITOR (object);
  MetaLogicalMonitorPrivate *priv =
    meta_logical_monitor_get_instance_private (logical_monitor);

  g_clear_pointer (&priv->id, meta_logical_monitor_id_free);

  G_OBJECT_CLASS (meta_logical_monitor_parent_class)->finalize (object);
}

static void
meta_logical_monitor_class_init (MetaLogicalMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_logical_monitor_dispose;
  object_class->finalize = meta_logical_monitor_finalize;
}

gboolean
meta_logical_monitor_has_neighbor (MetaLogicalMonitor   *logical_monitor,
                                   MetaLogicalMonitor   *neighbor,
                                   MetaDisplayDirection  neighbor_direction)
{
  switch (neighbor_direction)
    {
    case META_DISPLAY_RIGHT:
      if (neighbor->rect.x == (logical_monitor->rect.x +
                               logical_monitor->rect.width) &&
          mtk_rectangle_vert_overlap (&neighbor->rect,
                                      &logical_monitor->rect))
        return TRUE;
      break;
    case META_DISPLAY_LEFT:
      if (logical_monitor->rect.x == (neighbor->rect.x +
                                      neighbor->rect.width) &&
          mtk_rectangle_vert_overlap (&neighbor->rect,
                                      &logical_monitor->rect))
        return TRUE;
      break;
    case META_DISPLAY_UP:
      if (logical_monitor->rect.y == (neighbor->rect.y +
                                      neighbor->rect.height) &&
          mtk_rectangle_horiz_overlap (&neighbor->rect,
                                       &logical_monitor->rect))
        return TRUE;
      break;
    case META_DISPLAY_DOWN:
      if (neighbor->rect.y == (logical_monitor->rect.y +
                               logical_monitor->rect.height) &&
          mtk_rectangle_horiz_overlap (&neighbor->rect,
                                       &logical_monitor->rect))
        return TRUE;
      break;
    }

  return FALSE;
}

void
meta_logical_monitor_id_free (MetaLogicalMonitorId *id)
{
  g_free (id);
}

MetaLogicalMonitorId *
meta_logical_monitor_id_dup (const MetaLogicalMonitorId *id)
{
  return (MetaLogicalMonitorId *) g_strdup ((char *) id);
}

gboolean
meta_logical_monitor_id_equal (const MetaLogicalMonitorId *id,
                               const MetaLogicalMonitorId *other_id)
{
  return g_str_equal ((const char *) id, (const char *) other_id);
}

const MetaLogicalMonitorId *
meta_logical_monitor_get_id (MetaLogicalMonitor *logical_monitor)
{
  MetaLogicalMonitorPrivate *priv =
    meta_logical_monitor_get_instance_private (logical_monitor);

  return priv->id;
}

MetaLogicalMonitorId *
meta_logical_monitor_dup_id (MetaLogicalMonitor *logical_monitor)
{
  MetaLogicalMonitorPrivate *priv =
    meta_logical_monitor_get_instance_private (logical_monitor);

  return meta_logical_monitor_id_dup (priv->id);
}

static int
monitor_config_spec_compare (gconstpointer a,
                             gconstpointer b)
{
  const MetaMonitorConfig *monitor_config = a;
  const MetaMonitorSpec *monitor_spec = b;

  return meta_monitor_spec_compare (monitor_config->monitor_spec, monitor_spec);
}

MetaMonitorManager *
meta_logical_monitor_get_monitor_manager (MetaLogicalMonitor *logical_monitor)
{
  MetaLogicalMonitorPrivate *priv =
    meta_logical_monitor_get_instance_private (logical_monitor);

  return priv->monitor_manager;
}

gboolean
meta_logical_monitor_update (MetaLogicalMonitor       *logical_monitor,
                             MetaLogicalMonitorConfig *logical_monitor_config,
                             int                       number)
{
  GList *l;

  if (logical_monitor->number != number)
    return FALSE;

  if (!mtk_rectangle_equal (&logical_monitor->rect,
                            &logical_monitor_config->layout))
    return FALSE;

  if (logical_monitor->transform != logical_monitor_config->transform)
    return FALSE;

  if (logical_monitor->scale != logical_monitor_config->scale)
    return FALSE;

  if (g_list_length (logical_monitor->monitors) !=
      g_list_length (logical_monitor_config->monitor_configs))
    return FALSE;

  for (l = logical_monitor->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = META_MONITOR (l->data);

      if (!g_list_find_custom (logical_monitor_config->monitor_configs,
                               meta_monitor_get_spec (monitor),
                               (GCompareFunc) monitor_config_spec_compare))
        return FALSE;
    }

  g_list_foreach (logical_monitor->monitors,
                  (GFunc) meta_monitor_set_logical_monitor,
                  logical_monitor);

  return TRUE;
}

gboolean
meta_logical_monitor_update_derived (MetaLogicalMonitor *logical_monitor,
                                     int                 number,
                                     float               global_scale)
{
  MetaLogicalMonitorPrivate *priv =
    meta_logical_monitor_get_instance_private (logical_monitor);
  MetaMonitorManager *monitor_manager = priv->monitor_manager;
  g_autolist (MetaMonitor) new_monitors = NULL;
  GList *l;
  MtkMonitorTransform transform;

  if (logical_monitor->number != number)
    return FALSE;

  if (logical_monitor->scale != global_scale)
    return FALSE;

  for (l = logical_monitor->monitors; l; l = l->next)
    {
      MetaMonitor *old_monitor = META_MONITOR (l->data);
      MetaMonitorSpec *old_monitor_spec = meta_monitor_get_spec (old_monitor);
      MetaMonitor *monitor;
      MtkRectangle layout;

      monitor = meta_monitor_manager_get_monitor_from_spec (monitor_manager,
                                                            old_monitor_spec);
      if (!monitor)
        return FALSE;

      meta_monitor_derive_layout (monitor, &layout);
      if (!mtk_rectangle_equal (&logical_monitor->rect, &layout))
        return FALSE;

      transform = derive_monitor_transform (monitor);
      if (logical_monitor->transform != transform)
        return FALSE;

      new_monitors = g_list_append (new_monitors, g_object_ref (monitor));
    }

  g_clear_list (&logical_monitor->monitors, g_object_unref);
  logical_monitor->monitors = g_steal_pointer (&new_monitors);

  g_list_foreach (logical_monitor->monitors,
                  (GFunc) meta_monitor_set_logical_monitor,
                  logical_monitor);

  return TRUE;
}
