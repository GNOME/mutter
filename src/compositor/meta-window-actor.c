/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:meta-window-actor
 * @title: MetaWindowActor
 * @short_description: An actor representing a top-level window in the scene
 *   graph
 *
 * #MetaWindowActor is a #ClutterActor that adds a notion of a window to the
 * Clutter scene graph. It contains a #MetaWindow which provides the windowing
 * API, and the #MetaCompositor that handles it.  For the actual content of the
 * window, it contains a #MetaSurfaceActor.
 *
 * #MetaWindowActor takes care of the rendering features you need for your
 * window. For example, it will take the windows' requested opacity and use
 * that for clutter_actor_set_opacity(). Furthermore, it will also draw a
 * shadow around the window (using #MetaShadow) and deal with synchronization
 * between events of the window and the actual render loop. See
 * MetaWindowActor::first-frame for an example of the latter.
 */

#include "config.h"

#include <gdk/gdk.h>
#include <math.h>
#include <string.h>

#include "backends/meta-screen-cast-window.h"
#include "core/frame.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-surface-actor-x11.h"
#include "compositor/meta-surface-actor.h"
#include "compositor/meta-texture-rectangle.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/region-utils.h"
#include "meta/meta-enum-types.h"
#include "meta/meta-shadow-factory.h"
#include "meta/window.h"

#ifdef HAVE_WAYLAND
#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-surface.h"
#endif

typedef enum
{
  INITIALLY_FROZEN,
  DRAWING_FIRST_FRAME,
  EMITTED_FIRST_FRAME
} FirstFrameState;

typedef struct _MetaWindowActorPrivate
{
  MetaWindow *window;
  MetaCompositor *compositor;

  MetaSurfaceActor *surface;

  /* MetaShadowFactory only caches shadows that are actually in use;
   * to avoid unnecessary recomputation we do two things: 1) we store
   * both a focused and unfocused shadow for the window. If the window
   * doesn't have different focused and unfocused shadow parameters,
   * these will be the same. 2) when the shadow potentially changes we
   * don't immediately unreference the old shadow, we just flag it as
   * dirty and recompute it when we next need it (recompute_focused_shadow,
   * recompute_unfocused_shadow.) Because of our extraction of
   * size-invariant window shape, we'll often find that the new shadow
   * is the same as the old shadow.
   */
  MetaShadow       *focused_shadow;
  MetaShadow       *unfocused_shadow;

  /* A region that matches the shape of the window, including frame bounds */
  cairo_region_t   *shape_region;
  /* The region we should clip to when painting the shadow */
  cairo_region_t   *shadow_clip;

  /* Extracted size-invariant shape used for shadows */
  MetaWindowShape  *shadow_shape;
  char *            shadow_class;

  MetaShadowMode    shadow_mode;

  guint             size_changed_id;

  /*
   * These need to be counters rather than flags, since more plugins
   * can implement same effect; the practicality of stacking effects
   * might be dubious, but we have to at least handle it correctly.
   */
  gint              minimize_in_progress;
  gint              unminimize_in_progress;
  gint              size_change_in_progress;
  gint              map_in_progress;
  gint              destroy_in_progress;

  guint             freeze_count;

  guint		    visible                : 1;
  guint		    disposed               : 1;

  guint             needs_reshape          : 1;
  guint             recompute_focused_shadow   : 1;
  guint             recompute_unfocused_shadow : 1;

  guint		    needs_destroy	   : 1;

  guint             updates_frozen         : 1;
  guint             first_frame_state      : 2; /* FirstFrameState */
} MetaWindowActorPrivate;

enum
{
  FIRST_FRAME,
  EFFECTS_COMPLETED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

enum
{
  PROP_META_WINDOW = 1,
  PROP_SHADOW_MODE,
  PROP_SHADOW_CLASS
};

static void meta_window_actor_dispose    (GObject *object);
static void meta_window_actor_constructed (GObject *object);
static void meta_window_actor_set_property (GObject       *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec);
static void meta_window_actor_get_property (GObject      *object,
                                            guint         prop_id,
                                            GValue       *value,
                                            GParamSpec   *pspec);

static void meta_window_actor_paint (ClutterActor *actor);

static gboolean meta_window_actor_get_paint_volume (ClutterActor       *actor,
                                                    ClutterPaintVolume *volume);
static void set_surface (MetaWindowActor  *actor,
                         MetaSurfaceActor *surface);

static gboolean meta_window_actor_has_shadow (MetaWindowActor *self);

static void meta_window_actor_handle_updates (MetaWindowActor *self);

static void check_needs_reshape (MetaWindowActor *self);

static void cullable_iface_init (MetaCullableInterface *iface);

static void screen_cast_window_iface_init (MetaScreenCastWindowInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaWindowActor, meta_window_actor, CLUTTER_TYPE_ACTOR,
                                  G_ADD_PRIVATE (MetaWindowActor)
                                  G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init)
                                  G_IMPLEMENT_INTERFACE (META_TYPE_SCREEN_CAST_WINDOW, screen_cast_window_iface_init));

static void
meta_window_actor_real_set_surface_actor (MetaWindowActor  *actor,
                                          MetaSurfaceActor *surface)
{
  set_surface (actor, surface);
}

static void
meta_window_actor_class_init (MetaWindowActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec   *pspec;

  object_class->dispose      = meta_window_actor_dispose;
  object_class->set_property = meta_window_actor_set_property;
  object_class->get_property = meta_window_actor_get_property;
  object_class->constructed  = meta_window_actor_constructed;

  actor_class->paint = meta_window_actor_paint;
  actor_class->get_paint_volume = meta_window_actor_get_paint_volume;

  klass->set_surface_actor = meta_window_actor_real_set_surface_actor;

  /**
   * MetaWindowActor::first-frame:
   * @actor: the #MetaWindowActor instance
   *
   * The ::first-frame signal will be emitted the first time a frame
   * of window contents has been drawn by the application and Mutter
   * has had the chance to drawn that frame to the screen. If the
   * window starts off initially hidden, obscured, or on on a
   * different workspace, the ::first-frame signal will be emitted
   * even though the user doesn't see the contents.
   *
   * MetaDisplay::window-created is a good place to connect to this
   * signal - at that point, the MetaWindowActor for the window
   * exists, but the window has reliably not yet been drawn.
   * Connecting to an existing window that has already been drawn to
   * the screen is not useful.
   */
  signals[FIRST_FRAME] =
    g_signal_new ("first-frame",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * MetaWindowActor::effects-completed:
   * @actor: the #MetaWindowActor instance
   *
   * The ::effects-completed signal will be emitted once all pending compositor
   * effects are completed.
   */
  signals[EFFECTS_COMPLETED] =
    g_signal_new ("effects-completed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  pspec = g_param_spec_object ("meta-window",
                               "MetaWindow",
                               "The displayed MetaWindow",
                               META_TYPE_WINDOW,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_META_WINDOW,
                                   pspec);

  pspec = g_param_spec_enum ("shadow-mode",
                             "Shadow mode",
                             "Decides when to paint shadows",
                             META_TYPE_SHADOW_MODE,
                             META_SHADOW_MODE_AUTO,
                             G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_SHADOW_MODE,
                                   pspec);

  pspec = g_param_spec_string ("shadow-class",
                               "Name of the shadow class for this window.",
                               "NULL means to use the default shadow class for this window type",
                               NULL,
                               G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_SHADOW_CLASS,
                                   pspec);
}

static void
meta_window_actor_init (MetaWindowActor *self)
{
}

static void
window_appears_focused_notify (MetaWindow *mw,
                               GParamSpec *arg1,
                               gpointer    data)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (data));
}

static void
surface_size_changed (MetaSurfaceActor *actor,
                      gpointer          user_data)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (user_data);

  meta_window_actor_update_shape (self);
}

static gboolean
is_argb32 (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  /* assume we're argb until we get the window (because
     in practice we're drawing nothing, so we're fully
     transparent)
  */
  if (priv->surface)
    return meta_surface_actor_is_argb32 (priv->surface);
  else
    return TRUE;
}

static gboolean
is_non_opaque (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  return is_argb32 (self) || (window->opacity != 0xFF);
}

static gboolean
is_frozen (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->surface == NULL || priv->freeze_count > 0;
}

static void
meta_window_actor_freeze (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->freeze_count == 0 && priv->surface)
    meta_surface_actor_set_frozen (priv->surface, TRUE);

  priv->freeze_count ++;
}

static void
meta_window_actor_sync_thawed_state (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->first_frame_state == INITIALLY_FROZEN)
    priv->first_frame_state = DRAWING_FIRST_FRAME;

  if (priv->surface)
    meta_surface_actor_set_frozen (priv->surface, FALSE);

  /* We sometimes ignore moves and resizes on frozen windows */
  meta_window_actor_sync_actor_geometry (self, FALSE);
}

static void
meta_window_actor_thaw (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->freeze_count <= 0)
    g_error ("Error in freeze/thaw accounting");

  priv->freeze_count--;
  if (priv->freeze_count > 0)
    return;

  /* We still might be frozen due to lack of a MetaSurfaceActor */
  if (is_frozen (self))
    return;

  meta_window_actor_sync_thawed_state (self);

  /* We do this now since we might be going right back into the
   * frozen state */
  meta_window_actor_handle_updates (self);
}

static void
set_surface (MetaWindowActor  *self,
             MetaSurfaceActor *surface)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->surface)
    {
      g_signal_handler_disconnect (priv->surface, priv->size_changed_id);
      clutter_actor_remove_child (CLUTTER_ACTOR (self), CLUTTER_ACTOR (priv->surface));
      g_object_unref (priv->surface);
    }

  priv->surface = surface;

  if (priv->surface)
    {
      g_object_ref_sink (priv->surface);
      priv->size_changed_id = g_signal_connect (priv->surface, "size-changed",
                                                G_CALLBACK (surface_size_changed), self);
      clutter_actor_add_child (CLUTTER_ACTOR (self), CLUTTER_ACTOR (priv->surface));

      meta_window_actor_update_shape (self);

      if (is_frozen (self))
        meta_surface_actor_set_frozen (priv->surface, TRUE);
      else
        meta_window_actor_sync_thawed_state (self);
    }
}

void
meta_window_actor_update_surface (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;
  MetaSurfaceActor *surface_actor;

#ifdef HAVE_WAYLAND
  if (window->surface)
    surface_actor = meta_wayland_surface_get_actor (window->surface);
  else
#endif
  if (!meta_is_wayland_compositor ())
    surface_actor = meta_surface_actor_x11_new (window);
  else
    surface_actor = NULL;

  META_WINDOW_ACTOR_GET_CLASS (self)->set_surface_actor (self, surface_actor);
}

static void
meta_window_actor_constructed (GObject *object)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  priv->compositor = window->display->compositor;

  /* Hang our compositor window state off the MetaWindow for fast retrieval */
  meta_window_set_compositor_private (window, object);

  meta_window_actor_update_surface (self);

  meta_window_actor_update_opacity (self);

  /* Start off with an empty shape region to maintain the invariant
   * that it's always set */
  priv->shape_region = cairo_region_create ();

  meta_window_actor_sync_updates_frozen (self);

  if (is_frozen (self))
    priv->first_frame_state = INITIALLY_FROZEN;
  else
    priv->first_frame_state = DRAWING_FIRST_FRAME;

  meta_window_actor_sync_actor_geometry (self, priv->window->placed);
}

static void
meta_window_actor_dispose (GObject *object)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  g_clear_pointer (&priv->shape_region, cairo_region_destroy);
  g_clear_pointer (&priv->shadow_clip, cairo_region_destroy);

  g_clear_pointer (&priv->shadow_class, g_free);
  g_clear_pointer (&priv->focused_shadow, meta_shadow_unref);
  g_clear_pointer (&priv->unfocused_shadow, meta_shadow_unref);
  g_clear_pointer (&priv->shadow_shape, meta_window_shape_unref);

  compositor->windows = g_list_remove (compositor->windows, (gconstpointer) self);

  g_clear_object (&priv->window);

  META_WINDOW_ACTOR_GET_CLASS (self)->set_surface_actor (self, NULL);

  G_OBJECT_CLASS (meta_window_actor_parent_class)->dispose (object);
}

static void
meta_window_actor_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_META_WINDOW:
      priv->window = g_value_dup_object (value);
      g_signal_connect_object (priv->window, "notify::appears-focused",
                               G_CALLBACK (window_appears_focused_notify), self, 0);
      break;
    case PROP_SHADOW_MODE:
      {
        MetaShadowMode newv = g_value_get_enum (value);

        if (newv == priv->shadow_mode)
          return;

        priv->shadow_mode = newv;

        meta_window_actor_invalidate_shadow (self);
      }
      break;
    case PROP_SHADOW_CLASS:
      {
        const char *newv = g_value_get_string (value);

        if (g_strcmp0 (newv, priv->shadow_class) == 0)
          return;

        g_free (priv->shadow_class);
        priv->shadow_class = g_strdup (newv);

        meta_window_actor_invalidate_shadow (self);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_actor_get_property (GObject      *object,
                                guint         prop_id,
                                GValue       *value,
                                GParamSpec   *pspec)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_META_WINDOW:
      g_value_set_object (value, priv->window);
      break;
    case PROP_SHADOW_MODE:
      g_value_set_enum (value, priv->shadow_mode);
      break;
    case PROP_SHADOW_CLASS:
      g_value_set_string (value, priv->shadow_class);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static const char *
meta_window_actor_get_shadow_class (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->shadow_class != NULL)
    return priv->shadow_class;
  else
    {
      MetaWindowType window_type = meta_window_get_window_type (priv->window);

      switch (window_type)
        {
        case META_WINDOW_DROPDOWN_MENU:
        case META_WINDOW_COMBO:
          return "dropdown-menu";
        case META_WINDOW_POPUP_MENU:
          return "popup-menu";
        default:
          {
            MetaFrameType frame_type = meta_window_get_frame_type (priv->window);
            return meta_frame_type_to_string (frame_type);
          }
        }
    }
}

static void
meta_window_actor_get_shadow_params (MetaWindowActor  *self,
                                     gboolean          appears_focused,
                                     MetaShadowParams *params)
{
  const char *shadow_class = meta_window_actor_get_shadow_class (self);

  meta_shadow_factory_get_params (meta_shadow_factory_get_default (),
                                  shadow_class, appears_focused,
                                  params);
}

void
meta_window_actor_get_shape_bounds (MetaWindowActor       *self,
                                    cairo_rectangle_int_t *bounds)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  cairo_region_get_extents (priv->shape_region, bounds);
}

static void
meta_window_actor_get_shadow_bounds (MetaWindowActor       *self,
                                     gboolean               appears_focused,
                                     cairo_rectangle_int_t *bounds)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaShadow *shadow;
  cairo_rectangle_int_t shape_bounds;
  MetaShadowParams params;

  shadow = appears_focused ? priv->focused_shadow : priv->unfocused_shadow;

  meta_window_actor_get_shape_bounds (self, &shape_bounds);
  meta_window_actor_get_shadow_params (self, appears_focused, &params);

  meta_shadow_get_bounds (shadow,
                          params.x_offset + shape_bounds.x,
                          params.y_offset + shape_bounds.y,
                          shape_bounds.width,
                          shape_bounds.height,
                          bounds);
}

/* If we have an ARGB32 window that we decorate with a frame, it's
 * probably something like a translucent terminal - something where
 * the alpha channel represents transparency rather than a shape.  We
 * don't want to show the shadow through the translucent areas since
 * the shadow is wrong for translucent windows (it should be
 * translucent itself and colored), and not only that, will /look/
 * horribly wrong - a misplaced big black blob. As a hack, what we
 * want to do is just draw the shadow as normal outside the frame, and
 * inside the frame draw no shadow.  This is also not even close to
 * the right result, but looks OK. We also apply this approach to
 * windows set to be partially translucent with _NET_WM_WINDOW_OPACITY.
 */
static gboolean
clip_shadow_under_window (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return is_non_opaque (self) && priv->window->frame;
}

static void
meta_window_actor_paint (ClutterActor *actor)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (actor);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  gboolean appears_focused = meta_window_appears_focused (priv->window);
  MetaShadow *shadow;
  CoglFramebuffer *framebuffer = cogl_get_draw_framebuffer ();

  shadow = appears_focused ? priv->focused_shadow : priv->unfocused_shadow;

  if (shadow != NULL)
    {
      MetaShadowParams params;
      cairo_rectangle_int_t shape_bounds;
      cairo_region_t *clip = priv->shadow_clip;
      MetaWindow *window = priv->window;

      meta_window_actor_get_shape_bounds (self, &shape_bounds);
      meta_window_actor_get_shadow_params (self, appears_focused, &params);

      /* The frame bounds are already subtracted from priv->shadow_clip
       * if that exists.
       */
      if (!clip && clip_shadow_under_window (self))
        {
          cairo_region_t *frame_bounds = meta_window_get_frame_bounds (priv->window);
          cairo_rectangle_int_t bounds;

          meta_window_actor_get_shadow_bounds (self, appears_focused, &bounds);
          clip = cairo_region_create_rectangle (&bounds);

          cairo_region_subtract (clip, frame_bounds);
        }

      meta_shadow_paint (shadow,
                         framebuffer,
                         params.x_offset + shape_bounds.x,
                         params.y_offset + shape_bounds.y,
                         shape_bounds.width,
                         shape_bounds.height,
                         (clutter_actor_get_paint_opacity (actor) * params.opacity * window->opacity) / (255 * 255),
                         clip,
                         clip_shadow_under_window (self)); /* clip_strictly - not just as an optimization */

      if (clip && clip != priv->shadow_clip)
        cairo_region_destroy (clip);
    }

  CLUTTER_ACTOR_CLASS (meta_window_actor_parent_class)->paint (actor);
}

static gboolean
meta_window_actor_get_paint_volume (ClutterActor       *actor,
                                    ClutterPaintVolume *volume)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (actor);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  gboolean appears_focused = meta_window_appears_focused (priv->window);

  /* The paint volume is computed before paint functions are called
   * so our bounds might not be updated yet. Force an update. */
  meta_window_actor_handle_updates (self);

  if (appears_focused ? priv->focused_shadow : priv->unfocused_shadow)
    {
      cairo_rectangle_int_t shadow_bounds;
      ClutterActorBox shadow_box;

      /* We could compute an full clip region as we do for the window
       * texture, but the shadow is relatively cheap to draw, and
       * a little more complex to clip, so we just catch the case where
       * the shadow is completely obscured and doesn't need to be drawn
       * at all.
       */

      meta_window_actor_get_shadow_bounds (self, appears_focused, &shadow_bounds);
      shadow_box.x1 = shadow_bounds.x;
      shadow_box.x2 = shadow_bounds.x + shadow_bounds.width;
      shadow_box.y1 = shadow_bounds.y;
      shadow_box.y2 = shadow_bounds.y + shadow_bounds.height;

      clutter_paint_volume_union_box (volume, &shadow_box);
    }

  if (priv->surface)
    {
      const ClutterPaintVolume *child_volume;

      child_volume = clutter_actor_get_transformed_paint_volume (CLUTTER_ACTOR (priv->surface), actor);
      if (!child_volume)
        return FALSE;

      clutter_paint_volume_union (volume, child_volume);
    }

  return TRUE;
}

static gboolean
meta_window_actor_has_shadow (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->shadow_mode == META_SHADOW_MODE_FORCED_OFF)
    return FALSE;
  if (priv->shadow_mode == META_SHADOW_MODE_FORCED_ON)
    return TRUE;

  /* Leaving out shadows for maximized and fullscreen windows is an effeciency
   * win and also prevents the unsightly effect of the shadow of maximized
   * window appearing on an adjacent window */
  if ((meta_window_get_maximized (priv->window) == META_MAXIMIZE_BOTH) ||
      meta_window_is_fullscreen (priv->window))
    return FALSE;

  /*
   * If we have two snap-tiled windows, we don't want the shadow to obstruct
   * the other window.
   */
  if (meta_window_get_tile_match (priv->window))
    return FALSE;

  /*
   * Always put a shadow around windows with a frame - This should override
   * the restriction about not putting a shadow around ARGB windows.
   */
  if (meta_window_get_frame (priv->window))
    return TRUE;

  /*
   * Do not add shadows to non-opaque (ARGB32) windows, as we can't easily
   * generate shadows for them.
   */
  if (is_non_opaque (self))
    return FALSE;

  /*
   * If a window specifies that it has custom frame extents, that likely
   * means that it is drawing a shadow itself. Don't draw our own.
   */
  if (priv->window->has_custom_frame_extents)
    return FALSE;

  /*
   * Generate shadows for all other windows.
   */
  return TRUE;
}

/**
 * meta_window_actor_get_meta_window:
 * @self: a #MetaWindowActor
 *
 * Gets the #MetaWindow object that the the #MetaWindowActor is displaying
 *
 * Return value: (transfer none): the displayed #MetaWindow
 */
MetaWindow *
meta_window_actor_get_meta_window (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->window;
}

/**
 * meta_window_actor_get_texture:
 * @self: a #MetaWindowActor
 *
 * Gets the ClutterActor that is used to display the contents of the window,
 * or NULL if no texture is shown yet, because the window is not mapped.
 *
 * Return value: (transfer none): the #ClutterActor for the contents
 */
ClutterActor *
meta_window_actor_get_texture (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->surface)
    return CLUTTER_ACTOR (meta_surface_actor_get_texture (priv->surface));
  else
    return NULL;
}

/**
 * meta_window_actor_get_surface:
 * @self: a #MetaWindowActor
 *
 * Gets the MetaSurfaceActor that draws the content of this window,
 * or NULL if there is no surface yet associated with this window.
 *
 * Return value: (transfer none): the #MetaSurfaceActor for the contents
 */
MetaSurfaceActor *
meta_window_actor_get_surface (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->surface;
}

/**
 * meta_window_actor_is_destroyed:
 * @self: a #MetaWindowActor
 *
 * Gets whether the X window that the actor was displaying has been destroyed
 *
 * Return value: %TRUE when the window is destroyed, otherwise %FALSE
 */
gboolean
meta_window_actor_is_destroyed (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->disposed || priv->needs_destroy;
}

void
meta_window_actor_queue_frame_drawn (MetaWindowActor *self,
                                     gboolean         no_delay_frame)
{
  META_WINDOW_ACTOR_GET_CLASS (self)->queue_frame_drawn (self,
                                                         no_delay_frame);
}

gboolean
meta_window_actor_effect_in_progress (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return (priv->minimize_in_progress ||
          priv->size_change_in_progress ||
          priv->map_in_progress ||
          priv->destroy_in_progress);
}

static gboolean
is_freeze_thaw_effect (MetaPluginEffect event)
{
  switch (event)
  {
  case META_PLUGIN_DESTROY:
  case META_PLUGIN_SIZE_CHANGE:
    return TRUE;
    break;
  default:
    return FALSE;
  }
}

static gboolean
start_simple_effect (MetaWindowActor  *self,
                     MetaPluginEffect  event)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;
  gint *counter = NULL;
  gboolean use_freeze_thaw = FALSE;

  g_assert (compositor->plugin_mgr != NULL);

  switch (event)
  {
  case META_PLUGIN_NONE:
    return FALSE;
  case META_PLUGIN_MINIMIZE:
    counter = &priv->minimize_in_progress;
    break;
  case META_PLUGIN_UNMINIMIZE:
    counter = &priv->unminimize_in_progress;
    break;
  case META_PLUGIN_MAP:
    counter = &priv->map_in_progress;
    break;
  case META_PLUGIN_DESTROY:
    counter = &priv->destroy_in_progress;
    break;
  case META_PLUGIN_SIZE_CHANGE:
  case META_PLUGIN_SWITCH_WORKSPACE:
    g_assert_not_reached ();
    break;
  }

  g_assert (counter);

  use_freeze_thaw = is_freeze_thaw_effect (event);

  if (use_freeze_thaw)
    meta_window_actor_freeze (self);

  (*counter)++;

  if (!meta_plugin_manager_event_simple (compositor->plugin_mgr, self, event))
    {
      (*counter)--;
      if (use_freeze_thaw)
        meta_window_actor_thaw (self);
      return FALSE;
    }

  return TRUE;
}

static void
meta_window_actor_after_effects (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->needs_destroy)
    {
      clutter_actor_destroy (CLUTTER_ACTOR (self));
      return;
    }

  g_signal_emit (self, signals[EFFECTS_COMPLETED], 0);
  meta_window_actor_sync_visibility (self);
  meta_window_actor_sync_actor_geometry (self, FALSE);
}

void
meta_window_actor_effect_completed (MetaWindowActor  *self,
                                    MetaPluginEffect  event)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  gboolean inconsistent = FALSE;

  /* NB: Keep in mind that when effects get completed it possible
   * that the corresponding MetaWindow may have be been destroyed.
   * In this case priv->window will == NULL */

  switch (event)
  {
  case META_PLUGIN_NONE:
    break;
  case META_PLUGIN_MINIMIZE:
    {
      priv->minimize_in_progress--;
      if (priv->minimize_in_progress < 0)
        {
          g_warning ("Error in minimize accounting.");
          priv->minimize_in_progress = 0;
          inconsistent = TRUE;
        }
    }
    break;
  case META_PLUGIN_UNMINIMIZE:
    {
      priv->unminimize_in_progress--;
      if (priv->unminimize_in_progress < 0)
       {
         g_warning ("Error in unminimize accounting.");
         priv->unminimize_in_progress = 0;
         inconsistent = TRUE;
       }
    }
    break;
  case META_PLUGIN_MAP:
    /*
     * Make sure that the actor is at the correct place in case
     * the plugin fscked.
     */
    priv->map_in_progress--;

    if (priv->map_in_progress < 0)
      {
        g_warning ("Error in map accounting.");
        priv->map_in_progress = 0;
        inconsistent = TRUE;
      }
    break;
  case META_PLUGIN_DESTROY:
    priv->destroy_in_progress--;

    if (priv->destroy_in_progress < 0)
      {
        g_warning ("Error in destroy accounting.");
        priv->destroy_in_progress = 0;
        inconsistent = TRUE;
      }
    break;
  case META_PLUGIN_SIZE_CHANGE:
    priv->size_change_in_progress--;
    if (priv->size_change_in_progress < 0)
      {
        g_warning ("Error in size change accounting.");
        priv->size_change_in_progress = 0;
        inconsistent = TRUE;
      }
    break;
  case META_PLUGIN_SWITCH_WORKSPACE:
    g_assert_not_reached ();
    break;
  }

  if (is_freeze_thaw_effect (event) && !inconsistent)
    meta_window_actor_thaw (self);

  if (!meta_window_actor_effect_in_progress (self))
    meta_window_actor_after_effects (self);
}

gboolean
meta_window_actor_should_unredirect (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (!meta_window_actor_is_destroyed (self) && priv->surface)
    return meta_surface_actor_should_unredirect (priv->surface);
  else
    return FALSE;
}

void
meta_window_actor_set_unredirected (MetaWindowActor *self,
                                    gboolean         unredirected)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  g_assert (priv->surface); /* because otherwise should_unredirect() is FALSE */
  meta_surface_actor_set_unredirected (priv->surface, unredirected);
}

void
meta_window_actor_queue_destroy (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;
  MetaWindowType window_type = meta_window_get_window_type (window);

  meta_window_set_compositor_private (window, NULL);

  META_WINDOW_ACTOR_GET_CLASS (self)->queue_destroy (self);

  if (window_type == META_WINDOW_DROPDOWN_MENU ||
      window_type == META_WINDOW_POPUP_MENU ||
      window_type == META_WINDOW_TOOLTIP ||
      window_type == META_WINDOW_NOTIFICATION ||
      window_type == META_WINDOW_COMBO ||
      window_type == META_WINDOW_DND ||
      window_type == META_WINDOW_OVERRIDE_OTHER)
    {
      /*
       * No effects, just kill it.
       */
      clutter_actor_destroy (CLUTTER_ACTOR (self));
      return;
    }

  priv->needs_destroy = TRUE;

  if (!meta_window_actor_effect_in_progress (self))
    clutter_actor_destroy (CLUTTER_ACTOR (self));
}

void
meta_window_actor_sync_actor_geometry (MetaWindowActor *self,
                                       gboolean         did_placement)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaRectangle window_rect;

  meta_window_get_buffer_rect (priv->window, &window_rect);

  /* When running as a Wayland compositor we catch size changes when new
   * buffers are attached */
  if (META_IS_SURFACE_ACTOR_X11 (priv->surface))
    meta_surface_actor_x11_set_size (META_SURFACE_ACTOR_X11 (priv->surface),
                                     window_rect.width, window_rect.height);

  /* Normally we want freezing a window to also freeze its position; this allows
   * windows to atomically move and resize together, either under app control,
   * or because the user is resizing from the left/top. But on initial placement
   * we need to assign a position, since immediately after the window
   * is shown, the map effect will go into effect and prevent further geometry
   * updates.
   */
  if (is_frozen (self) && !did_placement)
    return;

  if (meta_window_actor_effect_in_progress (self))
    return;

  clutter_actor_set_position (CLUTTER_ACTOR (self),
                              window_rect.x, window_rect.y);
  clutter_actor_set_size (CLUTTER_ACTOR (self),
                          window_rect.width, window_rect.height);
}

void
meta_window_actor_show (MetaWindowActor   *self,
                        MetaCompEffect     effect)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;
  MetaPluginEffect event;

  g_return_if_fail (!priv->visible);

  priv->visible = TRUE;

  switch (effect)
    {
    case META_COMP_EFFECT_CREATE:
      event = META_PLUGIN_MAP;
      break;
    case META_COMP_EFFECT_UNMINIMIZE:
      event = META_PLUGIN_UNMINIMIZE;
      break;
    case META_COMP_EFFECT_NONE:
      event = META_PLUGIN_NONE;
      break;
    default:
      g_assert_not_reached();
    }

  if (compositor->switch_workspace_in_progress ||
      !start_simple_effect (self, event))
    {
      clutter_actor_show (CLUTTER_ACTOR (self));
    }
}

void
meta_window_actor_hide (MetaWindowActor *self,
                        MetaCompEffect   effect)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;
  MetaPluginEffect event;

  g_return_if_fail (priv->visible);

  priv->visible = FALSE;

  /* If a plugin is animating a workspace transition, we have to
   * hold off on hiding the window, and do it after the workspace
   * switch completes
   */
  if (compositor->switch_workspace_in_progress)
    return;

  switch (effect)
    {
    case META_COMP_EFFECT_DESTROY:
      event = META_PLUGIN_DESTROY;
      break;
    case META_COMP_EFFECT_MINIMIZE:
      event = META_PLUGIN_MINIMIZE;
      break;
    case META_COMP_EFFECT_NONE:
      event = META_PLUGIN_NONE;
      break;
    default:
      g_assert_not_reached();
    }

  if (!start_simple_effect (self, event))
    clutter_actor_hide (CLUTTER_ACTOR (self));
}

void
meta_window_actor_size_change (MetaWindowActor    *self,
                               MetaSizeChange      which_change,
                               MetaRectangle      *old_frame_rect,
                               MetaRectangle      *old_buffer_rect)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;

  priv->size_change_in_progress++;
  meta_window_actor_freeze (self);

  if (!meta_plugin_manager_event_size_change (compositor->plugin_mgr, self,
                                              which_change, old_frame_rect, old_buffer_rect))
    {
      priv->size_change_in_progress--;
      meta_window_actor_thaw (self);
    }
}

#if 0
/* Print out a region; useful for debugging */
static void
print_region (cairo_region_t *region)
{
  int n_rects;
  int i;

  n_rects = cairo_region_num_rectangles (region);
  g_print ("[");
  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t rect;
      cairo_region_get_rectangle (region, i, &rect);
      g_print ("+%d+%dx%dx%d ",
               rect.x, rect.y, rect.width, rect.height);
    }
  g_print ("]\n");
}
#endif

#if 0
/* Dump a region to a PNG file; useful for debugging */
static void
see_region (cairo_region_t *region,
            int             width,
            int             height,
            char           *filename)
{
  cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_A8, width, height);
  cairo_t *cr = cairo_create (surface);

  gdk_cairo_region (cr, region);
  cairo_fill (cr);

  cairo_surface_write_to_png (surface, filename);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
}
#endif

/**
 * meta_window_actor_set_clip_region_beneath:
 * @self: a #MetaWindowActor
 * @clip_region: the region of the screen that isn't completely
 *  obscured beneath the main window texture.
 *
 * Provides a hint as to what areas need to be drawn *beneath*
 * the main window texture.  This is the relevant clip region
 * when drawing the shadow, properly accounting for areas of the
 * shadow hid by the window itself. This will be set before painting
 * then unset afterwards.
 */
static void
meta_window_actor_set_clip_region_beneath (MetaWindowActor *self,
                                           cairo_region_t  *beneath_region)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  gboolean appears_focused = meta_window_appears_focused (priv->window);

  if (appears_focused ? priv->focused_shadow : priv->unfocused_shadow)
    {
      g_clear_pointer (&priv->shadow_clip, cairo_region_destroy);

      if (beneath_region)
        {
          priv->shadow_clip = cairo_region_copy (beneath_region);

          if (clip_shadow_under_window (self))
            {
              cairo_region_t *frame_bounds = meta_window_get_frame_bounds (priv->window);
              cairo_region_subtract (priv->shadow_clip, frame_bounds);
            }
        }
      else
        priv->shadow_clip = NULL;
    }
}

static void
meta_window_actor_cull_out (MetaCullable   *cullable,
                            cairo_region_t *unobscured_region,
                            cairo_region_t *clip_region)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (cullable);

  meta_cullable_cull_out_children (cullable, unobscured_region, clip_region);
  meta_window_actor_set_clip_region_beneath (self, clip_region);
}

static void
meta_window_actor_reset_culling (MetaCullable *cullable)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (cullable);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  g_clear_pointer (&priv->shadow_clip, cairo_region_destroy);

  meta_cullable_reset_culling_children (cullable);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_out = meta_window_actor_cull_out;
  iface->reset_culling = meta_window_actor_reset_culling;
}

static void
check_needs_shadow (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaShadow *old_shadow = NULL;
  MetaShadow **shadow_location;
  gboolean recompute_shadow;
  gboolean should_have_shadow;
  gboolean appears_focused;

  /* Calling meta_window_actor_has_shadow() here at every pre-paint is cheap
   * and avoids the need to explicitly handle window type changes, which
   * we would do if tried to keep track of when we might be adding or removing
   * a shadow more explicitly. We only keep track of changes to the *shape* of
   * the shadow with priv->recompute_shadow.
   */

  should_have_shadow = meta_window_actor_has_shadow (self);
  appears_focused = meta_window_appears_focused (priv->window);

  if (appears_focused)
    {
      recompute_shadow = priv->recompute_focused_shadow;
      priv->recompute_focused_shadow = FALSE;
      shadow_location = &priv->focused_shadow;
    }
  else
    {
      recompute_shadow = priv->recompute_unfocused_shadow;
      priv->recompute_unfocused_shadow = FALSE;
      shadow_location = &priv->unfocused_shadow;
    }

  if (!should_have_shadow || recompute_shadow)
    {
      if (*shadow_location != NULL)
        {
          old_shadow = *shadow_location;
          *shadow_location = NULL;
        }
    }

  if (*shadow_location == NULL && should_have_shadow)
    {
      if (priv->shadow_shape == NULL)
        priv->shadow_shape = meta_window_shape_new (priv->shape_region);

      MetaShadowFactory *factory = meta_shadow_factory_get_default ();
      const char *shadow_class = meta_window_actor_get_shadow_class (self);
      cairo_rectangle_int_t shape_bounds;

      meta_window_actor_get_shape_bounds (self, &shape_bounds);
      *shadow_location = meta_shadow_factory_get_shadow (factory,
                                                         priv->shadow_shape,
                                                         shape_bounds.width, shape_bounds.height,
                                                         shadow_class, appears_focused);
    }

  if (old_shadow != NULL)
    meta_shadow_unref (old_shadow);
}

void
meta_window_actor_process_x11_damage (MetaWindowActor    *self,
                                      XDamageNotifyEvent *event)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->surface)
    meta_surface_actor_process_damage (priv->surface,
                                       event->area.x,
                                       event->area.y,
                                       event->area.width,
                                       event->area.height);
}

void
meta_window_actor_sync_visibility (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (CLUTTER_ACTOR_IS_VISIBLE (self) != priv->visible)
    {
      if (priv->visible)
        clutter_actor_show (CLUTTER_ACTOR (self));
      else
        clutter_actor_hide (CLUTTER_ACTOR (self));
    }
}

static cairo_region_t *
scan_visible_region (guchar         *mask_data,
                     int             stride,
                     cairo_region_t *scan_area)
{
  int i, n_rects = cairo_region_num_rectangles (scan_area);
  MetaRegionBuilder builder;

  meta_region_builder_init (&builder);

  for (i = 0; i < n_rects; i++)
    {
      int x, y;
      cairo_rectangle_int_t rect;

      cairo_region_get_rectangle (scan_area, i, &rect);

      for (y = rect.y; y < (rect.y + rect.height); y++)
        {
          for (x = rect.x; x < (rect.x + rect.width); x++)
            {
              int x2 = x;
              while (mask_data[y * stride + x2] == 255 && x2 < (rect.x + rect.width))
                x2++;

              if (x2 > x)
                {
                  meta_region_builder_add_rectangle (&builder, x, y, x2 - x, 1);
                  x = x2;
                }
            }
        }
    }

  return meta_region_builder_finish (&builder);
}

static void
build_and_scan_frame_mask (MetaWindowActor       *self,
                           cairo_rectangle_int_t *client_area,
                           cairo_region_t        *shape_region)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  ClutterBackend *backend = clutter_get_default_backend ();
  CoglContext *ctx = clutter_backend_get_cogl_context (backend);
  guchar *mask_data;
  guint tex_width, tex_height;
  MetaShapedTexture *stex;
  CoglTexture *paint_tex, *mask_texture;
  int stride;
  cairo_t *cr;
  cairo_surface_t *surface;

  stex = meta_surface_actor_get_texture (priv->surface);
  g_return_if_fail (stex);

  meta_shaped_texture_set_mask_texture (stex, NULL);

  paint_tex = meta_shaped_texture_get_texture (stex);
  if (paint_tex == NULL)
    return;

  tex_width = cogl_texture_get_width (paint_tex);
  tex_height = cogl_texture_get_height (paint_tex);

  stride = cairo_format_stride_for_width (CAIRO_FORMAT_A8, tex_width);

  /* Create data for an empty image */
  mask_data = g_malloc0 (stride * tex_height);

  surface = cairo_image_surface_create_for_data (mask_data,
                                                 CAIRO_FORMAT_A8,
                                                 tex_width,
                                                 tex_height,
                                                 stride);
  cr = cairo_create (surface);

  gdk_cairo_region (cr, shape_region);
  cairo_fill (cr);

  if (priv->window->frame != NULL)
    {
      cairo_region_t *frame_paint_region, *scanned_region;
      cairo_rectangle_int_t rect = { 0, 0, tex_width, tex_height };

      /* Make sure we don't paint the frame over the client window. */
      frame_paint_region = cairo_region_create_rectangle (&rect);
      cairo_region_subtract_rectangle (frame_paint_region, client_area);

      gdk_cairo_region (cr, frame_paint_region);
      cairo_clip (cr);

      meta_frame_get_mask (priv->window->frame, cr);

      cairo_surface_flush (surface);
      scanned_region = scan_visible_region (mask_data, stride, frame_paint_region);
      cairo_region_union (shape_region, scanned_region);
      cairo_region_destroy (scanned_region);
      cairo_region_destroy (frame_paint_region);
    }

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  if (meta_texture_rectangle_check (paint_tex))
    {
      mask_texture = COGL_TEXTURE (cogl_texture_rectangle_new_with_size (ctx, tex_width, tex_height));
      cogl_texture_set_components (mask_texture, COGL_TEXTURE_COMPONENTS_A);
      cogl_texture_set_region (mask_texture,
                               0, 0, /* src_x/y */
                               0, 0, /* dst_x/y */
                               tex_width, tex_height, /* dst_width/height */
                               tex_width, tex_height, /* width/height */
                               COGL_PIXEL_FORMAT_A_8,
                               stride, mask_data);
    }
  else
    {
      CoglError *error = NULL;

      mask_texture = COGL_TEXTURE (cogl_texture_2d_new_from_data (ctx, tex_width, tex_height,
                                                                  COGL_PIXEL_FORMAT_A_8,
                                                                  stride, mask_data, &error));

      if (error)
        {
          g_warning ("Failed to allocate mask texture: %s", error->message);
          cogl_error_free (error);
        }
    }

  meta_shaped_texture_set_mask_texture (stex, mask_texture);
  if (mask_texture)
    cogl_object_unref (mask_texture);

  g_free (mask_data);
}

static void
meta_window_actor_update_shape_region (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  cairo_region_t *region = NULL;
  cairo_rectangle_int_t client_area;

  meta_window_get_client_area_rect (priv->window, &client_area);

  if (priv->window->frame != NULL && priv->window->shape_region != NULL)
    {
      region = cairo_region_copy (priv->window->shape_region);
      cairo_region_translate (region, client_area.x, client_area.y);
    }
  else if (priv->window->shape_region != NULL)
    {
      region = cairo_region_reference (priv->window->shape_region);
    }
  else
    {
      /* If we don't have a shape on the server, that means that
       * we have an implicit shape of one rectangle covering the
       * entire window. */
      region = cairo_region_create_rectangle (&client_area);
    }

  if ((priv->window->shape_region != NULL) || (priv->window->frame != NULL))
    build_and_scan_frame_mask (self, &client_area, region);

  g_clear_pointer (&priv->shape_region, cairo_region_destroy);
  priv->shape_region = region;

  g_clear_pointer (&priv->shadow_shape, meta_window_shape_unref);

  meta_window_actor_invalidate_shadow (self);
}

static void
meta_window_actor_update_input_region (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;
  cairo_region_t *region;

  if (window->shape_region && window->input_region)
    {
      region = cairo_region_copy (window->shape_region);
      cairo_region_intersect (region, window->input_region);
    }
  else if (window->shape_region)
    region = cairo_region_reference (window->shape_region);
  else if (window->input_region)
    region = cairo_region_reference (window->input_region);
  else
    region = NULL;

  meta_surface_actor_set_input_region (priv->surface, region);
  cairo_region_destroy (region);
}

static void
meta_window_actor_update_opaque_region (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  cairo_region_t *opaque_region;
  gboolean argb32 = is_argb32 (self);

  if (argb32 && priv->window->opaque_region != NULL)
    {
      cairo_rectangle_int_t client_area;

      meta_window_get_client_area_rect (priv->window, &client_area);

      /* The opaque region is defined to be a part of the
       * window which ARGB32 will always paint with opaque
       * pixels. For these regions, we want to avoid painting
       * windows and shadows beneath them.
       *
       * If the client gives bad coordinates where it does not
       * fully paint, the behavior is defined by the specification
       * to be undefined, and considered a client bug. In mutter's
       * case, graphical glitches will occur.
       */
      opaque_region = cairo_region_copy (priv->window->opaque_region);
      cairo_region_translate (opaque_region, client_area.x, client_area.y);
      cairo_region_intersect (opaque_region, priv->shape_region);
    }
  else if (argb32)
    opaque_region = NULL;
  else
    opaque_region = cairo_region_reference (priv->shape_region);

  meta_surface_actor_set_opaque_region (priv->surface, opaque_region);
  cairo_region_destroy (opaque_region);
}

static void
check_needs_reshape (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (!priv->needs_reshape)
    return;

  meta_window_actor_update_shape_region (self);

  if (priv->window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    {
      meta_window_actor_update_input_region (self);
      meta_window_actor_update_opaque_region (self);
    }

  priv->needs_reshape = FALSE;
}

void
meta_window_actor_update_shape (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  priv->needs_reshape = TRUE;

  if (is_frozen (self))
    return;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (priv->surface));
}

static void
meta_window_actor_handle_updates (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (is_frozen (self))
    {
      /* The window is frozen due to a pending animation: we'll wait until
       * the animation finishes to reshape and repair the window */
      return;
    }

  if (meta_surface_actor_is_unredirected (priv->surface))
    return;

  meta_surface_actor_pre_paint (priv->surface);

  if (!meta_surface_actor_is_visible (priv->surface))
    return;

  check_needs_reshape (self);
  check_needs_shadow (self);
}

void
meta_window_actor_pre_paint (MetaWindowActor *self)
{
  if (meta_window_actor_is_destroyed (self))
    return;

  meta_window_actor_handle_updates (self);

  META_WINDOW_ACTOR_GET_CLASS (self)->pre_paint (self);
}

void
meta_window_actor_post_paint (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  META_WINDOW_ACTOR_GET_CLASS (self)->post_paint (self);

  if (meta_window_actor_is_destroyed (self))
    return;

  if (priv->first_frame_state == DRAWING_FIRST_FRAME)
    {
      priv->first_frame_state = EMITTED_FIRST_FRAME;
      g_signal_emit (self, signals[FIRST_FRAME], 0);
    }
}

void
meta_window_actor_frame_complete (MetaWindowActor  *self,
                                  ClutterFrameInfo *frame_info,
                                  gint64            presentation_time)
{
  META_WINDOW_ACTOR_GET_CLASS (self)->frame_complete (self,
                                                      frame_info,
                                                      presentation_time);
}

void
meta_window_actor_invalidate_shadow (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  priv->recompute_focused_shadow = TRUE;
  priv->recompute_unfocused_shadow = TRUE;

  if (is_frozen (self))
    return;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

void
meta_window_actor_update_opacity (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  if (priv->surface)
    clutter_actor_set_opacity (CLUTTER_ACTOR (priv->surface), window->opacity);
}

static void
meta_window_actor_set_updates_frozen (MetaWindowActor *self,
                                      gboolean         updates_frozen)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  updates_frozen = updates_frozen != FALSE;

  if (priv->updates_frozen != updates_frozen)
    {
      priv->updates_frozen = updates_frozen;
      if (updates_frozen)
        meta_window_actor_freeze (self);
      else
        meta_window_actor_thaw (self);
    }
}

void
meta_window_actor_sync_updates_frozen (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  meta_window_actor_set_updates_frozen (self, meta_window_updates_are_frozen (window));
}

MetaWindowActor *
meta_window_actor_from_window (MetaWindow *window)
{
  return META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
}

static void
meta_window_actor_get_frame_bounds (MetaScreenCastWindow *screen_cast_window,
                                    MetaRectangle        *bounds)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  MetaWindow *window;
  MetaShapedTexture *stex;
  MetaRectangle buffer_rect;
  MetaRectangle frame_rect;
  double scale_x, scale_y;

  stex = meta_surface_actor_get_texture (priv->surface);
  clutter_actor_get_scale (CLUTTER_ACTOR (stex), &scale_x, &scale_y);

  window = priv->window;
  meta_window_get_buffer_rect (window, &buffer_rect);
  meta_window_get_frame_rect (window, &frame_rect);

  bounds->x = (int) floor ((frame_rect.x - buffer_rect.x) / scale_x);
  bounds->y = (int) floor ((frame_rect.y - buffer_rect.y) / scale_y);
  bounds->width = (int) ceil (frame_rect.width / scale_x);
  bounds->height = (int) ceil (frame_rect.height / scale_y);
}

static void
meta_window_actor_transform_relative_position (MetaScreenCastWindow *screen_cast_window,
                                               double                x,
                                               double                y,
                                               double               *x_out,
                                               double               *y_out)

{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  MetaShapedTexture *stex;
  MetaRectangle bounds;
  ClutterVertex v1 = { 0.f, }, v2 = { 0.f, };

  meta_window_actor_get_frame_bounds (screen_cast_window, &bounds);

  v1.x = CLAMP ((float) x,
                bounds.x,
                bounds.x + bounds.width);
  v1.y = CLAMP ((float) y,
                bounds.y,
                bounds.y + bounds.height);

  stex = meta_surface_actor_get_texture (priv->surface);
  clutter_actor_apply_transform_to_point (CLUTTER_ACTOR (stex), &v1, &v2);

  *x_out = (double) v2.x;
  *y_out = (double) v2.y;
}

static gboolean
meta_window_actor_transform_cursor_position (MetaScreenCastWindow *screen_cast_window,
                                             MetaCursorSprite     *cursor_sprite,
                                             ClutterPoint         *cursor_position,
                                             float                *out_cursor_scale,
                                             ClutterPoint         *out_relative_cursor_position)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  MetaWindow *window;

  window = priv->window;
  if (!meta_window_has_pointer (window))
    return FALSE;

  if (cursor_sprite &&
      meta_cursor_sprite_get_cogl_texture (cursor_sprite) &&
      out_cursor_scale)
    {
      MetaShapedTexture *stex;
      double actor_scale;
      float cursor_texture_scale;

      stex = meta_surface_actor_get_texture (priv->surface);
      clutter_actor_get_scale (CLUTTER_ACTOR (stex), &actor_scale, NULL);
      cursor_texture_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);

      *out_cursor_scale = actor_scale / cursor_texture_scale;
    }

  if (out_relative_cursor_position)
    {
      MetaShapedTexture *stex;

      stex = meta_surface_actor_get_texture (priv->surface);
      clutter_actor_transform_stage_point (CLUTTER_ACTOR (stex),
                                           cursor_position->x,
                                           cursor_position->y,
                                           &out_relative_cursor_position->x,
                                           &out_relative_cursor_position->y);
    }

  return TRUE;
}

static void
meta_window_actor_capture_into (MetaScreenCastWindow *screen_cast_window,
                                MetaRectangle        *bounds,
                                uint8_t              *data)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  cairo_surface_t *image;
  uint8_t *cr_data;
  int cr_stride;
  int cr_width;
  int cr_height;
  int bpp = 4;

  if (meta_window_actor_is_destroyed (window_actor))
    return;

  image = meta_surface_actor_get_image (priv->surface, bounds);
  cr_data = cairo_image_surface_get_data (image);
  cr_width = cairo_image_surface_get_width (image);
  cr_height = cairo_image_surface_get_height (image);
  cr_stride = cairo_image_surface_get_stride (image);

  if (cr_width < bounds->width || cr_height < bounds->height)
    {
      uint8_t *src, *dst;
      src = cr_data;
      dst = data;

      for (int i = 0; i < cr_height; i++)
        {
          memcpy (dst, src, cr_stride);
          if (cr_width < bounds->width)
            memset (dst + cr_stride, 0, (bounds->width * bpp) - cr_stride);

          src += cr_stride;
          dst += bounds->width * bpp;
        }

      for (int i = cr_height; i < bounds->height; i++)
        {
          memset (dst, 0, bounds->width * bpp);
          dst += bounds->width * bpp;
        }
    }
  else
    {
      memcpy (data, cr_data, cr_height * cr_stride);
    }

  cairo_surface_destroy (image);
}

static gboolean
meta_window_actor_has_damage (MetaScreenCastWindow *screen_cast_window)
{
  return clutter_actor_has_damage (CLUTTER_ACTOR (screen_cast_window));
}

static void
screen_cast_window_iface_init (MetaScreenCastWindowInterface *iface)
{
  iface->get_frame_bounds = meta_window_actor_get_frame_bounds;
  iface->transform_relative_position = meta_window_actor_transform_relative_position;
  iface->transform_cursor_position = meta_window_actor_transform_cursor_position;
  iface->capture_into = meta_window_actor_capture_into;
  iface->has_damage = meta_window_actor_has_damage;
}
