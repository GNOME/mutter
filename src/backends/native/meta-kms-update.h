/*
 * Copyright (C) 2018 Red Hat
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

#pragma once

#include <glib-object.h>
#include <glib.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "backends/meta-monitor-transform.h"
#include "backends/meta-output.h"
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-kms-types.h"
#include "meta/boxes.h"

typedef enum _MetaKmsFeedbackResult
{
  META_KMS_FEEDBACK_PASSED,
  META_KMS_FEEDBACK_FAILED,
} MetaKmsFeedbackResult;

typedef enum _MetaKmsAssignPlaneFlag
{
  META_KMS_ASSIGN_PLANE_FLAG_NONE = 0,
  META_KMS_ASSIGN_PLANE_FLAG_FB_UNCHANGED = 1 << 0,
  META_KMS_ASSIGN_PLANE_FLAG_ALLOW_FAIL = 1 << 1,
  META_KMS_ASSIGN_PLANE_FLAG_DIRECT_SCANOUT = 1 << 2,
} MetaKmsAssignPlaneFlag;

struct _MetaKmsPageFlipListenerVtable
{
  void (* flipped) (MetaKmsCrtc  *crtc,
                    unsigned int  sequence,
                    unsigned int  tv_sec,
                    unsigned int  tv_usec,
                    gpointer      user_data);

  void (* ready) (MetaKmsCrtc *crtc,
                  gpointer     user_data);

  void (* mode_set_fallback) (MetaKmsCrtc *crtc,
                              gpointer     user_data);

  void (* discarded) (MetaKmsCrtc  *crtc,
                      gpointer      user_data,
                      const GError *error);
};

typedef int (* MetaKmsCustomPageFlipFunc) (gpointer custom_page_flip_data,
                                           gpointer user_data);

typedef struct _MetaKmsPlaneFeedback
{
  MetaKmsPlane *plane;
  MetaKmsCrtc *crtc;
  GError *error;
} MetaKmsPlaneFeedback;

typedef struct _MetaKmsResultListenerVtable
{
  void (* feedback) (const MetaKmsFeedback *feedback,
                     gpointer               user_data);
} MetaKmsResultListenerVtable;

MetaKmsFeedback * meta_kms_feedback_ref (MetaKmsFeedback *feedback);

META_EXPORT_TEST
void meta_kms_feedback_unref (MetaKmsFeedback *feedback);

MetaKmsFeedbackResult meta_kms_feedback_get_result (const MetaKmsFeedback *feedback);

gboolean meta_kms_feedback_did_pass (const MetaKmsFeedback *feedback);

GList * meta_kms_feedback_get_failed_planes (const MetaKmsFeedback *feedback);

const GError * meta_kms_feedback_get_error (const MetaKmsFeedback *feedback);

META_EXPORT_TEST
void meta_kms_feedback_dispatch_result (MetaKmsFeedback *feedback,
                                        MetaKms         *kms,
                                        GList           *result_listeners);

META_EXPORT_TEST
MetaKmsUpdate * meta_kms_update_new (MetaKmsDevice *device);

META_EXPORT_TEST
void meta_kms_update_free (MetaKmsUpdate *update);

void meta_kms_update_set_flushing (MetaKmsUpdate *update,
                                   MetaKmsCrtc   *crtc);

META_EXPORT_TEST
MetaKmsDevice * meta_kms_update_get_device (MetaKmsUpdate *update);

META_EXPORT_TEST
void meta_kms_update_set_underscanning (MetaKmsUpdate    *update,
                                        MetaKmsConnector *connector,
                                        uint64_t          hborder,
                                        uint64_t          vborder);

void meta_kms_update_unset_underscanning (MetaKmsUpdate    *update,
                                          MetaKmsConnector *connector);

META_EXPORT_TEST
void meta_kms_update_set_privacy_screen (MetaKmsUpdate    *update,
                                         MetaKmsConnector *connector,
                                         gboolean          enabled);

META_EXPORT_TEST
void meta_kms_update_set_max_bpc (MetaKmsUpdate    *update,
                                  MetaKmsConnector *connector,
                                  uint64_t          max_bpc);

void meta_kms_update_set_color_space (MetaKmsUpdate        *update,
                                      MetaKmsConnector     *connector,
                                      MetaOutputColorspace  color_space);

void meta_kms_update_set_hdr_metadata (MetaKmsUpdate         *update,
                                       MetaKmsConnector      *connector,
                                       MetaOutputHdrMetadata *metadata);

void meta_kms_update_set_broadcast_rgb (MetaKmsUpdate      *update,
                                        MetaKmsConnector   *connector,
                                        MetaOutputRGBRange  rgb_range);

META_EXPORT_TEST
void meta_kms_update_set_power_save (MetaKmsUpdate *update);

META_EXPORT_TEST
void meta_kms_update_mode_set (MetaKmsUpdate *update,
                               MetaKmsCrtc   *crtc,
                               GList         *connectors,
                               MetaKmsMode   *mode);

META_EXPORT_TEST
void meta_kms_update_set_crtc_gamma (MetaKmsUpdate      *update,
                                     MetaKmsCrtc        *crtc,
                                     const MetaGammaLut *gamma);

void meta_kms_plane_assignment_set_fb_damage (MetaKmsPlaneAssignment *plane_assignment,
                                              const int              *rectangles,
                                              int                     n_rectangles);

META_EXPORT_TEST
MetaKmsPlaneAssignment * meta_kms_update_assign_plane (MetaKmsUpdate          *update,
                                                       MetaKmsCrtc            *crtc,
                                                       MetaKmsPlane           *plane,
                                                       MetaDrmBuffer          *buffer,
                                                       MetaFixed16Rectangle    src_rect,
                                                       MtkRectangle            dst_rect,
                                                       MetaKmsAssignPlaneFlag  flags);

MetaKmsPlaneAssignment * meta_kms_update_unassign_plane (MetaKmsUpdate *update,
                                                         MetaKmsCrtc   *crtc,
                                                         MetaKmsPlane  *plane);

META_EXPORT_TEST
void meta_kms_update_add_page_flip_listener (MetaKmsUpdate                       *update,
                                             MetaKmsCrtc                         *crtc,
                                             const MetaKmsPageFlipListenerVtable *vtable,
                                             GMainContext                        *main_context,
                                             gpointer                             user_data,
                                             GDestroyNotify                       destroy_notify);

void meta_kms_update_set_custom_page_flip (MetaKmsUpdate             *update,
                                           MetaKmsCustomPageFlipFunc  func,
                                           gpointer                   user_data);

META_EXPORT_TEST
void meta_kms_plane_assignment_set_cursor_hotspot (MetaKmsPlaneAssignment *plane_assignment,
                                                   int                     x,
                                                   int                     y);

META_EXPORT_TEST
void meta_kms_update_add_result_listener (MetaKmsUpdate                     *update,
                                          const MetaKmsResultListenerVtable *vtable,
                                          GMainContext                      *main_context,
                                          gpointer                           user_data,
                                          GDestroyNotify                     destroy_notify);

META_EXPORT_TEST
void meta_kms_update_merge_from (MetaKmsUpdate *update,
                                 MetaKmsUpdate *other_update);

static inline MetaFixed16
meta_fixed_16_from_int (int16_t d)
{
  return d * (1 << 16);
}

static inline int16_t
meta_fixed_16_to_int (MetaFixed16 fixed)
{
  return fixed / (1 << 16);
}

static inline MetaFixed16
meta_fixed_16_from_double (double d)
{
  return d * (1 << 16);
}

static inline double
meta_fixed_16_to_double (MetaFixed16 fixed)
{
  return fixed / (double) (1 << 16);
}

static inline MtkRectangle
meta_fixed_16_rectangle_to_rectangle (MetaFixed16Rectangle fixed_rect)
{
  return (MtkRectangle) {
    .x = meta_fixed_16_to_int (fixed_rect.x),
    .y = meta_fixed_16_to_int (fixed_rect.y),
    .width = meta_fixed_16_to_int (fixed_rect.width),
    .height = meta_fixed_16_to_int (fixed_rect.height),
  };
}

#define META_FIXED_16_RECTANGLE_INIT(_x,_y,_w,_h) \
  (MetaFixed16Rectangle) { .x = (_x), .y = (_y), .width = (_w), .height = (_h) }

#define META_FIXED_16_RECTANGLE_INIT_INT(_x,_y,_w,_h) \
  META_FIXED_16_RECTANGLE_INIT (meta_fixed_16_from_int (_x), \
                                meta_fixed_16_from_int (_y), \
                                meta_fixed_16_from_int (_w), \
                                meta_fixed_16_from_int (_h))

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsFeedback, meta_kms_feedback_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsUpdate, meta_kms_update_free)
