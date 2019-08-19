/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_COMPOSITOR_PRIVATE_H
#define META_COMPOSITOR_PRIVATE_H

#include <X11/extensions/Xfixes.h>

#include "clutter/clutter.h"
#include "compositor/meta-plugin-manager.h"
#include "compositor/meta-window-actor-private.h"
#include "meta/compositor.h"
#include "meta/display.h"

/* Wait 2ms after vblank before starting to draw next frame */
#define META_SYNC_DELAY 2

struct _MetaCompositorClass
{
  GObjectClass parent_class;

  void (* manage) (MetaCompositor *compositor);
  void (* unmanage) (MetaCompositor *compositor);
  void (* pre_paint) (MetaCompositor *compositor);
  void (* post_paint) (MetaCompositor *compositor);
  void (* remove_window) (MetaCompositor *compositor,
                          MetaWindow     *window);
};

void meta_compositor_remove_window_actor (MetaCompositor  *compositor,
                                          MetaWindowActor *window_actor);

void meta_switch_workspace_completed (MetaCompositor *compositor);

gboolean meta_begin_modal_for_plugin (MetaCompositor   *compositor,
                                      MetaPlugin       *plugin,
                                      MetaModalOptions  options,
                                      guint32           timestamp);
void     meta_end_modal_for_plugin   (MetaCompositor   *compositor,
                                      MetaPlugin       *plugin,
                                      guint32           timestamp);

MetaPluginManager * meta_compositor_get_plugin_manager (MetaCompositor *compositor);

gint64 meta_compositor_monotonic_time_to_server_time (MetaDisplay *display,
                                                      gint64       monotonic_time);

void meta_compositor_flash_window (MetaCompositor *compositor,
                                   MetaWindow     *window);

MetaCloseDialog * meta_compositor_create_close_dialog (MetaCompositor *compositor,
                                                       MetaWindow     *window);

MetaInhibitShortcutsDialog * meta_compositor_create_inhibit_shortcuts_dialog (MetaCompositor *compositor,
                                                                              MetaWindow     *window);

void meta_compositor_locate_pointer (MetaCompositor *compositor);

void meta_compositor_redirect_x11_windows (MetaCompositor *compositor);

gboolean meta_compositor_is_unredirect_inhibited (MetaCompositor *compositor);

MetaDisplay * meta_compositor_get_display (MetaCompositor *compositor);

MetaWindowActor * meta_compositor_get_top_window_actor (MetaCompositor *compositor);

ClutterStage * meta_compositor_get_stage (MetaCompositor *compositor);

gboolean meta_compositor_is_switching_workspace (MetaCompositor *compositor);

#endif /* META_COMPOSITOR_PRIVATE_H */
