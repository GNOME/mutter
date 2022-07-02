/*
 * Copyright (C) 2019 Red Hat
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

#include <glib.h>
#include <stdint.h>

#include "backends/meta-output.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-plane-private.h"
#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-kms-types-private.h"
#include "backends/native/meta-kms-update.h"

typedef struct _MetaKmsCrtcColorUpdate
{
  MetaKmsCrtc *crtc;

  struct {
    gboolean has_update;
    MetaGammaLut *state;
  } gamma;
} MetaKmsCrtcColorUpdate;

typedef struct _MetaKmsFeedback
{
  gatomicrefcount ref_count;

  MetaKmsFeedbackResult result;

  GList *failed_planes;
  GError *error;
} MetaKmsFeedback;

typedef struct _MetaKmsFbDamage
{
  struct drm_mode_rect *rects;
  int n_rects;
} MetaKmsFbDamage;

typedef struct _MetaKmsPlaneAssignment
{
  MetaKmsUpdate *update;
  MetaKmsCrtc *crtc;
  MetaKmsPlane *plane;
  MetaDrmBuffer *buffer;
  MetaFixed16Rectangle src_rect;
  MtkRectangle dst_rect;
  MetaKmsAssignPlaneFlag flags;
  MetaKmsFbDamage *fb_damage;
  MetaKmsPlaneRotation rotation;

  struct {
    gboolean has_update;
    gboolean is_valid;
    int x;
    int y;
  } cursor_hotspot;
} MetaKmsPlaneAssignment;

typedef struct _MetaKmsModeSet
{
  MetaKmsCrtc *crtc;
  GList *connectors;
  MetaKmsMode *mode;
} MetaKmsModeSet;

typedef struct _MetaKmsConnectorUpdate
{
  MetaKmsConnector *connector;

  struct {
    gboolean has_update;
    gboolean is_active;
    uint64_t hborder;
    uint64_t vborder;
  } underscanning;

  struct {
    gboolean has_update;
    gboolean is_enabled;
  } privacy_screen;

  struct {
    gboolean has_update;
    uint64_t value;
  } max_bpc;

  struct {
    gboolean has_update;
    MetaOutputColorspace value;
  } colorspace;

  struct {
    gboolean has_update;
    MetaOutputHdrMetadata value;
  } hdr;

  struct {
    gboolean has_update;
    MetaOutputRGBRange value;
  } broadcast_rgb;
} MetaKmsConnectorUpdate;

typedef struct _MetaKmsCrtcUpdate
{
  MetaKmsCrtc *crtc;

  struct {
    gboolean has_update;
    gboolean is_enabled;
  } vrr;
} MetaKmsCrtcUpdate;

typedef struct _MetaKmsPageFlipListener
{
  gatomicrefcount ref_count;

  MetaKmsCrtc *crtc;
  const MetaKmsPageFlipListenerVtable *vtable;
  GMainContext *main_context;
  gpointer user_data;
  GDestroyNotify destroy_notify;
} MetaKmsPageFlipListener;

struct _MetaKmsResultListener
{
  GMainContext *main_context;
  const MetaKmsResultListenerVtable *vtable;
  gpointer user_data;
  GDestroyNotify destroy_notify;

  MetaKmsFeedback *feedback;
};

typedef struct _MetaKmsCustomPageFlip
{
  MetaKmsCustomPageFlipFunc func;
  gpointer user_data;
} MetaKmsCustomPageFlip;

void meta_kms_plane_feedback_free (MetaKmsPlaneFeedback *plane_feedback);

MetaKmsPlaneFeedback * meta_kms_plane_feedback_new_take_error (MetaKmsPlane *plane,
                                                               MetaKmsCrtc  *crtc,
                                                               GError       *error);

MetaKmsPlaneFeedback * meta_kms_plane_feedback_new_failed (MetaKmsPlane *plane,
                                                           MetaKmsCrtc  *crtc,
                                                           const char   *error_message);

MetaKmsFeedback * meta_kms_feedback_new_passed (GList *failed_planes);

MetaKmsFeedback * meta_kms_feedback_new_failed (GList  *failed_planes,
                                                GError *error);

void meta_kms_plane_assignment_set_rotation (MetaKmsPlaneAssignment *plane_assignment,
                                             MetaKmsPlaneRotation    rotation);

META_EXPORT_TEST
MetaKmsPlaneAssignment * meta_kms_update_get_primary_plane_assignment (MetaKmsUpdate *update,
                                                                       MetaKmsCrtc   *crtc);

META_EXPORT_TEST
MetaKmsPlaneAssignment * meta_kms_update_get_cursor_plane_assignment (MetaKmsUpdate *update,
                                                                      MetaKmsCrtc   *crtc);

META_EXPORT_TEST
GList * meta_kms_update_get_plane_assignments (MetaKmsUpdate *update);

META_EXPORT_TEST
GList * meta_kms_update_get_mode_sets (MetaKmsUpdate *update);

META_EXPORT_TEST
GList * meta_kms_update_get_page_flip_listeners (MetaKmsUpdate *update);

META_EXPORT_TEST
GList * meta_kms_update_get_connector_updates (MetaKmsUpdate *update);

META_EXPORT_TEST
GList * meta_kms_update_get_crtc_updates (MetaKmsUpdate *update);

META_EXPORT_TEST
GList * meta_kms_update_get_crtc_color_updates (MetaKmsUpdate *update);

MetaKmsCustomPageFlip * meta_kms_update_take_custom_page_flip_func (MetaKmsUpdate *update);

META_EXPORT_TEST
GList * meta_kms_update_take_result_listeners (MetaKmsUpdate *update);

GMainContext * meta_kms_result_listener_get_main_context (MetaKmsResultListener *listener);

void meta_kms_result_listener_set_feedback (MetaKmsResultListener *listener,
                                            MetaKmsFeedback       *feedback);

void meta_kms_result_listener_notify (MetaKmsResultListener *listener);

void meta_kms_result_listener_free (MetaKmsResultListener *listener);

void meta_kms_custom_page_flip_free (MetaKmsCustomPageFlip *custom_page_flip);

void meta_kms_update_realize (MetaKmsUpdate     *update,
                              MetaKmsImplDevice *impl_device);

gboolean meta_kms_update_get_needs_modeset (MetaKmsUpdate *update);

MetaKmsCrtc * meta_kms_update_get_latch_crtc (MetaKmsUpdate *update);

void meta_kms_page_flip_listener_unref (MetaKmsPageFlipListener *listener);

gboolean meta_kms_update_is_empty (MetaKmsUpdate *update);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsPlaneFeedback,
                               meta_kms_plane_feedback_free)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsCustomPageFlip,
                               meta_kms_custom_page_flip_free)
