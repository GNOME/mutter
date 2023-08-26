/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011,2012,2013 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-context.h"
#include "cogl/cogl-framebuffer.h"
#include "cogl/cogl-frame-info.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define COGL_TYPE_ONSCREEN (cogl_onscreen_get_type ())
COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglOnscreen, cogl_onscreen,
                          COGL, ONSCREEN,
                          CoglFramebuffer)

struct _CoglOnscreenClass
{
  /*< private >*/
  CoglFramebufferClass parent_class;

  void (* bind) (CoglOnscreen *onscreen);

  void (* swap_buffers_with_damage) (CoglOnscreen  *onscreen,
                                     const int     *rectangles,
                                     int            n_rectangles,
                                     CoglFrameInfo *info,
                                     gpointer       user_data);

  void (* swap_region) (CoglOnscreen  *onscreen,
                        const int     *rectangles,
                        int            n_rectangles,
                        CoglFrameInfo *info,
                        gpointer       user_data);

  void (* queue_damage_region) (CoglOnscreen *onscreen,
                                const int    *rectangles,
                                int           n_rectangles);

  gboolean (* direct_scanout) (CoglOnscreen   *onscreen,
                               CoglScanout    *scanout,
                               CoglFrameInfo  *info,
                               gpointer        user_data,
                               GError        **error);

  int (* get_buffer_age) (CoglOnscreen *onscreen);
};

/**
 * cogl_onscreen_show:
 * @onscreen: The onscreen framebuffer to make visible
 *
 * This requests to make @onscreen visible to the user.
 *
 * Actually the precise semantics of this function depend on the
 * window system currently in use, and if you don't have a
 * multi-windowining system this function may in-fact do nothing.
 *
 * This function will implicitly allocate the given @onscreen
 * framebuffer before showing it if it hasn't already been allocated.
 *
 * When using the Wayland winsys calling this will set the surface to
 * a toplevel type which will make it appear. If the application wants
 * to set a different type for the surface, it can avoid calling
 * cogl_onscreen_show() and set its own type directly with the Wayland
 * client API via cogl_wayland_onscreen_get_surface().
 *
 * Since Cogl doesn't explicitly track the visibility status of
 * onscreen framebuffers it won't try to avoid redundant window system
 * requests e.g. to show an already visible window. This also means
 * that it's acceptable to alternatively use native APIs to show and
 * hide windows without confusing Cogl.
 */
COGL_EXPORT void
cogl_onscreen_show (CoglOnscreen *onscreen);

/**
 * cogl_onscreen_hide:
 * @onscreen: The onscreen framebuffer to make invisible
 *
 * This requests to make @onscreen invisible to the user.
 *
 * Actually the precise semantics of this function depend on the
 * window system currently in use, and if you don't have a
 * multi-windowining system this function may in-fact do nothing.
 *
 * This function does not implicitly allocate the given @onscreen
 * framebuffer before hiding it.
 *
 * Since Cogl doesn't explicitly track the visibility status of
 * onscreen framebuffers it won't try to avoid redundant window system
 * requests e.g. to show an already visible window. This also means
 * that it's acceptable to alternatively use native APIs to show and
 * hide windows without confusing Cogl.
 */
COGL_EXPORT void
cogl_onscreen_hide (CoglOnscreen *onscreen);

/**
 * cogl_onscreen_swap_buffers:
 * @onscreen: A #CoglOnscreen framebuffer
 *
 * Swaps the current back buffer being rendered too, to the front for display.
 *
 * This function also implicitly discards the contents of the color, depth and
 * stencil buffers as if cogl_framebuffer_discard_buffers() were used. The
 * significance of the discard is that you should not expect to be able to
 * start a new frame that incrementally builds on the contents of the previous
 * frame.
 *
 * It is highly recommended that applications use
 * cogl_onscreen_swap_buffers_with_damage() instead whenever possible
 * and also use the cogl_onscreen_get_buffer_age() api so they can
 * perform incremental updates to older buffers instead of having to
 * render a full buffer for every frame.
 */
COGL_EXPORT void
cogl_onscreen_swap_buffers (CoglOnscreen  *onscreen,
                            CoglFrameInfo *frame_info,
                            gpointer       user_data);


/**
 * cogl_onscreen_get_buffer_age:
 * @onscreen: A #CoglOnscreen framebuffer
 *
 * Gets the current age of the buffer contents.
 *
 * This function allows applications to query the age of the current
 * back buffer contents for a #CoglOnscreen as the number of frames
 * elapsed since the contents were most recently defined.
 *
 * These age values exposes enough information to applications about
 * how Cogl internally manages back buffers to allow applications to
 * re-use the contents of old frames and minimize how much must be
 * redrawn for the next frame.
 *
 * The back buffer contents can either be reported as invalid (has an
 * age of 0) or it may be reported to be the same contents as from n
 * frames prior to the current frame.
 *
 * The queried value remains valid until the next buffer swap.
 *
 * One caveat is that under X11 the buffer age does not reflect
 * changes to buffer contents caused by the window systems. X11
 * applications must track Expose events to determine what buffer
 * regions need to additionally be repaired each frame.
 *
 * The recommended way to take advantage of this buffer age api is to
 * build up a circular buffer of length 3 for tracking damage regions
 * over the last 3 frames and when starting a new frame look at the
 * age of the buffer and combine the damage regions for the current
 * frame with the damage regions of previous @age frames so you know
 * everything that must be redrawn to update the old contents for the
 * new frame.
 *
 * If the system doesn't not support being able to track the age
 * of back buffers then this function will always return 0 which
 * implies that the contents are undefined.
 *
 * The %COGL_FEATURE_ID_BUFFER_AGE feature can optionally be
 * explicitly checked to determine if Cogl is currently tracking the
 * age of #CoglOnscreen back buffer contents. If this feature is
 * missing then this function will always return 0.
 *
 * Return value: The age of the buffer contents or 0 when the buffer
 *               contents are undefined.
 */
COGL_EXPORT int
cogl_onscreen_get_buffer_age (CoglOnscreen *onscreen);

/**
 * cogl_onscreen_queue_damage_region:
 * @onscreen: A #CoglOnscreen framebuffer
 * @rectangles: (array length=n_rectangles): An array of integer 4-tuples
 *              representing damaged rectangles as (x, y, width, height) tuples.
 * @n_rectangles: The number of 4-tuples to be read from @rectangles
 *
 * Implementation for https://www.khronos.org/registry/EGL/extensions/KHR/EGL_KHR_partial_update.txt
 * This immediately queues state to OpenGL that will be used for the
 * next swap.
 * This needs to be called every frame.
 *
 * The expected values are independent of any viewport transforms applied to
 * the framebuffer.
 */
COGL_EXPORT void
cogl_onscreen_queue_damage_region (CoglOnscreen *onscreen,
                                   const int    *rectangles,
                                   int           n_rectangles);

/**
 * cogl_onscreen_swap_buffers_with_damage:
 * @onscreen: A #CoglOnscreen framebuffer
 * @rectangles: (array length=n_rectangles): An array of integer 4-tuples
 *              representing damaged rectangles as (x, y, width, height) tuples.
 * @n_rectangles: The number of 4-tuples to be read from @rectangles
 *
 * Swaps the current back buffer being rendered too, to the front for
 * display and provides information to any system compositor about
 * what regions of the buffer have changed (damage) with respect to
 * the last swapped buffer.
 *
 * This function has the same semantics as
 * cogl_framebuffer_swap_buffers() except that it additionally allows
 * applications to pass a list of damaged rectangles which may be
 * passed on to a compositor so that it can minimize how much of the
 * screen is redrawn in response to this applications newly swapped
 * front buffer.
 *
 * For example if your application is only animating a small object in
 * the corner of the screen and everything else is remaining static
 * then it can help the compositor to know that only the bottom right
 * corner of your newly swapped buffer has really changed with respect
 * to your previously swapped front buffer.
 *
 * If @n_rectangles is 0 then the whole buffer will implicitly be
 * reported as damaged as if cogl_onscreen_swap_buffers() had been
 * called.
 *
 * This function also implicitly discards the contents of the color,
 * depth and stencil buffers as if cogl_framebuffer_discard_buffers()
 * were used. The significance of the discard is that you should not
 * expect to be able to start a new frame that incrementally builds on
 * the contents of the previous frame. If you want to perform
 * incremental updates to older back buffers then please refer to the
 * cogl_onscreen_get_buffer_age() api.
 *
 * Whenever possible it is recommended that applications use this
 * function instead of cogl_onscreen_swap_buffers() to improve
 * performance when running under a compositor.
 *
 * It is highly recommended to use this API in conjunction with
 * the cogl_onscreen_get_buffer_age() api so that your application can
 * perform incremental rendering based on old back buffers.
 */
COGL_EXPORT void
cogl_onscreen_swap_buffers_with_damage (CoglOnscreen *onscreen,
                                        const int *rectangles,
                                        int n_rectangles,
                                        CoglFrameInfo *info,
                                        gpointer user_data);

/**
 * cogl_onscreen_direct_scanout: (skip)
 */
COGL_EXPORT gboolean
cogl_onscreen_direct_scanout (CoglOnscreen   *onscreen,
                              CoglScanout    *scanout,
                              CoglFrameInfo  *info,
                              gpointer        user_data,
                              GError        **error);

/**
 * cogl_onscreen_add_frame_info: (skip)
 */
COGL_EXPORT void
cogl_onscreen_add_frame_info (CoglOnscreen  *onscreen,
                              CoglFrameInfo *info);

/**
 * cogl_onscreen_swap_region:
 * @onscreen: A #CoglOnscreen framebuffer
 * @rectangles: (array length=n_rectangles): An array of integer 4-tuples
 *              representing rectangles as (x, y, width, height) tuples.
 * @n_rectangles: The number of 4-tuples to be read from @rectangles
 *
 * Swaps a region of the back buffer being rendered too, to the front for
 * display.  @rectangles represents the region as array of @n_rectangles each
 * defined by 4 sequential (x, y, width, height) integers.
 *
 * This function also implicitly discards the contents of the color, depth and
 * stencil buffers as if cogl_framebuffer_discard_buffers() were used. The
 * significance of the discard is that you should not expect to be able to
 * start a new frame that incrementally builds on the contents of the previous
 * frame.
 */
COGL_EXPORT void
cogl_onscreen_swap_region (CoglOnscreen *onscreen,
                           const int *rectangles,
                           int n_rectangles,
                           CoglFrameInfo *info,
                           gpointer user_data);

/**
 * CoglFrameEvent:
 * @COGL_FRAME_EVENT_SYNC: Notifies that the system compositor has
 *                         acknowledged a frame and is ready for a
 *                         new frame to be created.
 * @COGL_FRAME_EVENT_COMPLETE: Notifies that a frame has ended. This
 *                             is a good time for applications to
 *                             collect statistics about the frame
 *                             since the #CoglFrameInfo should hold
 *                             the most data at this point. No other
 *                             events should be expected after a
 *                             @COGL_FRAME_EVENT_COMPLETE event.
 *
 * Identifiers that are passed to #CoglFrameCallback functions
 * (registered using cogl_onscreen_add_frame_callback()) that
 * mark the progression of a frame in some way which usually
 * means that new information will have been accumulated in the
 * frame's corresponding #CoglFrameInfo object.
 *
 * The last event that will be sent for a frame will be a
 * @COGL_FRAME_EVENT_COMPLETE event and so these are a good
 * opportunity to collect statistics about a frame since the
 * #CoglFrameInfo should hold the most data at this point.
 *
 * A frame may not be completed before the next frame can start
 * so applications should avoid needing to collect all statistics for
 * a particular frame before they can start a new frame.
 */
typedef enum _CoglFrameEvent
{
  COGL_FRAME_EVENT_SYNC = 1,
  COGL_FRAME_EVENT_COMPLETE
} CoglFrameEvent;

/**
 * CoglFrameCallback:
 * @onscreen: The onscreen that the frame is associated with
 * @event: A #CoglFrameEvent notifying how the frame has progressed
 * @info: The meta information, such as timing information, about
 *        the frame that has progressed.
 * @user_data: The user pointer passed to
 *             cogl_onscreen_add_frame_callback()
 *
 * Is a callback that can be registered via
 * cogl_onscreen_add_frame_callback() to be called when a frame
 * progresses in some notable way.
 *
 * Please see the documentation for #CoglFrameEvent and
 * cogl_onscreen_add_frame_callback() for more details about what
 * events can be notified.
 */
typedef void (*CoglFrameCallback) (CoglOnscreen *onscreen,
                                   CoglFrameEvent event,
                                   CoglFrameInfo *info,
                                   void *user_data);

/**
 * CoglFrameClosure:
 *
 * An opaque type that tracks a #CoglFrameCallback and associated user
 * data. A #CoglFrameClosure pointer will be returned from
 * cogl_onscreen_add_frame_callback() and it allows you to remove a
 * callback later using cogl_onscreen_remove_frame_callback().
 */
typedef struct _CoglClosure CoglFrameClosure;

/**
 * cogl_frame_closure_get_type:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_frame_closure_get_type (void);

/**
 * cogl_onscreen_add_frame_callback:
 * @onscreen: A #CoglOnscreen framebuffer
 * @callback: (scope notified): A callback function to call for frame events
 * @user_data: (closure): A private pointer to be passed to @callback
 * @destroy: (allow-none): An optional callback to destroy @user_data
 *           when the @callback is removed or @onscreen is freed.
 *
 * Installs a @callback function that will be called for significant
 * events relating to the given @onscreen framebuffer.
 *
 * The @callback will be used to notify when the system compositor is
 * ready for this application to render a new frame. In this case
 * %COGL_FRAME_EVENT_SYNC will be passed as the event argument to the
 * given @callback in addition to the #CoglFrameInfo corresponding to
 * the frame being acknowledged by the compositor.
 *
 * The @callback will also be called to notify when the frame has
 * ended. In this case %COGL_FRAME_EVENT_COMPLETE will be passed as
 * the event argument to the given @callback in addition to the
 * #CoglFrameInfo corresponding to the newly presented frame.  The
 * meaning of "ended" here simply means that no more timing
 * information will be collected within the corresponding
 * #CoglFrameInfo and so this is a good opportunity to analyse the
 * given info. It does not necessarily mean that the GPU has finished
 * rendering the corresponding frame.
 *
 * We highly recommend throttling your application according to
 * %COGL_FRAME_EVENT_SYNC events so that your application can avoid
 * wasting resources, drawing more frames than your system compositor
 * can display.
 *
 * Returns: (transfer none): a #CoglFrameClosure pointer that can be used to
 *          remove the callback and associated @user_data later.
 */
COGL_EXPORT CoglFrameClosure *
cogl_onscreen_add_frame_callback (CoglOnscreen *onscreen,
                                  CoglFrameCallback callback,
                                  void *user_data,
                                  GDestroyNotify destroy);

/**
 * cogl_onscreen_remove_frame_callback:
 * @onscreen: A #CoglOnscreen
 * @closure: A #CoglFrameClosure returned from
 *           cogl_onscreen_add_frame_callback()
 *
 * Removes a callback and associated user data that were previously
 * registered using cogl_onscreen_add_frame_callback().
 *
 * If a destroy callback was passed to
 * cogl_onscreen_add_frame_callback() to destroy the user data then
 * this will get called.
 */
COGL_EXPORT void
cogl_onscreen_remove_frame_callback (CoglOnscreen *onscreen,
                                     CoglFrameClosure *closure);

/**
 * CoglOnscreenDirtyInfo:
 * @x: Left edge of the dirty rectangle
 * @y: Top edge of the dirty rectangle, measured from the top of the window
 * @width: Width of the dirty rectangle
 * @height: Height of the dirty rectangle
 *
 * A structure passed to callbacks registered using
 * cogl_onscreen_add_dirty_callback(). The members describe a
 * rectangle within the onscreen buffer that should be redrawn.
 */
typedef struct _CoglOnscreenDirtyInfo CoglOnscreenDirtyInfo;

struct _CoglOnscreenDirtyInfo
{
  int x, y;
  int width, height;
};

/**
 * CoglOnscreenDirtyCallback:
 * @onscreen: The onscreen that the frame is associated with
 * @info: A #CoglOnscreenDirtyInfo struct containing the details of the
 *   dirty area
 * @user_data: The user pointer passed to
 *             cogl_onscreen_add_frame_callback()
 *
 * Is a callback that can be registered via
 * cogl_onscreen_add_dirty_callback() to be called when the windowing
 * system determines that a region of the onscreen window has been
 * lost and the application should redraw it.
 */
typedef void (*CoglOnscreenDirtyCallback) (CoglOnscreen *onscreen,
                                           const CoglOnscreenDirtyInfo *info,
                                           void *user_data);

/**
 * CoglOnscreenDirtyClosure:
 *
 * An opaque type that tracks a #CoglOnscreenDirtyCallback and associated
 * user data. A #CoglOnscreenDirtyClosure pointer will be returned from
 * cogl_onscreen_add_dirty_callback() and it allows you to remove a
 * callback later using cogl_onscreen_remove_dirty_callback().
 */
typedef struct _CoglClosure CoglOnscreenDirtyClosure;

/**
 * cogl_onscreen_dirty_closure_get_type:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_onscreen_dirty_closure_get_type (void);

/**
 * cogl_onscreen_add_dirty_callback:
 * @onscreen: A #CoglOnscreen framebuffer
 * @callback: (scope notified): A callback function to call for dirty events
 * @user_data: (closure): A private pointer to be passed to @callback
 * @destroy: (allow-none): An optional callback to destroy @user_data when the
 *           @callback is removed or @onscreen is freed.
 *
 * Installs a @callback function that will be called whenever the
 * window system has lost the contents of a region of the onscreen
 * buffer and the application should redraw it to repair the buffer.
 * For example this may happen in a window system without a compositor
 * if a window that was previously covering up the onscreen window has
 * been moved causing a region of the onscreen to be exposed.
 *
 * The @callback will be passed a #CoglOnscreenDirtyInfo struct which
 * describes a rectangle containing the newly dirtied region. Note that
 * this may be called multiple times to describe a non-rectangular
 * region composed of multiple smaller rectangles.
 *
 * The dirty events are separate from %COGL_FRAME_EVENT_SYNC events so
 * the application should also listen for this event before rendering
 * the dirty region to ensure that the framebuffer is actually ready
 * for rendering.
 *
 * Returns: (transfer none): a #CoglOnscreenDirtyClosure pointer that can be
 *          used to remove the callback and associated @user_data later.
 */
COGL_EXPORT CoglOnscreenDirtyClosure *
cogl_onscreen_add_dirty_callback (CoglOnscreen *onscreen,
                                  CoglOnscreenDirtyCallback callback,
                                  void *user_data,
                                  GDestroyNotify destroy);

/**
 * cogl_onscreen_remove_dirty_callback:
 * @onscreen: A #CoglOnscreen
 * @closure: A #CoglOnscreenDirtyClosure returned from
 *           cogl_onscreen_add_dirty_callback()
 *
 * Removes a callback and associated user data that were previously
 * registered using cogl_onscreen_add_dirty_callback().
 *
 * If a destroy callback was passed to
 * cogl_onscreen_add_dirty_callback() to destroy the user data then
 * this will also get called.
 */
COGL_EXPORT void
cogl_onscreen_remove_dirty_callback (CoglOnscreen *onscreen,
                                     CoglOnscreenDirtyClosure *closure);

/**
 * cogl_onscreen_get_frame_counter:
 *
 * Gets the value of the framebuffers frame counter. This is
 * a counter that increases by one each time
 * cogl_onscreen_swap_buffers() or cogl_onscreen_swap_region()
 * is called.
 *
 * Return value: the current frame counter value
 */
COGL_EXPORT int64_t
cogl_onscreen_get_frame_counter (CoglOnscreen *onscreen);

G_END_DECLS
