/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_COMPOSITOR_PRIVATE_H
#define META_COMPOSITOR_PRIVATE_H

#include <X11/extensions/Xfixes.h>

#include <meta/compositor.h>
#include <meta/display.h>
#include "meta-plugin-manager.h"
#include "meta-window-actor-private.h"
#include <clutter/clutter.h>

struct _MetaCompositor
{
  MetaDisplay    *display;

  guint           pre_paint_func_id;
  guint           post_paint_func_id;

  guint           stage_presented_id;
  guint           stage_after_paint_id;

  gboolean xserver_uses_monotonic_clock;
  int64_t xserver_time_query_time_us;
  int64_t xserver_time_offset_us;

  guint           server_time_is_monotonic_time : 1;
  guint           no_mipmaps  : 1;

  ClutterActor          *stage, *window_group, *top_window_group, *feedback_group;
  ClutterActor          *background_actor;
  GList                 *windows;
  Window                 output;

  CoglContext           *context;

  MetaWindowActor       *top_window_actor;

  /* Used for unredirecting fullscreen windows */
  guint                  disable_unredirect_count;
  MetaWindow            *unredirected_window;

  gint                   switch_workspace_in_progress;

  MetaPluginManager *plugin_mgr;

  gboolean frame_has_updated_xsurfaces;
  gboolean have_x11_sync_object;
};

/* Wait 2ms after vblank before starting to draw next frame */
#define META_SYNC_DELAY 2

void meta_switch_workspace_completed (MetaCompositor *compositor);

gboolean meta_begin_modal_for_plugin (MetaCompositor   *compositor,
                                      MetaPlugin       *plugin,
                                      MetaModalOptions  options,
                                      guint32           timestamp);
void     meta_end_modal_for_plugin   (MetaCompositor   *compositor,
                                      MetaPlugin       *plugin,
                                      guint32           timestamp);

int64_t meta_compositor_monotonic_to_high_res_xserver_time (MetaCompositor *compositor,
                                                            int64_t         monotonic_time);

void meta_compositor_flash_window (MetaCompositor *compositor,
                                   MetaWindow     *window);

MetaCloseDialog * meta_compositor_create_close_dialog (MetaCompositor *compositor,
                                                       MetaWindow     *window);

MetaInhibitShortcutsDialog * meta_compositor_create_inhibit_shortcuts_dialog (MetaCompositor *compositor,
                                                                              MetaWindow     *window);

void meta_compositor_unmanage_window_actors (MetaCompositor *compositor);

static inline int64_t
us (int64_t us)
{
  return us;
}

static inline int64_t
ms2us (int64_t ms)
{
  return us (ms * 1000);
}

static inline int64_t
s2us (int64_t s)
{
  return ms2us(s * 1000);
}

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

#endif /* META_COMPOSITOR_PRIVATE_H */
