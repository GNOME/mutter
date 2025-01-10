/*
 * Copyright (C) 2013 Red Hat, Inc.
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
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#include "backends/meta-monitor-manager-private.h"
#include "clutter/clutter.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-surface-actor.h"
#include "meta/meta-cursor-tracker.h"
#include "meta/meta-wayland-surface.h"
#include "wayland/meta-wayland-pointer-constraints.h"
#include "wayland/meta-wayland-types.h"
#include "wayland/meta-wayland-linux-drm-syncobj.h"

#define META_TYPE_WAYLAND_SURFACE_ROLE (meta_wayland_surface_role_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaWaylandSurfaceRole, meta_wayland_surface_role,
                          META, WAYLAND_SURFACE_ROLE, GObject);

#define META_TYPE_WAYLAND_SURFACE_STATE (meta_wayland_surface_state_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandSurfaceState,
                      meta_wayland_surface_state,
                      META, WAYLAND_SURFACE_STATE,
                      GObject)

struct _MetaWaylandSurfaceRoleClass
{
  GObjectClass parent_class;

  void (*assigned) (MetaWaylandSurfaceRole *surface_role);
  void (*commit_state) (MetaWaylandSurfaceRole  *surface_role,
                        MetaWaylandTransaction  *transaction,
                        MetaWaylandSurfaceState *pending);
  void (*pre_apply_state) (MetaWaylandSurfaceRole  *surface_role,
                           MetaWaylandSurfaceState *pending);
  void (*apply_state) (MetaWaylandSurfaceRole  *surface_role,
                       MetaWaylandSurfaceState *pending);
  void (*post_apply_state) (MetaWaylandSurfaceRole  *surface_role,
                            MetaWaylandSurfaceState *pending);
  gboolean (*is_on_logical_monitor) (MetaWaylandSurfaceRole *surface_role,
                                     MetaLogicalMonitor     *logical_monitor);
  MetaWaylandSurface * (*get_toplevel) (MetaWaylandSurfaceRole *surface_role);
  gboolean (*is_synchronized) (MetaWaylandSurfaceRole *surface_role);
  void (*notify_subsurface_state_changed) (MetaWaylandSurfaceRole *surface_role);
  void (*get_relative_coordinates) (MetaWaylandSurfaceRole *surface_role,
                                    float                   abs_x,
                                    float                   abs_y,
                                    float                  *out_sx,
                                    float                  *out_sy);
  MetaWindow * (*get_window) (MetaWaylandSurfaceRole *surface_role);
};

struct _MetaWaylandSurfaceState
{
  GObject parent;

  /* wl_surface.attach */
  gboolean newly_attached;
  MetaWaylandBuffer *buffer;
  MetaMultiTexture *texture;
  gulong buffer_destroy_handler_id;
  int32_t dx;
  int32_t dy;

  int scale;

  /* wl_surface.damage */
  MtkRegion *surface_damage;
  /* wl_surface.damage_buffer */
  MtkRegion *buffer_damage;

  MtkRegion *input_region;
  gboolean input_region_set;
  MtkRegion *opaque_region;
  gboolean opaque_region_set;

  /* wl_surface.frame */
  struct wl_list frame_callback_list;

  MtkRectangle new_geometry;
  gboolean has_new_geometry;

  gboolean has_acked_configure_serial;
  uint32_t acked_configure_serial;

  /* pending min/max size in window geometry coordinates */
  gboolean has_new_min_size;
  int new_min_width;
  int new_min_height;
  gboolean has_new_max_size;
  int new_max_width;
  int new_max_height;

  gboolean has_new_buffer_transform;
  MtkMonitorTransform buffer_transform;
  gboolean has_new_viewport_src_rect;
  graphene_rect_t viewport_src_rect;
  gboolean has_new_viewport_dst_size;
  int viewport_dst_width;
  int viewport_dst_height;

  GSList *subsurface_placement_ops;

  /* presentation-time */
  struct wl_list presentation_feedback_list;

  struct {
    gboolean surface_size_changed;
  } derived;

  /* xdg_popup */
  MetaWaylandXdgPositioner *xdg_positioner;
  uint32_t xdg_popup_reposition_token;

  /* Explicit Synchronization */
  struct {
    MetaWaylandSyncPoint *acquire;
    MetaWaylandSyncPoint *release;
  } drm_syncobj;

  gboolean has_new_color_state;
  ClutterColorState *color_state;
};

struct _MetaWaylandDragDestFuncs
{
  void (* focus_in)  (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface,
                      MetaWaylandDataOffer  *offer);
  void (* focus_out) (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface);
  void (* motion)    (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface,
                      float                  x,
                      float                  y,
                      uint32_t               time_ms);
  void (* drop)      (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface);
  void (* update)    (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface);
};

struct _MetaWaylandSurface
{
  GObject parent;

  /* Generic stuff */
  struct wl_resource *resource;
  MetaWaylandCompositor *compositor;
  MetaWaylandSurfaceRole *role;
  MtkRegion *input_region;
  MtkRegion *opaque_region;
  int32_t offset_x, offset_y;
  GHashTable *outputs;
  MtkMonitorTransform buffer_transform;

  int preferred_scale;
  MtkMonitorTransform preferred_transform;

  /* Buffer reference state. */
  MetaWaylandBuffer *buffer;

  /* Buffer renderer state. */
  gboolean buffer_held;

  /* Intermediate state for when no role has been assigned. */
  struct {
    struct wl_list pending_frame_callback_list;
    MetaWaylandBuffer *buffer;
  } unassigned;

  struct {
    const MetaWaylandDragDestFuncs *funcs;
  } dnd;

  /* All the pending state that wl_surface.commit will apply. */
  MetaWaylandSurfaceState *pending_state;

  struct MetaWaylandSurfaceSubState {
    MetaWaylandSurface *parent;
    GNode *subsurface_branch_node;
    GNode *subsurface_leaf_node;
    MetaMultiTexture *texture;
    int scale;
    gboolean is_valid;
  } applied_state, committed_state;

  /* Extension resources. */
  struct wl_resource *wl_subsurface;

  /* wl_subsurface stuff. */
  struct {
    int x;
    int y;

    /* When the surface is synchronous, its state will be applied
     * when the parent is committed. This is done by moving the
     * "real" pending state below to here when this surface is
     * committed and in synchronous mode.
     *
     * When the parent surface is committed, we apply the pending
     * state here.
     */
    gboolean synchronous;

    /* Transaction which contains all synchronized state for this sub-surface.
     * This can include state for nested sub-surfaces.
     */
    MetaWaylandTransaction *transaction;
  } sub;

  /* wp_viewport */
  struct {
    struct wl_resource *resource;
    gulong destroy_handler_id;

    gboolean has_src_rect;
    graphene_rect_t src_rect;

    gboolean has_dst_size;
    int dst_width;
    int dst_height;
  } viewport;

  /* wp_fractional_scale */
  struct {
    struct wl_resource *resource;
    gulong destroy_handler_id;

    double scale;
  } fractional_scale;

  /* table of seats for which shortcuts are inhibited */
  GHashTable *shortcut_inhibited_seats;

  /* presentation-time */
  struct {
    struct wl_list feedback_list;
    MetaWaylandOutput *last_output;
    unsigned int last_output_sequence;
    gboolean is_last_output_sequence_valid;
    gboolean needs_sequence_update;

    /*
     * Sequence has an undefined base, but is guaranteed to monotonically
     * increase. DRM only gives us a 32-bit sequence, so we compute our own
     * delta to update our own 64-bit sequence.
     */
    uint64_t sequence;
  } presentation_time;

  /* dma-buf feedback */
  MetaCrtc *scanout_candidate;

  /* Transactions */
  struct {
    /* First & last committed transaction which has an entry for this surface */
    MetaWaylandTransaction *first_committed;
    MetaWaylandTransaction *last_committed;
  } transaction;

  MetaLogicalMonitor *main_monitor;

  /* color-management */
  ClutterColorState *color_state;
};

void                meta_wayland_shell_init     (MetaWaylandCompositor *compositor);

MetaWaylandSurface *meta_wayland_surface_create (MetaWaylandCompositor *compositor,
                                                 struct wl_client      *client,
                                                 struct wl_resource    *compositor_resource,
                                                 guint32                id);

void                meta_wayland_surface_state_reset (MetaWaylandSurfaceState *state);

void                meta_wayland_surface_state_merge_into (MetaWaylandSurfaceState *from,
                                                           MetaWaylandSurfaceState *to);

void                meta_wayland_surface_apply_placement_ops (MetaWaylandSurface      *surface,
                                                              MetaWaylandSurfaceState *state);

void                meta_wayland_surface_apply_state (MetaWaylandSurface      *surface,
                                                      MetaWaylandSurfaceState *state);

MetaWaylandSurfaceState *
                    meta_wayland_surface_get_pending_state (MetaWaylandSurface *surface);

MetaWaylandTransaction *
                    meta_wayland_surface_ensure_transaction (MetaWaylandSurface *surface);

gboolean            meta_wayland_surface_assign_role (MetaWaylandSurface *surface,
                                                      GType               role_type,
                                                      const char         *first_property_name,
                                                      ...);

MetaWaylandBuffer  *meta_wayland_surface_get_buffer (MetaWaylandSurface *surface);

void                meta_wayland_surface_configure_notify (MetaWaylandSurface             *surface,
                                                           MetaWaylandWindowConfiguration *configuration);

void                meta_wayland_surface_ping (MetaWaylandSurface *surface,
                                               guint32             serial);
void                meta_wayland_surface_delete (MetaWaylandSurface *surface);

/* Drag dest functions */
void                meta_wayland_surface_drag_dest_focus_in  (MetaWaylandSurface   *surface,
                                                              MetaWaylandDataOffer *offer);
void                meta_wayland_surface_drag_dest_motion    (MetaWaylandSurface   *surface,
                                                              float                 x,
                                                              float                 y,
                                                              uint32_t              time_ms);
void                meta_wayland_surface_drag_dest_focus_out (MetaWaylandSurface   *surface);
void                meta_wayland_surface_drag_dest_drop      (MetaWaylandSurface   *surface);
void                meta_wayland_surface_drag_dest_update    (MetaWaylandSurface   *surface);

double              meta_wayland_surface_get_highest_output_scale (MetaWaylandSurface *surface);

void                meta_wayland_surface_update_outputs (MetaWaylandSurface *surface);

MetaWaylandSurface *meta_wayland_surface_get_toplevel (MetaWaylandSurface *surface);

gboolean            meta_wayland_surface_is_synchronized (MetaWaylandSurface *surface);

MetaWindow *        meta_wayland_surface_get_toplevel_window (MetaWaylandSurface *surface);

void                meta_wayland_surface_get_relative_coordinates (MetaWaylandSurface *surface,
                                                                   float               abs_x,
                                                                   float               abs_y,
                                                                   float              *sx,
                                                                   float              *sy);

void                meta_wayland_surface_get_absolute_coordinates (MetaWaylandSurface *surface,
                                                                   float               sx,
                                                                   float               sy,
                                                                   float              *x,
                                                                   float              *y);

MetaWaylandSurface * meta_wayland_surface_role_get_surface (MetaWaylandSurfaceRole *role);

MtkRegion * meta_wayland_surface_calculate_input_region (MetaWaylandSurface *surface);


gboolean            meta_wayland_surface_begin_grab_op (MetaWaylandSurface   *surface,
                                                        MetaWaylandSeat      *seat,
                                                        MetaGrabOp            grab_op,
                                                        ClutterInputDevice   *device,
                                                        ClutterEventSequence *sequence,
                                                        gfloat                x,
                                                        gfloat                y);

void                meta_wayland_surface_window_managed (MetaWaylandSurface *surface,
                                                         MetaWindow         *window);

void                meta_wayland_surface_inhibit_shortcuts (MetaWaylandSurface *surface,
                                                            MetaWaylandSeat    *seat);

void                meta_wayland_surface_restore_shortcuts (MetaWaylandSurface *surface,
                                                            MetaWaylandSeat    *seat);

gboolean            meta_wayland_surface_is_shortcuts_inhibited (MetaWaylandSurface *surface,
                                                                 MetaWaylandSeat    *seat);

MetaMultiTexture *  meta_wayland_surface_get_texture (MetaWaylandSurface *surface);

META_EXPORT_TEST
MetaSurfaceActor *  meta_wayland_surface_get_actor (MetaWaylandSurface *surface);

void                meta_wayland_surface_notify_geometry_changed (MetaWaylandSurface *surface);

void                meta_wayland_surface_notify_subsurface_state_changed (MetaWaylandSurface *surface);

void                meta_wayland_surface_notify_unmapped (MetaWaylandSurface *surface);

META_EXPORT_TEST
int                 meta_wayland_surface_get_width (MetaWaylandSurface *surface);

META_EXPORT_TEST
int                 meta_wayland_surface_get_height (MetaWaylandSurface *surface);

META_EXPORT_TEST
int                 meta_wayland_surface_get_buffer_width (MetaWaylandSurface *surface);

META_EXPORT_TEST
int                 meta_wayland_surface_get_buffer_height (MetaWaylandSurface *surface);

CoglScanout *       meta_wayland_surface_try_acquire_scanout (MetaWaylandSurface *surface,
                                                              CoglOnscreen       *onscreen,
                                                              ClutterStageView   *stage_view);

MetaCrtc * meta_wayland_surface_get_scanout_candidate (MetaWaylandSurface *surface);

void meta_wayland_surface_set_scanout_candidate (MetaWaylandSurface *surface,
                                                 MetaCrtc           *crtc);

int meta_wayland_surface_get_geometry_scale (MetaWaylandSurface *surface);

META_EXPORT_TEST
struct wl_resource * meta_wayland_surface_get_resource (MetaWaylandSurface *surface);

MetaWaylandCompositor * meta_wayland_surface_get_compositor (MetaWaylandSurface *surface);

void meta_wayland_surface_notify_preferred_scale_monitor (MetaWaylandSurface *surface);

void meta_wayland_surface_notify_actor_changed (MetaWaylandSurface *surface);

void meta_wayland_surface_set_main_monitor (MetaWaylandSurface *surface,
                                            MetaLogicalMonitor *logical_monitor);

MetaLogicalMonitor * meta_wayland_surface_get_main_monitor (MetaWaylandSurface *surface);

static inline MetaWaylandSurfaceState *
meta_wayland_surface_state_new (void)
{
  return g_object_new (META_TYPE_WAYLAND_SURFACE_STATE, NULL);
}
gboolean meta_wayland_surface_is_xwayland (MetaWaylandSurface *surface);

gboolean meta_wayland_surface_has_initial_commit (MetaWaylandSurface *surface);

static inline GNode *
meta_get_next_subsurface_sibling (GNode *n)
{
  GNode *next;

  if (!n)
    return NULL;

  next = g_node_next_sibling (n);
  if (!next)
    return NULL;
  if (!G_NODE_IS_LEAF (next))
    return next;
  else
    return meta_get_next_subsurface_sibling (next);
}

static inline GNode *
meta_get_first_subsurface_node (struct MetaWaylandSurfaceSubState *sub)
{
  GNode *n;

  n = g_node_first_child (sub->subsurface_branch_node);
  if (!n)
    return NULL;
  else if (!G_NODE_IS_LEAF (n))
    return n;
  else
    return meta_get_next_subsurface_sibling (n);
}

#define META_WAYLAND_SURFACE_FOREACH_SUBSURFACE(state, subsurface) \
  for (GNode *G_PASTE(__n, __LINE__) = meta_get_first_subsurface_node (state), \
       *G_PASTE(__next, __LINE__) = meta_get_next_subsurface_sibling (G_PASTE (__n, __LINE__)); \
       (subsurface = (G_PASTE (__n, __LINE__) ? G_PASTE (__n, __LINE__)->data : NULL)); \
       G_PASTE (__n, __LINE__) = G_PASTE (__next, __LINE__), \
       G_PASTE (__next, __LINE__) = meta_get_next_subsurface_sibling (G_PASTE (__n, __LINE__)))
