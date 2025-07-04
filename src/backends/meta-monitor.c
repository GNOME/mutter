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

#include "config.h"

#include "backends/meta-monitor-private.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-gpu.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-settings-private.h"
#include "backends/meta-output.h"
#include "backends/meta-backlight-private.h"
#include "core/boxes-private.h"

#define SCALE_FACTORS_PER_INTEGER 4
#define SCALE_FACTORS_STEPS (1.0f / (float) SCALE_FACTORS_PER_INTEGER)
#define MINIMUM_SCALE_FACTOR 1.0f
#define MAXIMUM_SCALE_FACTOR 4.0f
#define MINIMUM_LOGICAL_AREA (800 * 480)
#define MAXIMUM_REFRESH_RATE_DIFF 0.001f

typedef struct _MetaMonitorModePrivate
{
  MetaMonitor *monitor;
  char *id;
  unsigned int n_crtc_modes;
  MetaMonitorModeSpec spec;
  MetaMonitorCrtcMode *crtc_modes;
} MetaMonitorModePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaMonitorMode, meta_monitor_mode, G_TYPE_OBJECT)

struct _MetaMonitorModeTiled
{
  MetaMonitorMode parent;

  gboolean is_tiled;
};

#define META_TYPE_MONITOR_MODE_TILED (meta_monitor_mode_tiled_get_type ())
G_DECLARE_FINAL_TYPE (MetaMonitorModeTiled, meta_monitor_mode_tiled,
                      META, MONITOR_MODE_TILED,
                      MetaMonitorMode)
G_DEFINE_FINAL_TYPE  (MetaMonitorModeTiled, meta_monitor_mode_tiled,
                      META_TYPE_MONITOR_MODE)

typedef struct _MetaMonitorPrivate
{
  MetaBackend *backend;

  GList *outputs;
  GList *modes;
  GHashTable *mode_ids;

  MetaMonitorMode *preferred_mode;
  MetaMonitorMode *current_mode;

  MetaMonitorSpec *spec;

  MetaLogicalMonitor *logical_monitor;

  char *display_name;

  gboolean is_for_lease;

  GList *color_modes;

  MetaBacklight *backlight;
} MetaMonitorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaMonitor, meta_monitor, G_TYPE_OBJECT)

struct _MetaMonitorNormal
{
  MetaMonitor parent;
};

G_DEFINE_TYPE (MetaMonitorNormal, meta_monitor_normal, META_TYPE_MONITOR)

struct _MetaMonitorTiled
{
  MetaMonitor parent;

  MetaMonitorManager *monitor_manager;

  uint32_t tile_group_id;

  /* The tile (0, 0) output. */
  MetaOutput *origin_output;

  /* The output enabled even when a non-tiled mode is used. */
  MetaOutput *main_output;
};

G_DEFINE_TYPE (MetaMonitorTiled, meta_monitor_tiled, META_TYPE_MONITOR)

MetaMonitorSpec *
meta_monitor_spec_clone (const MetaMonitorSpec *monitor_spec)
{
  MetaMonitorSpec *new_monitor_spec;

  new_monitor_spec = g_new0 (MetaMonitorSpec, 1);
  *new_monitor_spec = (MetaMonitorSpec) {
    .connector = g_strdup (monitor_spec->connector),
    .vendor = g_strdup (monitor_spec->vendor),
    .product = g_strdup (monitor_spec->product),
    .serial = g_strdup (monitor_spec->serial),
  };

  return new_monitor_spec;
}

guint
meta_monitor_spec_hash (gconstpointer key)
{
  const MetaMonitorSpec *monitor_spec = key;

  return (g_str_hash (monitor_spec->connector) +
          g_str_hash (monitor_spec->vendor) +
          g_str_hash (monitor_spec->product) +
          g_str_hash (monitor_spec->serial));
}

gboolean
meta_monitor_spec_equals (const MetaMonitorSpec *monitor_spec,
                          const MetaMonitorSpec *other_monitor_spec)
{
  return (g_str_equal (monitor_spec->connector, other_monitor_spec->connector) &&
          g_str_equal (monitor_spec->vendor, other_monitor_spec->vendor) &&
          g_str_equal (monitor_spec->product, other_monitor_spec->product) &&
          g_str_equal (monitor_spec->serial, other_monitor_spec->serial));
}

int
meta_monitor_spec_compare (const MetaMonitorSpec *monitor_spec_a,
                           const MetaMonitorSpec *monitor_spec_b)
{
  int ret;

  ret = strcmp (monitor_spec_a->connector, monitor_spec_b->connector);
  if (ret != 0)
    return ret;

  ret = strcmp (monitor_spec_a->vendor, monitor_spec_b->vendor);
  if (ret != 0)
    return ret;

  ret = strcmp (monitor_spec_a->product, monitor_spec_b->product);
  if (ret != 0)
    return ret;

  return strcmp (monitor_spec_a->serial, monitor_spec_b->serial);
}

void
meta_monitor_spec_free (MetaMonitorSpec *monitor_spec)
{
  g_free (monitor_spec->connector);
  g_free (monitor_spec->vendor);
  g_free (monitor_spec->product);
  g_free (monitor_spec->serial);
  g_free (monitor_spec);
}

static const MetaOutputInfo *
meta_monitor_get_main_output_info (MetaMonitor *monitor)
{
  MetaOutput *output = meta_monitor_get_main_output (monitor);

  return meta_output_get_info (output);
}

static void
meta_monitor_generate_spec (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);
  MetaMonitorSpec *monitor_spec;
  const char *vendor;
  const char *product;
  const char *serial;

  vendor = output_info->vendor;
  product = output_info->product;
  serial = output_info->serial;

  monitor_spec = g_new0 (MetaMonitorSpec, 1);
  *monitor_spec = (MetaMonitorSpec) {
    .connector = g_strdup (output_info->name),
    .vendor = g_strdup (vendor ? vendor : "unknown"),
    .product = g_strdup (product ? product : "unknown"),
    .serial = g_strdup (serial ? serial : "unknown"),
  };

  priv->spec = monitor_spec;
}

static void
meta_monitor_init_supported_color_modes (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  priv->color_modes =
    g_list_append (NULL, GINT_TO_POINTER (META_COLOR_MODE_DEFAULT));

  if ((output_info->supported_color_spaces &
       (1 << META_OUTPUT_COLORSPACE_BT2020)) &&
      (output_info->supported_hdr_eotfs &
       (1 << META_OUTPUT_HDR_METADATA_EOTF_PQ)))
    {
      priv->color_modes =
        g_list_append (priv->color_modes,
                       GINT_TO_POINTER (META_COLOR_MODE_BT2100));
    }
}

static const double known_diagonals[] = {
    12.1,
    13.3,
    15.6
};

static char *
diagonal_to_str (double d)
{
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (known_diagonals); i++)
    {
      double delta;

      delta = fabs(known_diagonals[i] - d);
      if (delta < 0.1)
        return g_strdup_printf ("%0.1lf\"", known_diagonals[i]);
    }

  return g_strdup_printf ("%d\"", (int) (d + 0.5));
}

static char *
meta_monitor_make_display_name (MetaMonitor *monitor)
{
  MetaBackend *backend = meta_monitor_get_backend (monitor);
  g_autofree char *inches = NULL;
  g_autofree char *vendor_name = NULL;
  const char *vendor = NULL;
  const char *product_name = NULL;
  int width_mm;
  int height_mm;

  meta_monitor_get_physical_dimensions (monitor, &width_mm, &height_mm);

  if (meta_monitor_is_builtin (monitor))
      return g_strdup (_("Built-in display"));

  if (width_mm > 0 && height_mm > 0)
    {
      if (!meta_monitor_has_aspect_as_size (monitor))
        {
          double d = sqrt (width_mm * width_mm +
                           height_mm * height_mm);
          inches = diagonal_to_str (d / 25.4);
        }
      else
        {
          product_name = meta_monitor_get_product (monitor);
        }
    }

  vendor = meta_monitor_get_vendor (monitor);
  if (vendor)
    {
      vendor_name = meta_backend_get_vendor_name (backend, vendor);

      if (!vendor_name)
        vendor_name = g_strdup (vendor);
    }
  else
    {
      if (inches != NULL)
        vendor_name = g_strdup (_("Unknown"));
      else
        vendor_name = g_strdup (_("Unknown Display"));
    }

  if (inches != NULL)
    {
       /**/
      return g_strdup_printf (C_("This is a monitor vendor name, followed by a "
                                 "size in inches, like 'Dell 15\"'",
                                 "%s %s"),
                              vendor_name, inches);
    }
  else if (product_name != NULL)
    {
      return g_strdup_printf (C_("This is a monitor vendor name followed by "
                                 "product/model name where size in inches "
                                 "could not be calculated, e.g. Dell U2414H",
                                 "%s %s"),
                              vendor_name, product_name);
    }
  else
    {
      return g_strdup (vendor_name);
    }
}

MetaBackend *
meta_monitor_get_backend (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->backend;
}

GList *
meta_monitor_get_outputs (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->outputs;
}

MetaOutput *
meta_monitor_get_main_output (MetaMonitor *monitor)
{
  return META_MONITOR_GET_CLASS (monitor)->get_main_output (monitor);
}

/**
 * meta_monitor_is_active:
 * @monitor: A #MetaMonitor object
 *
 * Returns whether the monitor is active.
 *
 * Returns: %TRUE if the monitor is active, %FALSE otherwise.
 */
gboolean
meta_monitor_is_active (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return !!priv->current_mode;
}

/**
 * meta_monitor_is_primary:
 * @monitor: A #MetaMonitor object
 *
 * Returns whether the monitor is the primary monitor.
 *
 * Returns: %TRUE if no monitors is the primary monitor, %FALSE otherwise.
 */
gboolean
meta_monitor_is_primary (MetaMonitor *monitor)
{
  MetaOutput *output;

  g_return_val_if_fail (META_IS_MONITOR (monitor), FALSE);

  output = meta_monitor_get_main_output (monitor);

  return meta_output_is_primary (output);
}

gboolean
meta_monitor_supports_underscanning (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  return output_info->supports_underscanning;
}

gboolean
meta_monitor_supports_color_transform (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  return output_info->supports_color_transform;
}

gboolean
meta_monitor_is_underscanning (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);

  return meta_output_is_underscanning (output);
}

gboolean
meta_monitor_get_max_bpc (MetaMonitor  *monitor,
                          unsigned int *max_bpc)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);

  return meta_output_get_max_bpc (output, max_bpc);
}

MetaOutputRGBRange
meta_monitor_get_rgb_range (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);

  return meta_output_peek_rgb_range (output);
}

/**
 * meta_monitor_is_builtin:
 * @monitor: A #MetaMonitor object
 *
 * Returns whether the monitor is a builtin monitor.
 *
 * Returns: %TRUE if no monitors is a builtin monitor, %FALSE otherwise.
 */
gboolean
meta_monitor_is_builtin (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info;

  g_return_val_if_fail (META_IS_MONITOR (monitor), FALSE);

  output_info = meta_monitor_get_main_output_info (monitor);

  return meta_output_info_is_builtin (output_info);
}

/**
 * meta_monitor_is_virtual:
 * @monitor: A #MetaMonitor object
 *
 * Returns whether the monitor is virtual.
 *
 * Returns: %TRUE if no monitors is virtual, %FALSE otherwise.
 */
gboolean
meta_monitor_is_virtual (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  return output_info->is_virtual;
}

gboolean
meta_monitor_is_same_as (MetaMonitor *monitor,
                         MetaMonitor *other_monitor)
{
  const MetaMonitorSpec *spec = meta_monitor_get_spec (monitor);
  const MetaMonitorSpec *other_spec = meta_monitor_get_spec (other_monitor);
  gboolean spec_is_unknown;
  gboolean other_spec_is_unknown;

  spec_is_unknown =
    g_strcmp0 (spec->vendor, "unknown") == 0 ||
    g_strcmp0 (spec->product, "unknown") == 0 ||
    g_strcmp0 (spec->serial, "unknown") == 0;
  other_spec_is_unknown =
    g_strcmp0 (other_spec->vendor, "unknown") == 0 ||
    g_strcmp0 (other_spec->product, "unknown") == 0 ||
    g_strcmp0 (other_spec->serial, "unknown") == 0;

  if (spec_is_unknown && other_spec_is_unknown)
    return g_strcmp0 (spec->connector, other_spec->connector) == 0;

  if (spec_is_unknown || other_spec_is_unknown)
    return FALSE;

  if (g_strcmp0 (spec->vendor, other_spec->vendor) != 0)
    return FALSE;

  if (g_strcmp0 (spec->product, other_spec->product) != 0)
    return FALSE;

  if (g_strcmp0 (spec->serial, other_spec->serial) != 0)
    return FALSE;

  return TRUE;
}

void
meta_monitor_get_current_resolution (MetaMonitor *monitor,
                                     int         *width,
                                     int         *height)
{
  MetaMonitorMode *mode = meta_monitor_get_current_mode (monitor);
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (mode);

  *width = priv->spec.width;
  *height = priv->spec.height;
}

void
meta_monitor_derive_layout (MetaMonitor  *monitor,
                            MtkRectangle *layout)
{
  META_MONITOR_GET_CLASS (monitor)->derive_layout (monitor, layout);
}

void
meta_monitor_get_physical_dimensions (MetaMonitor *monitor,
                                      int         *width_mm,
                                      int         *height_mm)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  *width_mm = output_info->width_mm;
  *height_mm = output_info->height_mm;
}

MetaSubpixelOrder
meta_monitor_get_subpixel_order (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  return output_info->subpixel_order;
}

/**
 * meta_monitor_get_connector:
 * @monitor: A #MetaMonitor object
 *
 * Get the connector name of the monitor.
 *
 * Returns: The connector name of the monitor.
 */
const char *
meta_monitor_get_connector (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info;

  g_return_val_if_fail (META_IS_MONITOR (monitor), NULL);

  output_info = meta_monitor_get_main_output_info (monitor);

  return output_info->name;
}

/**
 * meta_monitor_get_vendor:
 * @monitor: A #MetaMonitor object
 *
 * Get the vendor name of the monitor.
 *
 * Returns: The vendor name of the monitor.
 */
const char *
meta_monitor_get_vendor (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info;

  g_return_val_if_fail (META_IS_MONITOR (monitor), NULL);

  output_info = meta_monitor_get_main_output_info (monitor);

  return output_info->vendor;
}

/**
 * meta_monitor_get_product:
 * @monitor: A #MetaMonitor object
 *
 * Get the product name of the monitor.
 *
 * Returns: The product name of the monitor.
 */
const char *
meta_monitor_get_product (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info;

  g_return_val_if_fail (META_IS_MONITOR (monitor), NULL);

  output_info = meta_monitor_get_main_output_info (monitor);

  return output_info->product;
}

/**
 * meta_monitor_get_serial:
 * @monitor: A #MetaMonitor object
 *
 * Get the serial id of the monitor.
 *
 * Returns: The serial id of the monitor.
 */
const char *
meta_monitor_get_serial (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info;

  g_return_val_if_fail (META_IS_MONITOR (monitor), NULL);

  output_info = meta_monitor_get_main_output_info (monitor);

  return output_info->serial;
}

const MetaEdidInfo *
meta_monitor_get_edid_info (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  return output_info->edid_info;
}

const char *
meta_monitor_get_edid_checksum_md5 (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  return output_info->edid_checksum_md5;
}

MetaConnectorType
meta_monitor_get_connector_type (MetaMonitor *monitor)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  return output_info->connector_type;
}

MtkMonitorTransform
meta_monitor_logical_to_crtc_transform (MetaMonitor          *monitor,
                                        MtkMonitorTransform  transform)
{
  MetaOutput *output = meta_monitor_get_main_output (monitor);

  return meta_output_logical_to_crtc_transform (output, transform);
}

MtkMonitorTransform
meta_monitor_crtc_to_logical_transform (MetaMonitor         *monitor,
                                        MtkMonitorTransform  transform)
{
  MetaOutput *output = meta_monitor_get_main_output (monitor);

  return meta_output_crtc_to_logical_transform (output, transform);
}

static void
meta_monitor_dispose (GObject *object)
{
  MetaMonitor *monitor = META_MONITOR (object);
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  if (priv->outputs)
    {
      g_list_foreach (priv->outputs, (GFunc) meta_output_unset_monitor, NULL);
      g_list_free_full (priv->outputs, g_object_unref);
      priv->outputs = NULL;
    }

  g_clear_object (&priv->backlight);

  G_OBJECT_CLASS (meta_monitor_parent_class)->dispose (object);
}

static void
meta_monitor_finalize (GObject *object)
{
  MetaMonitor *monitor = META_MONITOR (object);
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  g_list_free (priv->color_modes);
  g_hash_table_destroy (priv->mode_ids);
  g_list_free_full (priv->modes, g_object_unref);
  meta_monitor_spec_free (priv->spec);
  g_free (priv->display_name);

  G_OBJECT_CLASS (meta_monitor_parent_class)->finalize (object);
}

static void
meta_monitor_init (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  priv->mode_ids = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
meta_monitor_class_init (MetaMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_monitor_dispose;
  object_class->finalize = meta_monitor_finalize;
}

static char *
generate_mode_id (MetaMonitorModeSpec *monitor_mode_spec)
{
  gboolean is_interlaced;
  char rate_str[G_ASCII_DTOSTR_BUF_SIZE];
  gboolean is_vrr;

  is_interlaced = !!(monitor_mode_spec->flags & META_CRTC_MODE_FLAG_INTERLACE);
  g_ascii_formatd (rate_str, sizeof (rate_str),
                   "%.3f", monitor_mode_spec->refresh_rate);

  is_vrr = monitor_mode_spec->refresh_rate_mode ==
           META_CRTC_REFRESH_RATE_MODE_VARIABLE;

  return g_strdup_printf ("%dx%d%s@%s%s",
                          monitor_mode_spec->width,
                          monitor_mode_spec->height,
                          is_interlaced ? "i" : "",
                          rate_str,
                          is_vrr ? "+vrr" : "");
}

static gboolean
meta_monitor_add_mode (MetaMonitor     *monitor,
                       MetaMonitorMode *monitor_mode,
                       gboolean         replace)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  MetaMonitorModePrivate *mode_priv =
    meta_monitor_mode_get_instance_private (monitor_mode);
  g_autoptr (MetaMonitorMode) existing_mode = NULL;

  existing_mode = g_hash_table_lookup (priv->mode_ids,
                                       meta_monitor_mode_get_id (monitor_mode));
  if (existing_mode && !replace)
    {
      g_steal_pointer (&existing_mode);
      return FALSE;
    }

  if (existing_mode)
    priv->modes = g_list_remove (priv->modes, existing_mode);

  priv->modes = g_list_append (priv->modes, g_object_ref (monitor_mode));
  g_hash_table_replace (priv->mode_ids, mode_priv->id, monitor_mode);

  return TRUE;
}

static MetaMonitorModeSpec
meta_monitor_create_spec (MetaMonitor  *monitor,
                          int           width,
                          int           height,
                          MetaCrtcMode *crtc_mode)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);
  const MetaCrtcModeInfo *crtc_mode_info =
    meta_crtc_mode_get_info (crtc_mode);

  if (mtk_monitor_transform_is_rotated (output_info->panel_orientation_transform))
    {
      int temp = width;
      width = height;
      height = temp;
    }

  return (MetaMonitorModeSpec) {
    .width = width,
    .height = height,
    .refresh_rate = crtc_mode_info->refresh_rate,
    .refresh_rate_mode = crtc_mode_info->refresh_rate_mode,
    .flags = crtc_mode_info->flags & HANDLED_CRTC_MODE_FLAGS
  };
}

/**
 * meta_monitor_get_gamma_lut_size:
 * @monitor: The MetaMonitor instance to retrieve the size from.
 *
 * Get the size of the look-up tables (LUTs) for the monitor.
 *
 * Retrieve the size of the LUT used to implement the encoding or decoding
 * transfer functions ("gamma", "degamma") for the CRTC or CRTCs that backs
 * this monitor.
 *
 * Returns: The number of look-up table entries possible for the monitor. It is
 *   assumed that each CRTC of a monitor has identical gamma LUT sizes.
 */
size_t
meta_monitor_get_gamma_lut_size (MetaMonitor *monitor)
{
  MetaOutput *output;
  MetaCrtc *crtc;

  output = meta_monitor_get_main_output (monitor);
  crtc = meta_output_get_assigned_crtc (output);
  return meta_crtc_get_gamma_lut_size (crtc);
}

static gboolean
set_gamma_lut (MetaMonitor          *monitor,
               MetaMonitorMode      *mode,
               MetaMonitorCrtcMode  *monitor_crtc_mode,
               gpointer              user_data,
               GError              **error)
{
  const MetaGammaLut *lut = user_data;
  MetaCrtc *crtc;

  crtc = meta_output_get_assigned_crtc (monitor_crtc_mode->output);

  meta_crtc_set_gamma_lut (crtc, lut);
  return TRUE;
}

/**
 * meta_monitor_set_gamma_lut:
 *
 * Set a new gamma look-up table (LUT) for the given monitor's CRTCs.
 */
void
meta_monitor_set_gamma_lut (MetaMonitor        *monitor,
                            const MetaGammaLut *lut)
{
  MetaMonitorMode *current_mode;

  current_mode = meta_monitor_get_current_mode (monitor);
  g_return_if_fail (current_mode);

  meta_monitor_mode_foreach_crtc (monitor,
                                  current_mode,
                                  set_gamma_lut,
                                  (gpointer) lut,
                                  NULL);
}

void
meta_monitor_create_backlight (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  MetaOutput *main_output = meta_monitor_get_main_output (monitor);
  g_autoptr (MetaBacklight) backlight = NULL;
  g_autoptr (GError) error = NULL;

  backlight = meta_output_create_backlight (main_output, &error);
  if (!backlight)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
        {
          meta_topic (META_DEBUG_BACKEND,
                      "No backlight support for monitor %s",
                      meta_monitor_get_display_name (monitor));
        }
      else
        {
          g_warning ("Failed creating backlight for %s: %s",
                     meta_monitor_get_display_name (monitor),
                     error->message);
        }
    }
  else
    {
      meta_topic (META_DEBUG_BACKEND,
                  "Created backlight for monitor %s",
                  meta_monitor_get_display_name (monitor));
    }

  g_set_object (&priv->backlight, backlight);
}

static void
meta_monitor_normal_generate_modes (MetaMonitorNormal *monitor_normal)
{
  MetaMonitor *monitor = META_MONITOR (monitor_normal);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaOutput *output;
  const MetaOutputInfo *output_info;
  MetaCrtcMode *preferred_mode;
  MetaCrtcModeFlag preferred_mode_flags;
  unsigned int i;

  output = meta_monitor_get_main_output (monitor);
  output_info = meta_output_get_info (output);
  preferred_mode = output_info->preferred_mode;
  preferred_mode_flags = meta_crtc_mode_get_info (preferred_mode)->flags;

  for (i = 0; i < output_info->n_modes; i++)
    {
      MetaCrtcMode *crtc_mode = output_info->modes[i];
      const MetaCrtcModeInfo *crtc_mode_info =
        meta_crtc_mode_get_info (crtc_mode);
      MetaCrtc *crtc;
      g_autoptr (MetaMonitorMode) mode = NULL;
      MetaMonitorModePrivate *mode_priv =
        meta_monitor_mode_get_instance_private (mode);
      gboolean replace;

      mode = g_object_new (META_TYPE_MONITOR_MODE, NULL);
      mode_priv = meta_monitor_mode_get_instance_private (mode);
      mode_priv->monitor = monitor;
      mode_priv->spec = meta_monitor_create_spec (monitor,
                                                  crtc_mode_info->width,
                                                  crtc_mode_info->height,
                                                  crtc_mode);
      mode_priv->id = generate_mode_id (&mode_priv->spec);
      mode_priv->n_crtc_modes = 1;
      mode_priv->crtc_modes = g_new (MetaMonitorCrtcMode, 1);
      mode_priv->crtc_modes[0] = (MetaMonitorCrtcMode) {
        .output = output,
        .crtc_mode = g_object_ref (crtc_mode)
      };

      /*
       * We don't distinguish between all available mode flags, just the ones
       * that are configurable. We still need to pick some mode though, so
       * prefer ones that has the same set of flags as the preferred mode;
       * otherwise take the first one in the list. This guarantees that the
       * preferred mode is always added.
       */
      replace = (crtc_mode_info->flags == preferred_mode_flags &&
                 (!monitor_priv->preferred_mode ||
                  g_strcmp0 (meta_monitor_mode_get_id (monitor_priv->preferred_mode),
                             mode_priv->id) != 0));

      if (!meta_monitor_add_mode (monitor, mode, replace))
        {
          g_assert (crtc_mode != output_info->preferred_mode);
          continue;
        }

      if (crtc_mode == output_info->preferred_mode)
        monitor_priv->preferred_mode = mode;

      crtc = meta_output_get_assigned_crtc (output);
      if (crtc)
        {
          const MetaCrtcConfig *crtc_config;

          crtc_config = meta_crtc_get_config (crtc);
          if (crtc_config && crtc_mode == crtc_config->mode)
            monitor_priv->current_mode = mode;
        }
    }
}

static void
reset_normal_monitor (MetaMonitorNormal *monitor_normal,
                      MetaOutput        *output)
{
  MetaMonitor *monitor = META_MONITOR (monitor_normal);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);

  g_clear_list (&monitor_priv->outputs, g_object_unref);
  monitor_priv->outputs = g_list_append (NULL, g_object_ref (output));
  meta_output_set_monitor (output, monitor);

  g_hash_table_remove_all (monitor_priv->mode_ids);
  g_clear_list (&monitor_priv->modes, g_object_unref);
  monitor_priv->preferred_mode = NULL;
  monitor_priv->current_mode = NULL;
  meta_monitor_normal_generate_modes (monitor_normal);
  g_assert (monitor_priv->preferred_mode);

  g_clear_list (&monitor_priv->color_modes, NULL);
  meta_monitor_init_supported_color_modes (monitor);
}

MetaMonitorNormal *
meta_monitor_normal_new (MetaMonitorManager *monitor_manager,
                         MetaOutput         *output)
{
  MetaMonitorNormal *monitor_normal;
  MetaMonitor *monitor;
  MetaMonitorPrivate *monitor_priv;

  monitor_normal = g_object_new (META_TYPE_MONITOR_NORMAL, NULL);
  monitor = META_MONITOR (monitor_normal);
  monitor_priv = meta_monitor_get_instance_private (monitor);

  monitor_priv->backend = meta_monitor_manager_get_backend (monitor_manager);

  reset_normal_monitor (monitor_normal, output);

  meta_monitor_generate_spec (monitor);
  monitor_priv->display_name = meta_monitor_make_display_name (monitor);

  return monitor_normal;
}

static MetaOutput *
meta_monitor_normal_get_main_output (MetaMonitor *monitor)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);

  return monitor_priv->outputs->data;
}

static void
meta_monitor_normal_derive_layout (MetaMonitor  *monitor,
                                   MtkRectangle *layout)
{
  MetaOutput *output;
  MetaCrtc *crtc;
  const MetaCrtcConfig *crtc_config;

  output = meta_monitor_get_main_output (monitor);
  crtc = meta_output_get_assigned_crtc (output);
  crtc_config = meta_crtc_get_config (crtc);

  g_return_if_fail (crtc_config);

  mtk_rectangle_from_graphene_rect (&crtc_config->layout,
                                    MTK_ROUNDING_STRATEGY_ROUND,
                                    layout);
}

static gboolean
meta_monitor_normal_get_suggested_position (MetaMonitor *monitor,
                                            int         *x,
                                            int         *y)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  if (!output_info->hotplug_mode_update)
    return FALSE;

  if (output_info->suggested_x < 0 && output_info->suggested_y < 0)
    return FALSE;

  if (x)
    *x = output_info->suggested_x;

  if (y)
    *y = output_info->suggested_y;

  return TRUE;
}

static void
meta_monitor_normal_calculate_crtc_pos (MetaMonitor         *monitor,
                                        MetaMonitorMode     *monitor_mode,
                                        MetaOutput          *output,
                                        MtkMonitorTransform  crtc_transform,
                                        int                 *out_x,
                                        int                 *out_y)
{
  *out_x = 0;
  *out_y = 0;
}

static gboolean
meta_monitor_normal_update_outputs (MetaMonitor *monitor)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (monitor_priv->backend);
  MetaOutput *old_output = META_OUTPUT (monitor_priv->outputs->data);
  MetaOutput *output;
  const MetaOutputInfo *output_info;

  output = meta_monitor_manager_find_output (monitor_manager,
                                             old_output);
  if (!output)
    return FALSE;

  output_info = meta_output_get_info (output);
  if (output_info->tile_info.group_id)
    return FALSE;

  reset_normal_monitor (META_MONITOR_NORMAL (monitor), output);

  return TRUE;
}

static void
meta_monitor_normal_init (MetaMonitorNormal *monitor)
{
}

static void
meta_monitor_normal_class_init (MetaMonitorNormalClass *klass)
{
  MetaMonitorClass *monitor_class = META_MONITOR_CLASS (klass);

  monitor_class->get_main_output = meta_monitor_normal_get_main_output;
  monitor_class->derive_layout = meta_monitor_normal_derive_layout;
  monitor_class->calculate_crtc_pos = meta_monitor_normal_calculate_crtc_pos;
  monitor_class->get_suggested_position = meta_monitor_normal_get_suggested_position;
  monitor_class->update_outputs = meta_monitor_normal_update_outputs;
}

uint32_t
meta_monitor_tiled_get_tile_group_id (MetaMonitorTiled *monitor_tiled)
{
  return monitor_tiled->tile_group_id;
}

gboolean
meta_monitor_get_suggested_position (MetaMonitor *monitor,
                                     int         *x,
                                     int         *y)
{
  return META_MONITOR_GET_CLASS (monitor)->get_suggested_position (monitor,
                                                                   x, y);
}

static GList *
find_tiled_monitor_outputs (MetaGpu    *gpu,
                            MetaOutput *origin_output)
{
  uint32_t tile_group_id =
    meta_output_get_info (origin_output)->tile_info.group_id;
  GList *outputs = NULL;
  GList *l;

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;
      const MetaOutputInfo *output_info = meta_output_get_info (output);
      const MetaOutputInfo *origin_output_info;

      if (output_info->tile_info.group_id != tile_group_id)
        continue;

      origin_output_info = meta_output_get_info (origin_output);
      g_warn_if_fail (output_info->subpixel_order ==
                      origin_output_info->subpixel_order);

      outputs = g_list_append (outputs, g_object_ref (output));
    }

  return outputs;
}

static void
calculate_tile_coordinate (MetaMonitor         *monitor,
                           MetaOutput          *output,
                           MtkMonitorTransform  crtc_transform,
                           int                 *out_x,
                           int                 *out_y)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  GList *l;
  int x = 0;
  int y = 0;

  for (l = monitor_priv->outputs; l; l = l->next)
    {
      const MetaOutputInfo *other_output_info = meta_output_get_info (l->data);

      switch (crtc_transform)
        {
        case MTK_MONITOR_TRANSFORM_NORMAL:
        case MTK_MONITOR_TRANSFORM_FLIPPED:
          if ((other_output_info->tile_info.loc_v_tile ==
               output_info->tile_info.loc_v_tile) &&
              (other_output_info->tile_info.loc_h_tile <
               output_info->tile_info.loc_h_tile))
            x += other_output_info->tile_info.tile_w;
          if ((other_output_info->tile_info.loc_h_tile ==
               output_info->tile_info.loc_h_tile) &&
              (other_output_info->tile_info.loc_v_tile <
               output_info->tile_info.loc_v_tile))
            y += other_output_info->tile_info.tile_h;
          break;
        case MTK_MONITOR_TRANSFORM_180:
        case MTK_MONITOR_TRANSFORM_FLIPPED_180:
          if ((other_output_info->tile_info.loc_v_tile ==
               output_info->tile_info.loc_v_tile) &&
              (other_output_info->tile_info.loc_h_tile >
               output_info->tile_info.loc_h_tile))
            x += other_output_info->tile_info.tile_w;
          if ((other_output_info->tile_info.loc_h_tile ==
               output_info->tile_info.loc_h_tile) &&
              (other_output_info->tile_info.loc_v_tile >
               output_info->tile_info.loc_v_tile))
            y += other_output_info->tile_info.tile_h;
          break;
        case MTK_MONITOR_TRANSFORM_270:
        case MTK_MONITOR_TRANSFORM_FLIPPED_270:
          if ((other_output_info->tile_info.loc_v_tile ==
               output_info->tile_info.loc_v_tile) &&
              (other_output_info->tile_info.loc_h_tile >
               output_info->tile_info.loc_h_tile))
            y += other_output_info->tile_info.tile_w;
          if ((other_output_info->tile_info.loc_h_tile ==
               output_info->tile_info.loc_h_tile) &&
              (other_output_info->tile_info.loc_v_tile >
               output_info->tile_info.loc_v_tile))
            x += other_output_info->tile_info.tile_h;
          break;
        case MTK_MONITOR_TRANSFORM_90:
        case MTK_MONITOR_TRANSFORM_FLIPPED_90:
          if ((other_output_info->tile_info.loc_v_tile ==
               output_info->tile_info.loc_v_tile) &&
              (other_output_info->tile_info.loc_h_tile <
               output_info->tile_info.loc_h_tile))
            y += other_output_info->tile_info.tile_w;
          if ((other_output_info->tile_info.loc_h_tile ==
               output_info->tile_info.loc_h_tile) &&
              (other_output_info->tile_info.loc_v_tile <
               output_info->tile_info.loc_v_tile))
            x += other_output_info->tile_info.tile_h;
          break;
        }
    }

  *out_x = x;
  *out_y = y;
}

static void
meta_monitor_tiled_calculate_tiled_size (MetaMonitor *monitor,
                                         int         *out_width,
                                         int         *out_height)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  GList *l;
  int width;
  int height;

  width = 0;
  height = 0;
  for (l = monitor_priv->outputs; l; l = l->next)
    {
      const MetaOutputInfo *output_info = meta_output_get_info (l->data);

      if (output_info->tile_info.loc_v_tile == 0)
        width += output_info->tile_info.tile_w;

      if (output_info->tile_info.loc_h_tile == 0)
        height += output_info->tile_info.tile_h;
    }

  *out_width = width;
  *out_height = height;
}

static gboolean
is_monitor_mode_assigned (MetaMonitor     *monitor,
                          MetaMonitorMode *mode)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  MetaMonitorModePrivate *mode_priv =
    meta_monitor_mode_get_instance_private (mode);
  GList *l;
  int i;

  for (l = priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaOutput *output = l->data;
      MetaMonitorCrtcMode *monitor_crtc_mode = &mode_priv->crtc_modes[i];
      MetaCrtc *crtc;
      const MetaCrtcConfig *crtc_config;

      crtc = meta_output_get_assigned_crtc (output);
      crtc_config = crtc ? meta_crtc_get_config (crtc) : NULL;

      if (monitor_crtc_mode->crtc_mode &&
          (!crtc || !crtc_config ||
           crtc_config->mode != monitor_crtc_mode->crtc_mode))
        return FALSE;
      else if (!monitor_crtc_mode->crtc_mode && crtc)
        return FALSE;
    }

  return TRUE;
}

static gboolean
is_crtc_mode_tiled (MetaOutput   *output,
                    MetaCrtcMode *crtc_mode)
{
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  const MetaCrtcModeInfo *crtc_mode_info = meta_crtc_mode_get_info (crtc_mode);

  return (crtc_mode_info->width == (int) output_info->tile_info.tile_w &&
          crtc_mode_info->height == (int) output_info->tile_info.tile_h);
}

static MetaCrtcMode *
find_tiled_crtc_mode (MetaOutput   *output,
                      MetaCrtcMode *reference_crtc_mode)
{
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  const MetaCrtcModeInfo *reference_crtc_mode_info =
    meta_crtc_mode_get_info (reference_crtc_mode);
  MetaCrtcMode *crtc_mode;
  unsigned int i;

  crtc_mode = output_info->preferred_mode;
  if (is_crtc_mode_tiled (output, crtc_mode))
    return crtc_mode;

  for (i = 0; i < output_info->n_modes; i++)
    {
      const MetaCrtcModeInfo *crtc_mode_info;

      crtc_mode = output_info->modes[i];
      crtc_mode_info = meta_crtc_mode_get_info (crtc_mode);

      if (!is_crtc_mode_tiled (output, crtc_mode))
        continue;

      if (crtc_mode_info->refresh_rate != reference_crtc_mode_info->refresh_rate)
        continue;

      if (crtc_mode_info->refresh_rate_mode != reference_crtc_mode_info->refresh_rate_mode)
        continue;

      if (crtc_mode_info->flags != reference_crtc_mode_info->flags)
        continue;

      return crtc_mode;
    }

  return NULL;
}

static MetaMonitorMode *
create_tiled_monitor_mode (MetaMonitorTiled *monitor_tiled,
                           MetaCrtcMode     *reference_crtc_mode,
                           gboolean         *out_is_preferred)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  g_autoptr (MetaMonitorModeTiled) mode_tiled = NULL;
  MetaMonitorModePrivate *mode_priv;
  int width, height;
  GList *l;
  unsigned int i;
  gboolean is_preferred = TRUE;

  mode_tiled = g_object_new (META_TYPE_MONITOR_MODE_TILED, NULL);
  mode_priv =
    meta_monitor_mode_get_instance_private (META_MONITOR_MODE (mode_tiled));
  mode_tiled->is_tiled = TRUE;
  meta_monitor_tiled_calculate_tiled_size (monitor, &width, &height);
  mode_priv->monitor = monitor;
  mode_priv->spec =
    meta_monitor_create_spec (monitor, width, height, reference_crtc_mode);
  mode_priv->id = generate_mode_id (&mode_priv->spec);

  mode_priv->n_crtc_modes = g_list_length (monitor_priv->outputs);
  mode_priv->crtc_modes = g_new0 (MetaMonitorCrtcMode, mode_priv->n_crtc_modes);
  for (l = monitor_priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaOutput *output = l->data;
      const MetaOutputInfo *output_info = meta_output_get_info (output);
      MetaCrtcMode *tiled_crtc_mode;

      tiled_crtc_mode = find_tiled_crtc_mode (output, reference_crtc_mode);
      if (!tiled_crtc_mode)
        {
          g_warning ("No tiled mode found on %s", meta_output_get_name (output));
          return NULL;
        }

      mode_priv->crtc_modes[i] = (MetaMonitorCrtcMode) {
        .output = output,
        .crtc_mode = g_object_ref (tiled_crtc_mode)
      };

      is_preferred = (is_preferred &&
                      tiled_crtc_mode == output_info->preferred_mode);
    }

  *out_is_preferred = is_preferred;

  return META_MONITOR_MODE (g_steal_pointer (&mode_tiled));
}

static void
generate_tiled_monitor_modes (MetaMonitorTiled *monitor_tiled)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaOutput *main_output;
  const MetaOutputInfo *main_output_info;
  GList *tiled_modes = NULL;
  unsigned int i;
  MetaMonitorMode *best_mode = NULL;
  float best_refresh_rate;
  GList *l;

  main_output = meta_monitor_get_main_output (META_MONITOR (monitor_tiled));
  main_output_info = meta_output_get_info (main_output);

  for (i = 0; i < main_output_info->n_modes; i++)
    {
      MetaCrtcMode *reference_crtc_mode = main_output_info->modes[i];
      MetaMonitorMode *mode;
      gboolean is_preferred;

      if (!is_crtc_mode_tiled (main_output, reference_crtc_mode))
        continue;

      mode = create_tiled_monitor_mode (monitor_tiled, reference_crtc_mode,
                                        &is_preferred);
      if (!mode)
        continue;

      tiled_modes = g_list_append (tiled_modes, mode);

      if (is_monitor_mode_assigned (monitor, mode))
        monitor_priv->current_mode = mode;

      if (is_preferred)
        monitor_priv->preferred_mode = mode;
    }

  while ((l = tiled_modes))
    {
      g_autoptr (MetaMonitorMode) mode = META_MONITOR_MODE (l->data);
      MetaMonitorModePrivate *mode_priv =
        meta_monitor_mode_get_instance_private (mode);

      tiled_modes = g_list_delete_link (tiled_modes, l);

      if (!meta_monitor_add_mode (monitor, mode, FALSE))
        continue;

      if (!monitor_priv->preferred_mode)
        {
          if (!best_mode)
            {
              best_mode = mode;
              best_refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
              continue;
            }

          if (mode_priv->spec.refresh_rate > best_refresh_rate)
            {
              best_mode = mode;
              best_refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
              continue;
            }

          if (mode_priv->spec.refresh_rate == best_refresh_rate &&
              mode_priv->spec.refresh_rate_mode > best_refresh_rate)
            {
              best_mode = mode;
              best_refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
              continue;
            }
        }
    }

  if (best_mode)
    monitor_priv->preferred_mode = best_mode;
}

static MetaMonitorMode *
create_untiled_monitor_mode (MetaMonitorTiled *monitor_tiled,
                             MetaOutput       *main_output,
                             MetaCrtcMode     *crtc_mode)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorModeTiled *mode_tiled;
  MetaMonitorModePrivate *mode_priv;
  const MetaCrtcModeInfo *crtc_mode_info;
  GList *l;
  int i;

  if (is_crtc_mode_tiled (main_output, crtc_mode))
    return NULL;

  mode_tiled = g_object_new (META_TYPE_MONITOR_MODE_TILED, NULL);
  mode_priv =
    meta_monitor_mode_get_instance_private (META_MONITOR_MODE (mode_tiled));
  mode_tiled->is_tiled = FALSE;
  mode_priv->monitor = monitor;

  crtc_mode_info = meta_crtc_mode_get_info (crtc_mode);
  mode_priv->spec = meta_monitor_create_spec (monitor,
                                         crtc_mode_info->width,
                                         crtc_mode_info->height,
                                         crtc_mode);
  mode_priv->id = generate_mode_id (&mode_priv->spec);
  mode_priv->n_crtc_modes = g_list_length (monitor_priv->outputs);
  mode_priv->crtc_modes = g_new0 (MetaMonitorCrtcMode, mode_priv->n_crtc_modes);

  for (l = monitor_priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaOutput *output = l->data;

      if (output == main_output)
        {
          mode_priv->crtc_modes[i] = (MetaMonitorCrtcMode) {
            .output = output,
            .crtc_mode = g_object_ref (crtc_mode)
          };
        }
      else
        {
          mode_priv->crtc_modes[i] = (MetaMonitorCrtcMode) {
            .output = output,
            .crtc_mode = NULL
          };
        }
    }

  return META_MONITOR_MODE (mode_tiled);
}

static int
count_untiled_crtc_modes (MetaOutput *output)
{
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  int count;
  unsigned int i;

  count = 0;
  for (i = 0; i < output_info->n_modes; i++)
    {
      MetaCrtcMode *crtc_mode = output_info->modes[i];

      if (!is_crtc_mode_tiled (output, crtc_mode))
        count++;
    }

  return count;
}

static MetaOutput *
find_origin_output (GList *outputs)
{
  GList *l;

  for (l = outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;
      const MetaOutputInfo *output_info = meta_output_get_info (output);

      if (output_info->tile_info.loc_h_tile == 0 &&
          output_info->tile_info.loc_v_tile == 0)
        return output;
    }

  return NULL;
}

static MetaOutput *
find_untiled_output (MetaOutput *origin_output,
                     GList      *outputs)
{
  MetaOutput *best_output;
  int best_untiled_crtc_mode_count;
  GList *l;

  best_output = origin_output;
  best_untiled_crtc_mode_count = count_untiled_crtc_modes (origin_output);

  for (l = outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;
      int untiled_crtc_mode_count;

      if (output == origin_output)
        continue;

      untiled_crtc_mode_count = count_untiled_crtc_modes (output);
      if (untiled_crtc_mode_count > best_untiled_crtc_mode_count)
        {
          best_untiled_crtc_mode_count = untiled_crtc_mode_count;
          best_output = output;
        }
    }

  return best_output;
}

static void
generate_untiled_monitor_modes (MetaMonitorTiled *monitor_tiled)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaOutput *main_output;
  const MetaOutputInfo *main_output_info;
  unsigned int i;

  main_output = meta_monitor_get_main_output (monitor);
  main_output_info = meta_output_get_info (main_output);

  for (i = 0; i < main_output_info->n_modes; i++)
    {
      MetaCrtcMode *crtc_mode = main_output_info->modes[i];
      g_autoptr (MetaMonitorMode) mode = NULL;

      mode = create_untiled_monitor_mode (monitor_tiled,
                                          main_output,
                                          crtc_mode);
      if (!mode)
        continue;

      if (!meta_monitor_add_mode (monitor, mode, FALSE))
        continue;

      if (is_monitor_mode_assigned (monitor, mode))
        {
          g_assert (!monitor_priv->current_mode);
          monitor_priv->current_mode = mode;
        }

      if (!monitor_priv->preferred_mode &&
          crtc_mode == main_output_info->preferred_mode)
        monitor_priv->preferred_mode = mode;
    }
}

static MetaMonitorMode *
find_best_mode (MetaMonitor *monitor)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorMode *best_mode = NULL;
  float best_refresh_rate;
  GList *l;

  for (l = monitor_priv->modes; l; l = l->next)
    {
      MetaMonitorMode *mode = l->data;
      MetaMonitorModePrivate *mode_priv =
        meta_monitor_mode_get_instance_private (mode);
      int best_width, best_height;
      int area, best_area;

      if (!best_mode)
        {
          best_mode = mode;
          continue;
        }

      area = mode_priv->spec.width * mode_priv->spec.height;
      meta_monitor_mode_get_resolution (best_mode, &best_width, &best_height);
      best_area = best_width * best_height;
      if (area > best_area)
        {
          best_mode = mode;
          best_refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
          continue;
        }

      if (mode_priv->spec.refresh_rate > best_refresh_rate)
        {
          best_mode = mode;
          best_refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
          continue;
        }

      if (mode_priv->spec.refresh_rate == best_refresh_rate &&
          mode_priv->spec.refresh_rate_mode > best_refresh_rate)
        {
          best_mode = mode;
          best_refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
          continue;
        }
    }

  return best_mode;
}

static void
meta_monitor_tiled_generate_modes (MetaMonitorTiled *monitor_tiled)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);

  /*
   * Tiled monitors may look a bit different from each other, depending on the
   * monitor itself, the driver, etc.
   *
   * On some, the tiled modes will be the preferred CRTC modes, and running
   * untiled is done by only enabling (0, 0) tile. In this case, things are
   * pretty straight forward.
   *
   * Other times a monitor may have some bogus mode preferred on the main tile,
   * and an untiled mode preferred on the non-main tile, and there seems to be
   * no guarantee that the (0, 0) tile is the one that should drive the
   * non-tiled mode.
   *
   * To handle both these cases, the following hueristics are implemented:
   *
   *  1) Find all the tiled CRTC modes of the (0, 0) tile, and create tiled
   *     monitor modes for all tiles based on these.
   *  2) If there is any tiled monitor mode combination where all CRTC modes
   *     are the preferred ones, that one is marked as preferred.
   *  3) If there is no preferred mode determined so far, assume the tiled
   *     monitor mode with the highest refresh rate is preferred.
   *  4) Find the tile with highest number of untiled CRTC modes available,
   *     assume this is the one driving the monitor in untiled mode, and
   *     create monitor modes for all untiled CRTC modes of that tile. If
   *     there is still no preferred mode, set any untiled mode as preferred
   *     if the CRTC mode is marked as such.
   *  5) If at this point there is still no preferred mode, just pick the one
   *     with the highest number of pixels and highest refresh rate.
   *
   * Note that this ignores the preference if the preference is a non-tiled
   * mode. This seems to be the case on some systems, where the user tends to
   * manually set up the tiled mode anyway.
   */

  generate_tiled_monitor_modes (monitor_tiled);

  if (!monitor_priv->preferred_mode)
    g_warning ("Tiled monitor on %s didn't have any tiled modes",
               monitor_priv->spec->connector);

  generate_untiled_monitor_modes (monitor_tiled);

  if (!monitor_priv->preferred_mode)
    {
      g_warning ("Tiled monitor on %s didn't have a valid preferred mode",
                 monitor_priv->spec->connector);
      monitor_priv->preferred_mode = find_best_mode (monitor);
    }
}

static void
reset_tiled_monitor (MetaMonitorTiled *monitor_tiled,
                     GList            *outputs,
                     MetaOutput       *origin_output,
                     MetaOutput       *main_output)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);

  g_clear_list (&monitor_priv->outputs, g_object_unref);
  monitor_priv->outputs = g_steal_pointer (&outputs);
  g_list_foreach (monitor_priv->outputs,
                  (GFunc) meta_output_set_monitor,
                  monitor);

  monitor_tiled->origin_output = origin_output;
  monitor_tiled->main_output = main_output;

  g_hash_table_remove_all (monitor_priv->mode_ids);
  g_clear_list (&monitor_priv->modes, g_object_unref);
  monitor_priv->preferred_mode = NULL;
  monitor_priv->current_mode = NULL;
  meta_monitor_tiled_generate_modes (monitor_tiled);
  g_assert (monitor_priv->preferred_mode);

  g_clear_list (&monitor_priv->color_modes, NULL);
  meta_monitor_init_supported_color_modes (monitor);
}

MetaMonitorTiled *
meta_monitor_tiled_new (MetaMonitorManager *monitor_manager,
                        MetaOutput         *output)
{
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  g_autolist (MetaOutput) outputs = NULL;
  MetaMonitorTiled *monitor_tiled;
  MetaMonitor *monitor;
  MetaMonitorPrivate *monitor_priv;
  MetaOutput *origin_output;
  MetaOutput *main_output;

  monitor_tiled = g_object_new (META_TYPE_MONITOR_TILED, NULL);
  monitor = META_MONITOR (monitor_tiled);
  monitor_priv = meta_monitor_get_instance_private (monitor);

  monitor_priv->backend = meta_monitor_manager_get_backend (monitor_manager);
  monitor_tiled->monitor_manager = monitor_manager;

  monitor_tiled->tile_group_id = output_info->tile_info.group_id;

  origin_output = output;
  outputs = find_tiled_monitor_outputs (meta_output_get_gpu (output),
                                        origin_output);

  main_output = find_untiled_output (origin_output, outputs);

  reset_tiled_monitor (monitor_tiled, g_steal_pointer (&outputs),
                       origin_output, main_output);

  meta_monitor_generate_spec (monitor);
  monitor_priv->display_name = meta_monitor_make_display_name (monitor);
  meta_monitor_manager_tiled_monitor_added (monitor_manager,
                                            META_MONITOR (monitor_tiled));

  return monitor_tiled;
}

static MetaOutput *
meta_monitor_tiled_get_main_output (MetaMonitor *monitor)
{
  MetaMonitorTiled *monitor_tiled = META_MONITOR_TILED (monitor);

  return monitor_tiled->main_output;
}

static void
meta_monitor_tiled_derive_layout (MetaMonitor  *monitor,
                                  MtkRectangle *layout)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  GList *l;
  float min_x, min_y, max_x, max_y;

  min_x = FLT_MAX;
  min_y = FLT_MAX;
  max_x = 0.0;
  max_y = 0.0;
  for (l = monitor_priv->outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;
      MetaCrtc *crtc;
      const MetaCrtcConfig *crtc_config;
      const graphene_rect_t *crtc_layout;

      crtc = meta_output_get_assigned_crtc (output);
      if (!crtc)
        continue;

      crtc_config = meta_crtc_get_config (crtc);
      g_return_if_fail (crtc_config);

      crtc_layout = &crtc_config->layout;

      min_x = MIN (crtc_layout->origin.x, min_x);
      min_y = MIN (crtc_layout->origin.y, min_y);
      max_x = MAX (crtc_layout->origin.x + crtc_layout->size.width, max_x);
      max_y = MAX (crtc_layout->origin.y + crtc_layout->size.height, max_y);
    }

  *layout = (MtkRectangle) {
    .x = (int) roundf (min_x),
    .y = (int) roundf (min_y),
    .width = (int) roundf (max_x - min_x),
    .height = (int) roundf (max_y - min_y)
  };
}

static gboolean
meta_monitor_tiled_get_suggested_position (MetaMonitor *monitor,
                                           int         *x,
                                           int         *y)
{
  return FALSE;
}

static void
meta_monitor_tiled_calculate_crtc_pos (MetaMonitor         *monitor,
                                       MetaMonitorMode     *monitor_mode,
                                       MetaOutput          *output,
                                       MtkMonitorTransform  crtc_transform,
                                       int                 *out_x,
                                       int                 *out_y)
{
  MetaMonitorModeTiled *mode_tiled = (MetaMonitorModeTiled *) monitor_mode;

  if (mode_tiled->is_tiled)
    {
      calculate_tile_coordinate (monitor, output, crtc_transform,
                                 out_x, out_y);
    }
  else
    {
      *out_x = 0;
      *out_y = 0;
    }
}

static void
meta_monitor_tiled_finalize (GObject *object)
{
  MetaMonitorTiled *monitor_tiled = META_MONITOR_TILED (object);

  meta_monitor_manager_tiled_monitor_removed (monitor_tiled->monitor_manager,
                                              META_MONITOR (monitor_tiled));

  G_OBJECT_CLASS (meta_monitor_tiled_parent_class)->finalize (object);
}

static gboolean
meta_monitor_tiled_update_outputs (MetaMonitor *monitor)
{
  MetaMonitorTiled *monitor_tiled = META_MONITOR_TILED (monitor);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (monitor_priv->backend);
  GList *old_outputs = monitor_priv->outputs;
  g_autolist (MetaOutput) outputs = NULL;
  MetaOutput *first_output = NULL;
  GList *l;
  MetaOutput *origin_output;
  MetaOutput *main_output;

  for (l = old_outputs; l; l = l->next)
    {
      MetaOutput *old_output = META_OUTPUT (l->data);
      MetaOutput *output;

      output = meta_monitor_manager_find_output (monitor_manager,
                                                 old_output);
      if (!output)
        return FALSE;

      if (!first_output)
        {
          const MetaOutputInfo *output_info =
            meta_output_get_info (output);

          if (!output_info->tile_info.group_id)
            return FALSE;

          first_output = output;
        }
      else
        {
          const MetaOutputInfo *output_info =
            meta_output_get_info (output);
          const MetaOutputInfo *first_output_info =
            meta_output_get_info (first_output);

          if (output_info->tile_info.group_id !=
              first_output_info->tile_info.group_id)
            return FALSE;
        }

      outputs = g_list_append (outputs, g_object_ref (output));
    }

  if (g_list_length (outputs) != g_list_length (monitor_priv->outputs))
    return FALSE;

  origin_output = find_origin_output (outputs);
  if (!meta_output_matches (monitor_tiled->origin_output,
                            origin_output))
    return FALSE;

  main_output = find_untiled_output (origin_output, outputs);
  if (!meta_output_matches (monitor_tiled->main_output,
                            main_output))
    return FALSE;

  reset_tiled_monitor (monitor_tiled, g_steal_pointer (&outputs),
                       origin_output, main_output);

  return TRUE;
}

static void
meta_monitor_tiled_init (MetaMonitorTiled *monitor)
{
}

static void
meta_monitor_tiled_class_init (MetaMonitorTiledClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaMonitorClass *monitor_class = META_MONITOR_CLASS (klass);

  object_class->finalize = meta_monitor_tiled_finalize;

  monitor_class->get_main_output = meta_monitor_tiled_get_main_output;
  monitor_class->derive_layout = meta_monitor_tiled_derive_layout;
  monitor_class->calculate_crtc_pos = meta_monitor_tiled_calculate_crtc_pos;
  monitor_class->get_suggested_position = meta_monitor_tiled_get_suggested_position;
  monitor_class->update_outputs = meta_monitor_tiled_update_outputs;
}

static void
meta_monitor_mode_finalize (GObject *object)
{
  MetaMonitorMode *monitor_mode = META_MONITOR_MODE (object);
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (monitor_mode);

  g_free (priv->id);
  for (int i = 0; i < priv->n_crtc_modes; i++)
    g_clear_object (&priv->crtc_modes[i].crtc_mode);
  g_free (priv->crtc_modes);

  G_OBJECT_CLASS (meta_monitor_mode_parent_class)->finalize (object);
}

static void
meta_monitor_mode_class_init (MetaMonitorModeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_monitor_mode_finalize;
}

static void
meta_monitor_mode_init (MetaMonitorMode *monitor_mode)
{
}

static void
meta_monitor_mode_tiled_class_init (MetaMonitorModeTiledClass *klass)
{
}

static void
meta_monitor_mode_tiled_init (MetaMonitorModeTiled *monitor_mode_tiled)
{
}

MetaMonitorSpec *
meta_monitor_get_spec (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->spec;
}

MetaLogicalMonitor *
meta_monitor_get_logical_monitor (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->logical_monitor;
}

MetaMonitorMode *
meta_monitor_get_mode_from_id (MetaMonitor *monitor,
                               const char  *monitor_mode_id)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return g_hash_table_lookup (priv->mode_ids, monitor_mode_id);
}

gboolean
meta_monitor_mode_spec_has_similar_size (MetaMonitorModeSpec *monitor_mode_spec,
                                         MetaMonitorModeSpec *other_monitor_mode_spec)
{
  const float target_ratio = 1.0f;
  /* The a size difference of 15% means e.g. 4K modes matches other 4K modes,
   * FHD (2K) modes other FHD modes, and HD modes other HD modes, but not each
   * other.
   */
  const float epsilon = 0.15f;

  return G_APPROX_VALUE (((float) monitor_mode_spec->width /
                          other_monitor_mode_spec->width) *
                         ((float) monitor_mode_spec->height /
                          other_monitor_mode_spec->height),
                         target_ratio, epsilon);
}

static gboolean
meta_monitor_mode_spec_equals (MetaMonitorModeSpec *monitor_mode_spec,
                               MetaMonitorModeSpec *other_monitor_mode_spec)
{
  return (monitor_mode_spec->width == other_monitor_mode_spec->width &&
          monitor_mode_spec->height == other_monitor_mode_spec->height &&
          ABS (monitor_mode_spec->refresh_rate -
               other_monitor_mode_spec->refresh_rate) < MAXIMUM_REFRESH_RATE_DIFF &&
          monitor_mode_spec->refresh_rate_mode ==
          other_monitor_mode_spec->refresh_rate_mode &&
          monitor_mode_spec->flags == other_monitor_mode_spec->flags);
}

MetaMonitorMode *
meta_monitor_get_mode_from_spec (MetaMonitor         *monitor,
                                 MetaMonitorModeSpec *monitor_mode_spec)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  GList *l;

  for (l = priv->modes; l; l = l->next)
    {
      MetaMonitorMode *monitor_mode = l->data;
      MetaMonitorModePrivate *mode_priv =
        meta_monitor_mode_get_instance_private (monitor_mode);


      if (meta_monitor_mode_spec_equals (monitor_mode_spec,
                                         &mode_priv->spec))
        return monitor_mode;
    }

  return NULL;
}

MetaMonitorMode *
meta_monitor_get_preferred_mode (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->preferred_mode;
}

MetaMonitorMode *
meta_monitor_get_current_mode (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->current_mode;
}

static gboolean
is_current_mode_known (MetaMonitor *monitor)
{
  MetaOutput *output;
  MetaCrtc *crtc;

  output = meta_monitor_get_main_output (monitor);
  crtc = meta_output_get_assigned_crtc (output);

  return (meta_monitor_is_active (monitor) ==
          (crtc && meta_crtc_get_config (crtc)));
}

void
meta_monitor_update_current_mode (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  MetaMonitorMode *current_mode = NULL;
  GList *l;

  for (l = priv->modes; l; l = l->next)
    {
      MetaMonitorMode *mode = l->data;

      if (is_monitor_mode_assigned (monitor, mode))
        {
          current_mode = mode;
          break;
        }
    }

  priv->current_mode = current_mode;

  g_warn_if_fail (is_current_mode_known (monitor));
}

void
meta_monitor_set_current_mode (MetaMonitor     *monitor,
                               MetaMonitorMode *mode)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  priv->current_mode = mode;
}

GList *
meta_monitor_get_modes (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->modes;
}

void
meta_monitor_calculate_crtc_pos (MetaMonitor         *monitor,
                                 MetaMonitorMode     *monitor_mode,
                                 MetaOutput          *output,
                                 MtkMonitorTransform  crtc_transform,
                                 int                 *out_x,
                                 int                 *out_y)
{
  META_MONITOR_GET_CLASS (monitor)->calculate_crtc_pos (monitor,
                                                        monitor_mode,
                                                        output,
                                                        crtc_transform,
                                                        out_x,
                                                        out_y);
}

/*
 * We choose a default scale factor such that the UI is as big
 * as it would be on a display with this DPI without scaling.
 *
 * Through experiementing, a value of 135 has been found to best
 * line up with the UI size chosen as default by other operating
 * systems (macOS, Android, iOS, Windows) and the community-decided
 * "known-good" scale factors for GNOME for various mobile devices
 * such as phones, tablets, and laptops
 */
#define UI_SCALE_MOBILE_TARGET_DPI 135

/*
 * People tend to sit further away from larger stationary displays
 * than they do from mobile displays, so a UI of an identical size to
 * a mobile device has a smaller angular size and thus seems to be too
 * small.
 *
 * The largest mainstream laptops have screens ~17in, and HiDPI external
 * monitors start at ~23in, so 20in is a good boundary point
 */
#define UI_SCALE_LARGE_TARGET_DPI 110
#define UI_SCALE_LARGE_MIN_SIZE_INCHES 20

static float
calculate_scale (MetaMonitor                *monitor,
                 MetaMonitorMode            *monitor_mode,
                 MetaMonitorScalesConstraint constraints)
{
  int width_px, height_px, width_mm, height_mm;
  float diag_inches;
  g_autofree float *scales = NULL;
  int n_scales;
  float best_scale, physical_dpi, perfect_scale, best_scale_error;
  int target_dpi;

  /*
   * Somebody encoded the aspect ratio (16/9 or 16/10) instead of the physical
   * size. We'll be unable to select an appropriate scale factor.
   */
  if (meta_monitor_has_aspect_as_size (monitor))
    return 1.0;

  /* Compute display's diagonal size in inches */
  meta_monitor_get_physical_dimensions (monitor, &width_mm, &height_mm);
  if (width_mm == 0 || height_mm == 0)
    return 1.0;
  diag_inches = sqrtf (width_mm * width_mm + height_mm * height_mm) / 25.4f;

  /* Pick the appropriate target DPI based on screen size */
  if (diag_inches < UI_SCALE_LARGE_MIN_SIZE_INCHES)
    target_dpi = UI_SCALE_MOBILE_TARGET_DPI;
  else
    target_dpi = UI_SCALE_LARGE_TARGET_DPI;

  meta_monitor_mode_get_resolution (monitor_mode, &width_px, &height_px);

  physical_dpi = sqrtf (width_px * width_px + height_px * height_px) /
                 diag_inches;
  perfect_scale = physical_dpi / target_dpi;

  if (constraints & META_MONITOR_SCALES_CONSTRAINT_NO_FRAC)
    perfect_scale -= 0.125f;

  /* We'll only be considering the supported scale factors */
  scales = meta_monitor_calculate_supported_scales (monitor, monitor_mode,
                                                    constraints,
                                                    &n_scales);
  best_scale = scales[0];
  for (int i = 0; i < n_scales; i++)
    {
      float scale_error = fabsf (scales[i] - perfect_scale);

      if (i == 0 || scale_error < best_scale_error)
        {
          best_scale = scales[i];
          best_scale_error = scale_error;
        }
    }

  return best_scale;
}

float
meta_monitor_calculate_mode_scale (MetaMonitor                *monitor,
                                   MetaMonitorMode            *monitor_mode,
                                   MetaMonitorScalesConstraint constraints)
{
  MetaBackend *backend = meta_monitor_get_backend (monitor);
  MetaSettings *settings = meta_backend_get_settings (backend);
  int global_scaling_factor;

  if (meta_settings_get_global_scaling_factor (settings,
                                               &global_scaling_factor))
    return global_scaling_factor;

  return calculate_scale (monitor, monitor_mode, constraints);
}

static gboolean
is_logical_size_large_enough (int width,
                              int height)
{
  return width * height >= MINIMUM_LOGICAL_AREA;
}

static gboolean
is_scale_valid_for_size (float width,
                         float height,
                         float scale)
{
  if (scale < MINIMUM_SCALE_FACTOR || scale > MAXIMUM_SCALE_FACTOR)
    return FALSE;

  return is_logical_size_large_enough ((int) floorf (width / scale),
                                       (int) floorf (height / scale));
}

gboolean
meta_monitor_mode_should_be_advertised (MetaMonitorMode *monitor_mode)
{
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (monitor_mode);
  MetaMonitorMode *preferred_mode;
  MetaMonitorModeSpec *preferred_mode_spec;

  g_return_val_if_fail (monitor_mode != NULL, FALSE);

  preferred_mode = meta_monitor_get_preferred_mode (priv->monitor);
  preferred_mode_spec = meta_monitor_mode_get_spec (preferred_mode);
  if (priv->spec.width == preferred_mode_spec->width &&
      priv->spec.height == preferred_mode_spec->height)
    return TRUE;

  return is_logical_size_large_enough (priv->spec.width,
                                       priv->spec.height);
}

MetaMonitor *
meta_monitor_mode_get_monitor (MetaMonitorMode *monitor_mode)
{
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (monitor_mode);

  return priv->monitor;
}

float
meta_get_closest_monitor_scale_factor_for_resolution (float width,
                                                      float height,
                                                      float scale,
                                                      float threshold)
{
  unsigned int i, j;
  float scaled_h;
  float scaled_w;
  float best_scale;
  int base_scaled_w;
  gboolean found_one;

  best_scale = 0;

  if (fmodf (width, scale) == 0.0 && fmodf (height, scale) == 0.0)
    return scale;

  i = 0;
  found_one = FALSE;
  base_scaled_w = (int) floorf (width / scale);

  do
    {
      for (j = 0; j < 2; j++)
        {
          float current_scale;
          int offset = i * (j ? 1 : -1);

          scaled_w = base_scaled_w + offset;
          current_scale = width / scaled_w;
          scaled_h = height / current_scale;

          if (current_scale >= scale + threshold ||
              current_scale <= scale - threshold ||
              current_scale < MINIMUM_SCALE_FACTOR ||
              current_scale > MAXIMUM_SCALE_FACTOR)
            {
              return best_scale;
            }

          if (floorf (scaled_h) == scaled_h)
            {
              found_one = TRUE;

              if (fabsf (current_scale - scale) < fabsf (best_scale - scale))
                best_scale = current_scale;
            }
        }

      i++;
    }
  while (!found_one);

  return best_scale;
}

float *
meta_monitor_calculate_supported_scales (MetaMonitor                 *monitor,
                                         MetaMonitorMode             *monitor_mode,
                                         MetaMonitorScalesConstraint  constraints,
                                         int                         *n_supported_scales)
{
  unsigned int i, j;
  int width, height;
  GArray *supported_scales;

  supported_scales = g_array_new (FALSE, FALSE, sizeof (float));

  meta_monitor_mode_get_resolution (monitor_mode, &width, &height);

  for (i = floorf (MINIMUM_SCALE_FACTOR);
       i <= ceilf (MAXIMUM_SCALE_FACTOR);
       i++)
    {
      if (constraints & META_MONITOR_SCALES_CONSTRAINT_NO_FRAC)
        {
          if (is_scale_valid_for_size (width, height, i))
            {
              float scale = i;
              g_array_append_val (supported_scales, scale);
            }
        }
      else
        {
          float max_bound;

          if (i == floorf (MINIMUM_SCALE_FACTOR) ||
              i == ceilf (MAXIMUM_SCALE_FACTOR))
            max_bound = SCALE_FACTORS_STEPS;
          else
            max_bound = SCALE_FACTORS_STEPS / 2.0;

          for (j = 0; j < SCALE_FACTORS_PER_INTEGER; j++)
            {
              float scale;
              float scale_value = i + j * SCALE_FACTORS_STEPS;

              if (!is_scale_valid_for_size (width, height, scale_value))
                continue;

              scale = meta_get_closest_monitor_scale_factor_for_resolution (width,
                                                                            height,
                                                                            scale_value,
                                                                            max_bound);
              if (scale > 0.0)
                g_array_append_val (supported_scales, scale);
            }
        }
    }

  if (supported_scales->len == 0)
    {
      float fallback_scale;

      fallback_scale = 1.0;
      g_array_append_val (supported_scales, fallback_scale);
    }

  *n_supported_scales = supported_scales->len;
  return (float *) g_array_free (supported_scales, FALSE);
}

MetaMonitorModeSpec *
meta_monitor_mode_get_spec (MetaMonitorMode *monitor_mode)
{
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (monitor_mode);

  return &priv->spec;
}

const char *
meta_monitor_mode_get_id (MetaMonitorMode *monitor_mode)
{
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (monitor_mode);

  return priv->id;
}

void
meta_monitor_mode_get_resolution (MetaMonitorMode *monitor_mode,
                                  int             *width,
                                  int             *height)
{
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (monitor_mode);

  *width = priv->spec.width;
  *height = priv->spec.height;
}

float
meta_monitor_mode_get_refresh_rate (MetaMonitorMode *monitor_mode)
{
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (monitor_mode);

  return priv->spec.refresh_rate;
}

MetaCrtcRefreshRateMode
meta_monitor_mode_get_refresh_rate_mode (MetaMonitorMode *monitor_mode)
{
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (monitor_mode);

  return priv->spec.refresh_rate_mode;
}

MetaCrtcModeFlag
meta_monitor_mode_get_flags (MetaMonitorMode *monitor_mode)
{
  MetaMonitorModePrivate *priv =
    meta_monitor_mode_get_instance_private (monitor_mode);

  return priv->spec.flags;
}

gboolean
meta_monitor_mode_foreach_crtc (MetaMonitor          *monitor,
                                MetaMonitorMode      *mode,
                                MetaMonitorModeFunc   func,
                                gpointer              user_data,
                                GError              **error)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorModePrivate *mode_priv =
    meta_monitor_mode_get_instance_private (mode);
  GList *l;
  int i;

  for (l = monitor_priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaMonitorCrtcMode *monitor_crtc_mode = &mode_priv->crtc_modes[i];

      if (!monitor_crtc_mode->crtc_mode)
        continue;

      if (!func (monitor, mode, monitor_crtc_mode, user_data, error))
        return FALSE;
    }

  return TRUE;
}

gboolean
meta_monitor_mode_foreach_output (MetaMonitor          *monitor,
                                  MetaMonitorMode      *mode,
                                  MetaMonitorModeFunc   func,
                                  gpointer              user_data,
                                  GError              **error)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorModePrivate *mode_priv =
    meta_monitor_mode_get_instance_private (mode);
  GList *l;
  int i;

  for (l = monitor_priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaMonitorCrtcMode *monitor_crtc_mode = &mode_priv->crtc_modes[i];

      if (!func (monitor, mode, monitor_crtc_mode, user_data, error))
        return FALSE;
    }

  return TRUE;
}

/**
 * meta_monitor_get_display_name:
 * @monitor: A #MetaMonitor object
 *
 * Get the displayable name of the monitor.
 *
 * Returns: The displayable name of the monitor.
 */
const char *
meta_monitor_get_display_name (MetaMonitor *monitor)
{
  MetaMonitorPrivate *monitor_priv;

  g_return_val_if_fail (META_IS_MONITOR (monitor), NULL);

  monitor_priv = meta_monitor_get_instance_private (monitor);

  return monitor_priv->display_name;
}

void
meta_monitor_set_logical_monitor (MetaMonitor        *monitor,
                                  MetaLogicalMonitor *logical_monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  priv->logical_monitor = logical_monitor;
}

static MetaOutput *
maybe_get_privacy_screen_output (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  if (priv->outputs && priv->outputs->next)
      return NULL;

  return meta_monitor_get_main_output (monitor);
}

MetaPrivacyScreenState
meta_monitor_get_privacy_screen_state (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = maybe_get_privacy_screen_output (monitor);

  if (!output)
    return META_PRIVACY_SCREEN_UNAVAILABLE;

  return meta_output_get_privacy_screen_state (output);
}

gboolean
meta_monitor_set_privacy_screen_enabled (MetaMonitor  *monitor,
                                         gboolean      enabled,
                                         GError      **error)
{
  MetaOutput *output;

  output = maybe_get_privacy_screen_output (monitor);

  if (!output)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "The privacy screen is not supported by this output");
      return FALSE;
    }

  return meta_output_set_privacy_screen_enabled (output, enabled, error);
}

gboolean
meta_monitor_get_min_refresh_rate (MetaMonitor *monitor,
                                   int         *min_refresh_rate)
{
  const MetaOutputInfo *output_info =
    meta_monitor_get_main_output_info (monitor);

  return meta_output_info_get_min_refresh_rate (output_info,
                                                min_refresh_rate);
}

GList *
meta_monitor_get_supported_color_modes (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->color_modes;
}

gboolean
meta_monitor_is_color_mode_supported (MetaMonitor   *monitor,
                                      MetaColorMode  color_mode)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return !!g_list_find (priv->color_modes, GINT_TO_POINTER (color_mode));
}

MetaColorMode
meta_monitor_get_color_mode (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);
  return meta_output_get_color_mode (output);
}

gboolean
meta_parse_monitor_mode (const char *string,
                         int        *out_width,
                         int        *out_height,
                         float      *out_refresh_rate,
                         float       fallback_refresh_rate)
{
  char *ptr = (char *) string;
  int width, height;
  float refresh_rate;

  width = g_ascii_strtoull (ptr, &ptr, 10);
  if (width == 0)
    return FALSE;

  if (ptr[0] != 'x')
    return FALSE;
  ptr++;

  height = g_ascii_strtoull (ptr, &ptr, 10);
  if (height == 0)
    return FALSE;

  if (ptr[0] == '\0')
    {
      refresh_rate = fallback_refresh_rate;
      goto out;
    }

  if (ptr[0] != '@')
    return FALSE;
  ptr++;

  refresh_rate = (float) g_ascii_strtod (ptr, &ptr);
  if (G_APPROX_VALUE (refresh_rate, 0.0f, FLT_EPSILON))
    return FALSE;

  if (ptr[0] != '\0')
    return FALSE;

out:
  *out_width = width;
  *out_height = height;
  *out_refresh_rate = refresh_rate;
  return TRUE;
}

/**
 * meta_monitor_get_backlight:
 * @monitor: A #MetaMonitor object
 *
 * Returns the [class@Meta.Backlight] of the monitor, or NULL if it has no
 * controllable backlight.
 *
 * Returns: (transfer none) (nullable): The [class@Meta.Backlight].
 */
MetaBacklight *
meta_monitor_get_backlight (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv;

  g_return_val_if_fail (META_IS_MONITOR (monitor), NULL);

  priv = meta_monitor_get_instance_private (monitor);

  return priv->backlight;
}

void
meta_monitor_set_for_lease (MetaMonitor *monitor,
                            gboolean     for_lease)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  priv->is_for_lease = for_lease;
}

gboolean
meta_monitor_is_for_lease (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->is_for_lease;
}

gboolean
meta_monitor_update_outputs (MetaMonitor *monitor)
{
  meta_monitor_set_logical_monitor (monitor, NULL);

  return META_MONITOR_GET_CLASS (monitor)->update_outputs (monitor);
}
