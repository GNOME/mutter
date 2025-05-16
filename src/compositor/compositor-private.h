/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#pragma once

#include <graphene.h>

#include "backends/meta-backend-private.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter.h"
#include "compositor/meta-compositor-view.h"
#include "compositor/meta-plugin-manager.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-window-drag.h"
#include "meta/compositor.h"
#include "meta/display.h"
#include "meta/prefs.h"

/* Wait 2ms after vblank before starting to draw next frame */
#define META_SYNC_DELAY 2

typedef enum _MetaMappingType MetaMappingType;

enum _MetaMappingType
{
  META_MAPPING_TYPE_BUTTON,
  META_MAPPING_TYPE_KEY,
};

typedef enum _MetaMappingState MetaMappingState;

enum _MetaMappingState
{
  META_MAPPING_STATE_PRE_CHANGE,
  META_MAPPING_STATE_POST_CHANGE,
};

typedef struct _MetaLaters MetaLaters;

struct _MetaCompositorClass
{
  GObjectClass parent_class;

  gboolean (* manage) (MetaCompositor  *compositor,
                       GError         **error);
  void (* unmanage) (MetaCompositor *compositor);
  void (* before_paint) (MetaCompositor     *compositor,
                         MetaCompositorView *compositor_view,
                         ClutterFrame       *frame);
  void (* after_paint) (MetaCompositor     *compositor,
                        MetaCompositorView *compositor_view,
                        ClutterFrame       *frame);
  void (* add_window) (MetaCompositor *compositor,
                       MetaWindow     *window);
  void (* remove_window) (MetaCompositor *compositor,
                          MetaWindow     *window);
  int64_t (* monotonic_to_high_res_xserver_time) (MetaCompositor *compositor,
                                                  int64_t         time_us);

  MetaCompositorView * (* create_view) (MetaCompositor   *compositor,
                                        ClutterStageView *stage_view);

  gboolean (* handle_event) (MetaCompositor     *compositor,
                             const ClutterEvent *event,
                             MetaWindow         *event_window,
                             MetaEventMode       mode_hint);

  void (* notify_mapping_change) (MetaCompositor   *compositor,
                                  MetaMappingType   type,
                                  MetaMappingState  state);
};

void meta_compositor_remove_window_actor (MetaCompositor  *compositor,
                                          MetaWindowActor *window_actor);

void meta_compositor_window_actor_stage_views_changed (MetaCompositor *compositor);

void meta_switch_workspace_completed (MetaCompositor *compositor);

META_EXPORT_TEST
MetaPluginManager * meta_compositor_get_plugin_manager (MetaCompositor *compositor);

int64_t meta_compositor_monotonic_to_high_res_xserver_time (MetaCompositor *compositor,
                                                            int64_t         monotonic_time_us);

void meta_compositor_flash_window (MetaCompositor *compositor,
                                   MetaWindow     *window,
                                   int             n_flashes);

MetaCloseDialog * meta_compositor_create_close_dialog (MetaCompositor *compositor,
                                                       MetaWindow     *window);

MetaInhibitShortcutsDialog * meta_compositor_create_inhibit_shortcuts_dialog (MetaCompositor *compositor,
                                                                              MetaWindow     *window);

void meta_compositor_locate_pointer (MetaCompositor *compositor);

gboolean meta_compositor_is_unredirect_inhibited (MetaCompositor *compositor);

MetaDisplay * meta_compositor_get_display (MetaCompositor *compositor);

MetaWindowActor * meta_compositor_get_top_window_actor (MetaCompositor *compositor);
gboolean meta_compositor_is_switching_workspace (MetaCompositor *compositor);

gboolean meta_compositor_drag_window (MetaCompositor      *compositor,
                                      MetaWindow          *window,
                                      MetaGrabOp           grab_op,
                                      MetaDragWindowFlags  flags,
                                      ClutterSprite       *sprite,
                                      uint32_t             timestamp,
                                      graphene_point_t    *pos_hint);

META_EXPORT_TEST
MetaWindowDrag * meta_compositor_get_current_window_drag (MetaCompositor *compositor);

void meta_compositor_grab_begin (MetaCompositor *compositor);

void meta_compositor_grab_end (MetaCompositor *compositor);

void meta_compositor_destroy (MetaCompositor *compositor);

gboolean meta_compositor_manage (MetaCompositor  *compositor,
                                 GVariant        *plugin_options,
                                 GError         **error);

void meta_compositor_unmanage (MetaCompositor *compositor);

void meta_compositor_window_shape_changed (MetaCompositor *compositor,
                                           MetaWindow     *window);

void meta_compositor_window_opacity_changed (MetaCompositor *compositor,
                                             MetaWindow     *window);

gboolean meta_compositor_filter_keybinding (MetaCompositor *compositor,
                                            MetaKeyBinding *binding);

void meta_compositor_add_window (MetaCompositor      *compositor,
                                 MetaWindow          *window);

void meta_compositor_remove_window (MetaCompositor      *compositor,
                                    MetaWindow          *window);

void meta_compositor_show_window (MetaCompositor      *compositor,
                                  MetaWindow          *window,
                                  MetaCompEffect       effect);

void meta_compositor_hide_window (MetaCompositor      *compositor,
                                  MetaWindow          *window,
                                  MetaCompEffect       effect);

void meta_compositor_switch_workspace (MetaCompositor      *compositor,
                                       MetaWorkspace       *from,
                                       MetaWorkspace       *to,
                                       MetaMotionDirection  direction);

void meta_compositor_size_change_window (MetaCompositor *compositor,
                                         MetaWindow     *window,
                                         MetaSizeChange  which_change,
                                         MtkRectangle   *old_frame_rect,
                                         MtkRectangle   *old_buffer_rect);

void meta_compositor_sync_window_geometry (MetaCompositor *compositor,
                                           MetaWindow     *window,
                                           gboolean        did_placement);

void meta_compositor_sync_updates_frozen (MetaCompositor *compositor,
                                          MetaWindow     *window);

void meta_compositor_queue_frame_drawn (MetaCompositor *compositor,
                                        MetaWindow     *window,
                                        gboolean        no_delay_frame);

void meta_compositor_sync_stack (MetaCompositor *compositor,
                                 GList          *stack);

void meta_compositor_flash_display (MetaCompositor *compositor,
                                    MetaDisplay    *display,
                                    int             n_flashes);

void meta_compositor_show_tile_preview (MetaCompositor *compositor,
                                        MetaWindow     *window,
                                        MtkRectangle   *tile_rect,
                                        int             tile_monitor_number);

void meta_compositor_hide_tile_preview (MetaCompositor *compositor);

void meta_compositor_show_window_menu (MetaCompositor     *compositor,
                                       MetaWindow         *window,
				       MetaWindowMenuType  menu,
                                       int                 x,
                                       int                 y);

gboolean meta_compositor_handle_event (MetaCompositor     *compositor,
                                       const ClutterEvent *event,
                                       MetaWindow         *event_window,
                                       MetaEventMode       mode_hint);

void meta_compositor_notify_mapping_change (MetaCompositor   *compositor,
                                            MetaMappingType   type,
                                            MetaMappingState  state);

/*
 * This function takes a 64 bit time stamp from the monotonic clock, and clamps
 * it to the scope of the X server clock, without losing the granularity.
 */
static inline int64_t
meta_translate_to_high_res_xserver_time (int64_t time_us)
{
  int64_t us;
  int64_t ms;

  us = time_us % 1000;
  ms = time_us / 1000;

  return ms2us (ms & 0xffffffff) + us;
}
