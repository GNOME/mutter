/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * MetaWindowActor:
 *
 * An actor representing a top-level window in the scene graph
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

#include <math.h>
#include <string.h>

#include "backends/meta-screen-cast-window.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-surface-actor.h"
#include "compositor/meta-window-actor-private.h"
#include "core/boxes-private.h"
#include "core/window-private.h"
#include "meta/window.h"

#ifdef HAVE_X11_CLIENT
#include "compositor/meta-surface-actor-x11.h"
#endif

#ifdef HAVE_WAYLAND
#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-surface-private.h"
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

  gulong stage_views_changed_id;

  MetaSurfaceActor *surface;

  GPtrArray *surface_actors;

  int geometry_scale;

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
  guint             screen_cast_usage_count;

  guint		    visible                : 1;
  guint		    disposed               : 1;

  guint		    needs_destroy	   : 1;

  guint             updates_frozen         : 1;
  guint             first_frame_state      : 2; /* FirstFrameState */
} MetaWindowActorPrivate;

enum
{
  FIRST_FRAME,
  EFFECTS_COMPLETED,
  DAMAGED,
  THAWED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

enum
{
  PROP_0,

  PROP_META_WINDOW,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

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

static MetaSurfaceActor * meta_window_actor_real_get_scanout_candidate (MetaWindowActor *self);

static void meta_window_actor_real_assign_surface_actor (MetaWindowActor  *self,
                                                         MetaSurfaceActor *surface_actor);

static void on_cloned (ClutterActor *actor,
                       ClutterClone *clone);
static void on_decloned (ClutterActor *actor,
                         ClutterClone *clone);

static void screen_cast_window_iface_init (MetaScreenCastWindowInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaWindowActor, meta_window_actor, CLUTTER_TYPE_ACTOR,
                                  G_ADD_PRIVATE (MetaWindowActor)
                                  G_IMPLEMENT_INTERFACE (META_TYPE_SCREEN_CAST_WINDOW, screen_cast_window_iface_init));

static void
meta_window_actor_class_init (MetaWindowActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose      = meta_window_actor_dispose;
  object_class->set_property = meta_window_actor_set_property;
  object_class->get_property = meta_window_actor_get_property;
  object_class->constructed  = meta_window_actor_constructed;

  klass->get_scanout_candidate = meta_window_actor_real_get_scanout_candidate;
  klass->assign_surface_actor = meta_window_actor_real_assign_surface_actor;

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

  /**
   * MetaWindowActor::damaged:
   * @actor: the #MetaWindowActor instance
   *
   * Notify that one or more of the surfaces of the window have been damaged.
   */
  signals[DAMAGED] =
    g_signal_new ("damaged",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * MetaWindowActor::thawed:
   * @actor: the #MetaWindowActor instance
   */
  signals[THAWED] =
    g_signal_new ("thawed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  obj_props[PROP_META_WINDOW] =
    g_param_spec_object ("meta-window", NULL, NULL,
                         META_TYPE_WINDOW,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_window_actor_init (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  priv->surface_actors = g_ptr_array_new ();
  priv->geometry_scale = 1;

  g_signal_connect (self, "cloned",
                    G_CALLBACK (on_cloned), NULL);
  g_signal_connect (self, "decloned",
                    G_CALLBACK (on_decloned), NULL);
}

static void
window_appears_focused_notify (MetaWindow *mw,
                               GParamSpec *arg1,
                               gpointer    data)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (data));
}

gboolean
meta_window_actor_is_opaque (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  if (window->opacity != 0xff)
    return FALSE;

  if (!priv->surface)
    return FALSE;

  return meta_surface_actor_is_opaque (priv->surface);
}

gboolean
meta_window_actor_is_frozen (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->surface == NULL || priv->freeze_count > 0;
}

void
meta_window_actor_update_regions (MetaWindowActor *self)
{
  META_WINDOW_ACTOR_GET_CLASS (self)->update_regions (self);
}

gboolean
meta_window_actor_can_freeze_commits (MetaWindowActor *self)
{
  g_return_val_if_fail (META_IS_WINDOW_ACTOR (self), FALSE);

  return META_WINDOW_ACTOR_GET_CLASS (self)->can_freeze_commits (self);
}

static void
meta_window_actor_set_frozen (MetaWindowActor *self,
                              gboolean         frozen)
{
  META_WINDOW_ACTOR_GET_CLASS (self)->set_frozen (self, frozen);
}

/**
 * meta_window_actor_freeze:
 * @self: The #MetaWindowActor
 *
 * Freezes the #MetaWindowActor, which inhibits updates and geometry
 * changes of the window. This property is refcounted, so make sure
 * to call meta_window_actor_thaw() the exact same amount of times
 * as this function to allow updates again.
 */
void
meta_window_actor_freeze (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv;

  g_return_if_fail (META_IS_WINDOW_ACTOR (self));

  priv = meta_window_actor_get_instance_private (self);

  if (priv->freeze_count == 0 && priv->surface)
    meta_window_actor_set_frozen (self, TRUE);

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
    meta_window_actor_set_frozen (self, FALSE);

  /* We sometimes ignore moves and resizes on frozen windows */
  meta_window_actor_sync_actor_geometry (self, FALSE);
}

/**
 * meta_window_actor_thaw:
 * @self: The #MetaWindowActor
 *
 * Thaws/unfreezes the #MetaWindowActor to allow updates and geometry
 * changes after a window was frozen using meta_window_actor_freeze().
 */
void
meta_window_actor_thaw (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv;

  g_return_if_fail (META_IS_WINDOW_ACTOR (self));

  priv = meta_window_actor_get_instance_private (self);

  if (priv->freeze_count <= 0)
    g_error ("Error in freeze/thaw accounting");

  priv->freeze_count--;
  if (priv->freeze_count > 0)
    return;

  /* We still might be frozen due to lack of a MetaSurfaceActor */
  if (meta_window_actor_is_frozen (self))
    return;

  meta_window_actor_sync_thawed_state (self);

  g_signal_emit (self, signals[THAWED], 0);
}

static void
meta_window_actor_real_assign_surface_actor (MetaWindowActor  *self,
                                             MetaSurfaceActor *surface_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->surface)
    meta_window_actor_remove_surface_actor (self, priv->surface);

  g_clear_object (&priv->surface);
  priv->surface = g_object_ref_sink (surface_actor);

  meta_window_actor_add_surface_actor (self, surface_actor);

  if (meta_window_actor_is_frozen (self))
    meta_window_actor_set_frozen (self, TRUE);
  else
    meta_window_actor_sync_thawed_state (self);
}

void
meta_window_actor_assign_surface_actor (MetaWindowActor  *self,
                                        MetaSurfaceActor *surface_actor)
{
  META_WINDOW_ACTOR_GET_CLASS (self)->assign_surface_actor (self,
                                                            surface_actor);
}

static void
is_surface_actor_obscured_changed (MetaSurfaceActor *surface_actor,
                                   GParamSpec       *pspec,
                                   MetaWindowActor  *window_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  if (meta_surface_actor_is_obscured (surface_actor))
    meta_window_uninhibit_suspend_state (priv->window);
  else
    meta_window_inhibit_suspend_state (priv->window);
}

static void
disconnect_surface_actor_from (MetaSurfaceActor *surface_actor,
                               MetaWindowActor  *window_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  g_signal_handlers_disconnect_by_func (surface_actor,
                                        is_surface_actor_obscured_changed,
                                        window_actor);
  if (!meta_surface_actor_is_obscured (surface_actor))
    meta_window_uninhibit_suspend_state (priv->window);
}

void
meta_window_actor_add_surface_actor (MetaWindowActor  *window_actor,
                                     MetaSurfaceActor *surface_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  g_signal_connect (surface_actor,
                    "notify::is-obscured",
                    G_CALLBACK (is_surface_actor_obscured_changed),
                    window_actor);
  if (!meta_surface_actor_is_obscured (surface_actor))
    meta_window_inhibit_suspend_state (priv->window);
  g_ptr_array_add (priv->surface_actors, surface_actor);
}

void
meta_window_actor_remove_surface_actor (MetaWindowActor  *window_actor,
                                        MetaSurfaceActor *surface_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  disconnect_surface_actor_from (surface_actor, window_actor);
  g_ptr_array_remove (priv->surface_actors, surface_actor);
}

static void
on_clone_notify_mapped (ClutterClone    *clone,
                        GParamSpec      *pspec,
                        MetaWindowActor *window_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  if (clutter_actor_is_mapped (CLUTTER_ACTOR (clone)))
    meta_window_inhibit_suspend_state (priv->window);
  else
    meta_window_uninhibit_suspend_state (priv->window);
}

static void
on_cloned (ClutterActor *actor,
           ClutterClone *clone)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (actor);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  g_signal_connect (clone, "notify::mapped",
                    G_CALLBACK (on_clone_notify_mapped), actor);
  if (clutter_actor_is_mapped (CLUTTER_ACTOR (clone)))
    meta_window_inhibit_suspend_state (priv->window);
}

static void
on_decloned (ClutterActor *actor,
             ClutterClone *clone)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (actor);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  g_signal_handlers_disconnect_by_func (clone, on_clone_notify_mapped, actor);
  if (clutter_actor_is_mapped (CLUTTER_ACTOR (clone)) &&
      priv->window)
    meta_window_uninhibit_suspend_state (priv->window);
}

static void
init_surface_actor (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;
  MetaSurfaceActor *surface_actor = NULL;

#ifdef HAVE_X11_CLIENT
  if (!meta_is_wayland_compositor ())
    {
      surface_actor = meta_surface_actor_x11_new (window);
    }
  else
#endif
#ifdef HAVE_WAYLAND
    {
      MetaWaylandSurface *surface = meta_window_get_wayland_surface (window);
      surface_actor = surface ? meta_wayland_surface_get_actor (surface) : NULL;
    }
#endif

  if (surface_actor)
    meta_window_actor_assign_surface_actor (self, surface_actor);
}

static void
on_stage_views_changed (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  meta_compositor_window_actor_stage_views_changed (priv->compositor);
}

static void
meta_window_actor_constructed (GObject *object)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  priv->compositor = window->display->compositor;

  priv->stage_views_changed_id =
    g_signal_connect (self,
                      "stage-views-changed",
                      G_CALLBACK (on_stage_views_changed),
                      NULL);

  /* Hang our compositor window state off the MetaWindow for fast retrieval */
  meta_window_set_compositor_private (window, object);

  init_surface_actor (self);

  meta_window_actor_update_opacity (self);

  meta_window_actor_sync_updates_frozen (self);

  if (meta_window_actor_is_frozen (self))
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
    {
      G_OBJECT_CLASS (meta_window_actor_parent_class)->dispose (object);
      return;
    }

  priv->disposed = TRUE;

  g_ptr_array_foreach (priv->surface_actors,
                       (GFunc) disconnect_surface_actor_from,
                       self);
  g_clear_pointer (&priv->surface_actors, g_ptr_array_unref);
  g_clear_signal_handler (&priv->stage_views_changed_id, self);

  meta_compositor_remove_window_actor (compositor, self);

  g_clear_object (&priv->window);

  g_clear_object (&priv->surface);

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/**
 * meta_window_actor_get_meta_window:
 * @self: a #MetaWindowActor
 *
 * Gets the #MetaWindow object that the the #MetaWindowActor is displaying
 *
 * Return value: (transfer none) (nullable): the displayed #MetaWindow
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
 * Return value: (transfer none) (nullable): the #ClutterActor for the contents
 */
MetaShapedTexture *
meta_window_actor_get_texture (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->surface)
    return meta_surface_actor_get_texture (priv->surface);
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
 * Return value: (transfer none) (nullable): the #MetaSurfaceActor for the
 * contents
 */
MetaSurfaceActor *
meta_window_actor_get_surface (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->surface;
}

static MetaSurfaceActor *
meta_window_actor_real_get_scanout_candidate (MetaWindowActor *self)
{
  return NULL;
}

MetaSurfaceActor *
meta_window_actor_get_scanout_candidate (MetaWindowActor *self)
{
  return META_WINDOW_ACTOR_GET_CLASS (self)->get_scanout_candidate (self);
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
  MetaPluginManager *plugin_mgr =
    meta_compositor_get_plugin_manager (compositor);
  gint *counter = NULL;
  gboolean use_freeze_thaw = FALSE;

  g_assert (plugin_mgr != NULL);

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

  if (!meta_plugin_manager_event_simple (plugin_mgr, self, event))
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
    }
  else
    {
      g_signal_emit (self, signals[EFFECTS_COMPLETED], 0);
      meta_window_actor_sync_visibility (self);
      meta_window_actor_sync_actor_geometry (self, FALSE);
    }
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

MetaWindowActorChanges
meta_window_actor_sync_actor_geometry (MetaWindowActor *self,
                                       gboolean         did_placement)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MtkRectangle actor_rect;
  ClutterActor *actor = CLUTTER_ACTOR (self);
  MetaWindowActorChanges changes = 0;

  meta_window_get_buffer_rect (priv->window, &actor_rect);

  /* When running as a Wayland compositor we catch size changes when new
   * buffers are attached */
#ifdef HAVE_X11_CLIENT
  if (META_IS_SURFACE_ACTOR_X11 (priv->surface))
    meta_surface_actor_x11_set_size (META_SURFACE_ACTOR_X11 (priv->surface),
                                     actor_rect.width, actor_rect.height);
#endif
  /* Normally we want freezing a window to also freeze its position; this allows
   * windows to atomically move and resize together, either under app control,
   * or because the user is resizing from the left/top. But on initial placement
   * we need to assign a position, since immediately after the window
   * is shown, the map effect will go into effect and prevent further geometry
   * updates.
   */
  if (meta_window_actor_is_frozen (self) && !did_placement)
    return META_WINDOW_ACTOR_CHANGE_POSITION | META_WINDOW_ACTOR_CHANGE_SIZE;

  if (clutter_actor_has_allocation (actor))
    {
      ClutterActorBox box;
      float old_x, old_y;
      float old_width, old_height;

      clutter_actor_get_allocation_box (actor, &box);

      old_x = box.x1;
      old_y = box.y1;
      old_width = box.x2 - box.x1;
      old_height = box.y2 - box.y1;

      if (old_x != actor_rect.x || old_y != actor_rect.y)
        changes |= META_WINDOW_ACTOR_CHANGE_POSITION;

      if (old_width != actor_rect.width || old_height != actor_rect.height)
        changes |= META_WINDOW_ACTOR_CHANGE_SIZE;
    }
  else
    {
      changes = META_WINDOW_ACTOR_CHANGE_POSITION | META_WINDOW_ACTOR_CHANGE_SIZE;
    }

  if (changes & META_WINDOW_ACTOR_CHANGE_POSITION)
    clutter_actor_set_position (actor, actor_rect.x, actor_rect.y);

  if (changes & META_WINDOW_ACTOR_CHANGE_SIZE)
    clutter_actor_set_size (actor, actor_rect.width, actor_rect.height);

  META_WINDOW_ACTOR_GET_CLASS (self)->sync_geometry (self);

  return changes;
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

  if (event == META_PLUGIN_MAP)
    meta_window_actor_sync_actor_geometry (self, TRUE);

  if (meta_compositor_is_switching_workspace (compositor) ||
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
  if (meta_compositor_is_switching_workspace (compositor))
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
meta_window_actor_size_change (MetaWindowActor *self,
                               MetaSizeChange   which_change,
                               MtkRectangle    *old_frame_rect,
                               MtkRectangle    *old_buffer_rect)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;
  MetaPluginManager *plugin_mgr =
    meta_compositor_get_plugin_manager (compositor);

  priv->size_change_in_progress++;

  if (!meta_plugin_manager_event_size_change (plugin_mgr, self,
                                              which_change, old_frame_rect, old_buffer_rect))
    priv->size_change_in_progress--;
}

void
meta_window_actor_sync_visibility (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (clutter_actor_is_visible (CLUTTER_ACTOR (self)) != priv->visible)
    {
      if (priv->visible)
        clutter_actor_show (CLUTTER_ACTOR (self));
      else
        clutter_actor_hide (CLUTTER_ACTOR (self));
    }
}

void
meta_window_actor_before_paint (MetaWindowActor  *self,
                                ClutterStageView *stage_view)
{
  if (meta_window_actor_is_destroyed (self))
    return;

  META_WINDOW_ACTOR_GET_CLASS (self)->before_paint (self, stage_view);
}

void
meta_window_actor_after_paint (MetaWindowActor  *self,
                               ClutterStageView *stage_view)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  META_WINDOW_ACTOR_GET_CLASS (self)->after_paint (self, stage_view);

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

void
meta_window_actor_set_geometry_scale (MetaWindowActor *window_actor,
                                      int              geometry_scale)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  graphene_matrix_t child_transform;

  if (priv->geometry_scale == geometry_scale)
    return;

  priv->geometry_scale = geometry_scale;

  graphene_matrix_init_scale (&child_transform,
                              geometry_scale,
                              geometry_scale,
                              1);
  clutter_actor_set_child_transform (CLUTTER_ACTOR (window_actor),
                                     &child_transform);
}

int
meta_window_actor_get_geometry_scale (MetaWindowActor *window_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  return priv->geometry_scale;
}

static void
meta_window_actor_get_buffer_bounds (MetaScreenCastWindow *screen_cast_window,
                                     MtkRectangle         *bounds)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  MetaShapedTexture *stex;

  stex = meta_surface_actor_get_texture (priv->surface);
  *bounds = (MtkRectangle) {
    .width = floorf (meta_shaped_texture_get_unscaled_width (stex)),
    .height = floorf (meta_shaped_texture_get_unscaled_height (stex)),
  };
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
  MtkRectangle bounds;
  graphene_point3d_t v1 = { 0.f, }, v2 = { 0.f, };

  meta_window_actor_get_buffer_bounds (screen_cast_window, &bounds);

  v1.x = CLAMP ((float) x,
                bounds.x,
                bounds.x + bounds.width);
  v1.y = CLAMP ((float) y,
                bounds.y,
                bounds.y + bounds.height);

  clutter_actor_apply_transform_to_point (CLUTTER_ACTOR (priv->surface),
                                          &v1,
                                          &v2);

  *x_out = (double) v2.x;
  *y_out = (double) v2.y;
}

static gboolean
meta_window_actor_transform_cursor_position (MetaScreenCastWindow *screen_cast_window,
                                             MetaCursorSprite     *cursor_sprite,
                                             graphene_point_t     *cursor_position,
                                             float                *out_cursor_scale,
                                             MetaMonitorTransform *out_cursor_transform,
                                             graphene_point_t     *out_relative_cursor_position)
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
      MetaDisplay *display = meta_compositor_get_display (priv->compositor);
      MetaContext *context = meta_display_get_context (display);
      MetaBackend *backend = meta_context_get_backend (context);
      MetaLogicalMonitor *logical_monitor;
      float view_scale;
      float cursor_texture_scale;

      logical_monitor = meta_window_get_main_logical_monitor (window);

      if (meta_backend_is_stage_views_scaled (backend))
        view_scale = meta_logical_monitor_get_scale (logical_monitor);
      else
        view_scale = 1.0;

      cursor_texture_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);

      *out_cursor_scale = view_scale * cursor_texture_scale;
    }

  if (cursor_sprite &&
      meta_cursor_sprite_get_cogl_texture (cursor_sprite) &&
      out_cursor_transform)
    {
      *out_cursor_transform =
        meta_cursor_sprite_get_texture_transform (cursor_sprite);
    }

  if (out_relative_cursor_position)
    {
      MetaShapedTexture *stex = meta_surface_actor_get_texture (priv->surface);

      float unscaled_width = meta_shaped_texture_get_unscaled_width (stex);
      float unscaled_height = meta_shaped_texture_get_unscaled_height (stex);

      int width = meta_shaped_texture_get_width (stex);
      int height = meta_shaped_texture_get_height (stex);

      clutter_actor_transform_stage_point (CLUTTER_ACTOR (priv->surface),
                                           cursor_position->x,
                                           cursor_position->y,
                                           &out_relative_cursor_position->x,
                                           &out_relative_cursor_position->y);

      if (width != 0)
        out_relative_cursor_position->x *= unscaled_width / width;
      if (height != 0)
        out_relative_cursor_position->y *= unscaled_height / height;
    }

  return TRUE;
}

static void
meta_window_actor_capture_into (MetaScreenCastWindow *screen_cast_window,
                                MtkRectangle         *bounds,
                                uint8_t              *data)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  cairo_surface_t *image;
  uint8_t *cr_data;
  int cr_stride;
  int cr_width;
  int cr_height;
  int bpp = 4;

  if (meta_window_actor_is_destroyed (window_actor))
    return;

  image = meta_window_actor_get_image (window_actor, bounds);
  cr_data = cairo_image_surface_get_data (image);
  cr_width = cairo_image_surface_get_width (image);
  cr_height = cairo_image_surface_get_height (image);
  cr_stride = cairo_image_surface_get_stride (image);

  if (cr_width == bounds->width && cr_height == bounds->height)
    {
      memcpy (data, cr_data, cr_height * cr_stride);
    }
  else
    {
      int width = MIN (bounds->width, cr_width);
      int height = MIN (bounds->height, cr_height);
      int stride = width * bpp;
      uint8_t *src, *dst;

      src = cr_data;
      dst = data;

      for (int i = 0; i < height; i++)
        {
          memcpy (dst, src, stride);
          if (width < bounds->width)
            memset (dst + stride, 0, (bounds->width * bpp) - stride);

          src += cr_stride;
          dst += bounds->width * bpp;
        }

      for (int i = height; i < bounds->height; i++)
        {
          memset (dst, 0, bounds->width * bpp);
          dst += bounds->width * bpp;
        }
    }

  cairo_surface_destroy (image);
}

static gboolean
meta_window_actor_blit_to_framebuffer (MetaScreenCastWindow *screen_cast_window,
                                       MtkRectangle         *bounds,
                                       CoglFramebuffer      *framebuffer)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  ClutterPaintContext *paint_context;
  graphene_rect_t scaled_clip;
  CoglColor clear_color;
  MetaShapedTexture *stex;
  graphene_matrix_t transform, inverted_transform;
  float width, height;
  float unscaled_width, unscaled_height;

  if (meta_window_actor_is_destroyed (window_actor))
    return FALSE;

  if (!priv->surface)
    return FALSE;

  stex = meta_surface_actor_get_texture (priv->surface);

  width = meta_shaped_texture_get_width (stex);
  height = meta_shaped_texture_get_height (stex);

  if (width == 0 || height == 0)
    return FALSE;

  clutter_actor_get_relative_transformation_matrix (CLUTTER_ACTOR (priv->surface),
                                                    clutter_actor_get_stage (actor),
                                                    &transform);

  if (!graphene_matrix_inverse (&transform, &inverted_transform))
    return FALSE;

  unscaled_width = meta_shaped_texture_get_unscaled_width (stex);
  unscaled_height = meta_shaped_texture_get_unscaled_height (stex);

  clutter_actor_inhibit_culling (actor);

  cogl_color_init_from_4f (&clear_color, 0.0, 0.0, 0.0, 0.0);
  cogl_framebuffer_clear (framebuffer, COGL_BUFFER_BIT_COLOR, &clear_color);
  cogl_framebuffer_orthographic (framebuffer,
                                 0, 0,
                                 unscaled_width, unscaled_height,
                                 0, 1.0);
  cogl_framebuffer_set_viewport (framebuffer,
                                 0, 0,
                                 unscaled_width, unscaled_height);

  scaled_clip = mtk_rectangle_to_graphene_rect (bounds);
  graphene_rect_scale (&scaled_clip,
                       unscaled_width / width,
                       unscaled_height / height,
                       &scaled_clip);
  graphene_rect_intersection (&scaled_clip,
                              &GRAPHENE_RECT_INIT (0, 0, unscaled_width, unscaled_height),
                              &scaled_clip);

  cogl_framebuffer_push_rectangle_clip (framebuffer,
                                        scaled_clip.origin.x, scaled_clip.origin.y,
                                        scaled_clip.origin.x + scaled_clip.size.width,
                                        scaled_clip.origin.y + scaled_clip.size.height);

  cogl_framebuffer_push_matrix (framebuffer);
  cogl_framebuffer_scale (framebuffer,
                          unscaled_width / width,
                          unscaled_height / height,
                          1);
  cogl_framebuffer_transform (framebuffer, &inverted_transform);

  paint_context =
    clutter_paint_context_new_for_framebuffer (framebuffer, NULL,
                                               CLUTTER_PAINT_FLAG_NONE);
  clutter_actor_paint (actor, paint_context);
  clutter_paint_context_destroy (paint_context);

  cogl_framebuffer_pop_matrix (framebuffer);
  cogl_framebuffer_pop_clip (framebuffer);

  clutter_actor_uninhibit_culling (actor);

  return TRUE;
}

static gboolean
meta_window_actor_has_damage (MetaScreenCastWindow *screen_cast_window)
{
  return clutter_actor_has_damage (CLUTTER_ACTOR (screen_cast_window));
}

static void
meta_window_actor_inc_screen_cast_usage (MetaScreenCastWindow *screen_cast_window)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  priv->screen_cast_usage_count++;
}

static void
meta_window_actor_dec_screen_cast_usage (MetaScreenCastWindow *screen_cast_window)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  priv->screen_cast_usage_count--;
}

static void
screen_cast_window_iface_init (MetaScreenCastWindowInterface *iface)
{
  iface->get_buffer_bounds = meta_window_actor_get_buffer_bounds;
  iface->transform_relative_position = meta_window_actor_transform_relative_position;
  iface->transform_cursor_position = meta_window_actor_transform_cursor_position;
  iface->capture_into = meta_window_actor_capture_into;
  iface->blit_to_framebuffer = meta_window_actor_blit_to_framebuffer;
  iface->has_damage = meta_window_actor_has_damage;
  iface->inc_usage = meta_window_actor_inc_screen_cast_usage;
  iface->dec_usage = meta_window_actor_dec_screen_cast_usage;
}

gboolean
meta_window_actor_is_streaming (MetaWindowActor *window_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  return priv->screen_cast_usage_count > 0;
}

MetaWindowActor *
meta_window_actor_from_actor (ClutterActor *actor)
{
  if (!META_IS_SURFACE_ACTOR (actor))
    return NULL;

  do
    {
      actor = clutter_actor_get_parent (actor);

      if (META_IS_WINDOW_ACTOR (actor))
        return META_WINDOW_ACTOR (actor);
    }
  while (actor != NULL);

  return NULL;
}

void
meta_window_actor_notify_damaged (MetaWindowActor *window_actor)
{
  g_signal_emit (window_actor, signals[DAMAGED], 0);
}

static CoglFramebuffer *
create_framebuffer_from_window_actor (MetaWindowActor  *self,
                                      MtkRectangle     *clip,
                                      GError          **error)
{
  MetaWindowActorPrivate *priv = meta_window_actor_get_instance_private (self);
  ClutterActor *actor = CLUTTER_ACTOR (self);
  MetaDisplay *display = meta_compositor_get_display (priv->compositor);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglTexture *texture;
  CoglOffscreen *offscreen;
  CoglFramebuffer *framebuffer;
  CoglColor clear_color;
  ClutterPaintContext *paint_context;
  float resource_scale;

  resource_scale = clutter_actor_get_resource_scale (actor);

  texture = cogl_texture_2d_new_with_size (cogl_context,
                                           clip->width * resource_scale,
                                           clip->height * resource_scale);
  if (!texture)
    return NULL;

  cogl_primitive_texture_set_auto_mipmap (texture, FALSE);

  offscreen = cogl_offscreen_new_with_texture (texture);
  framebuffer = COGL_FRAMEBUFFER (offscreen);

  g_object_unref (texture);

  if (!cogl_framebuffer_allocate (framebuffer, error))
    {
      g_object_unref (framebuffer);
      return NULL;
    }

  cogl_color_init_from_4f (&clear_color, 0.0, 0.0, 0.0, 0.0);
  cogl_framebuffer_clear (framebuffer, COGL_BUFFER_BIT_COLOR, &clear_color);
  cogl_framebuffer_orthographic (framebuffer, 0, 0, clip->width, clip->height,
                                 0, 1.0);
  cogl_framebuffer_translate (framebuffer, -clip->x, -clip->y, 0);

  paint_context =
    clutter_paint_context_new_for_framebuffer (framebuffer, NULL,
                                               CLUTTER_PAINT_FLAG_NONE);
  clutter_actor_paint (actor, paint_context);
  clutter_paint_context_destroy (paint_context);

  return framebuffer;
}

static gboolean
meta_window_actor_is_single_surface_actor (MetaWindowActor *self)
{
  return META_WINDOW_ACTOR_GET_CLASS (self)->is_single_surface_actor (self);
}

/**
 * meta_window_actor_get_image:
 * @self: A #MetaWindowActor
 * @clip: (nullable): A clipping rectangle, to help prevent extra processing.
 * In the case that the clipping rectangle is partially or fully
 * outside the bounds of the actor, the rectangle will be clipped.
 *
 * Flattens the layers of @self into one ARGB32 image by alpha blending
 * the images, and returns the flattened image.
 *
 * Returns: (nullable) (transfer full): a new cairo surface to be freed with
 * cairo_surface_destroy().
 */
cairo_surface_t *
meta_window_actor_get_image (MetaWindowActor *self,
                             MtkRectangle    *clip)
{
  MetaWindowActorPrivate *priv = meta_window_actor_get_instance_private (self);
  ClutterActor *actor = CLUTTER_ACTOR (self);
  MetaShapedTexture *stex;
  cairo_surface_t *surface = NULL;
  CoglFramebuffer *framebuffer;
  MtkRectangle framebuffer_clip;
  float resource_scale;
  float x, y, width, height;

  if (!priv->surface)
    return NULL;

  clutter_actor_inhibit_culling (actor);

  stex = meta_surface_actor_get_texture (priv->surface);
  if (!meta_shaped_texture_should_get_via_offscreen (stex) &&
      meta_window_actor_is_single_surface_actor (self))
    {
      MtkRectangle *surface_clip = NULL;

      if (clip)
        {
          int geometry_scale;

          geometry_scale =
            meta_window_actor_get_geometry_scale (self);

          surface_clip = g_alloca (sizeof (MtkRectangle));
          surface_clip->x = clip->x / geometry_scale,
          surface_clip->y = clip->y / geometry_scale;
          surface_clip->width = clip->width / geometry_scale;
          surface_clip->height = clip->height / geometry_scale;
        }

      surface = meta_shaped_texture_get_image (stex, surface_clip);
      goto out;
    }

  clutter_actor_get_position (actor, &x, &y);
  clutter_actor_get_size (actor, &width, &height);

  if (width == 0 || height == 0)
    goto out;

  framebuffer_clip = (MtkRectangle) {
    .x = floorf (x),
    .y = floorf (y),
    .width = ceilf (width),
    .height = ceilf (height),
  };

  if (clip)
    {
      MtkRectangle tmp_clip;
      MtkRectangle intersected_clip;

      tmp_clip = *clip;
      tmp_clip.x += floorf (x);
      tmp_clip.y += floorf (y);
      if (!mtk_rectangle_intersect (&framebuffer_clip,
                                    &tmp_clip,
                                    &intersected_clip))
        goto out;

      framebuffer_clip = intersected_clip;
    }

  framebuffer = create_framebuffer_from_window_actor (self,
                                                      &framebuffer_clip,
                                                      NULL);
  if (!framebuffer)
    goto out;

  resource_scale = clutter_actor_get_resource_scale (actor);
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        framebuffer_clip.width *
                                        resource_scale,
                                        framebuffer_clip.height *
                                        resource_scale);
  cogl_framebuffer_read_pixels (framebuffer,
                                0, 0,
                                framebuffer_clip.width * resource_scale,
                                framebuffer_clip.height * resource_scale,
                                COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                cairo_image_surface_get_data (surface));

  g_object_unref (framebuffer);

  cairo_surface_mark_dirty (surface);

out:
  clutter_actor_uninhibit_culling (actor);
  return surface;
}

/**
 * meta_window_actor_paint_to_content:
 * @self: A #MetaWindowActor
 * @clip: (nullable): A clipping rectangle, in actor coordinates, to help
 * prevent extra processing.
 * In the case that the clipping rectangle is partially or fully
 * outside the bounds of the actor, the rectangle will be clipped.
 * @error: A #GError to catch exceptional errors or %NULL.
 *
 * Returns: (nullable) (transfer full): a new #ClutterContent
 */
ClutterContent *
meta_window_actor_paint_to_content (MetaWindowActor  *self,
                                    MtkRectangle     *clip,
                                    GError          **error)
{
  MetaWindowActorPrivate *priv = meta_window_actor_get_instance_private (self);
  ClutterActor *actor = CLUTTER_ACTOR (self);
  ClutterContent *content = NULL;
  CoglFramebuffer *framebuffer;
  CoglTexture *texture;
  MtkRectangle framebuffer_clip;
  float x, y, width, height;

  if (!priv->surface)
    return NULL;

  clutter_actor_inhibit_culling (actor);

  clutter_actor_get_position (actor, &x, &y);
  clutter_actor_get_size (actor, &width, &height);

  if (width == 0 || height == 0)
    goto out;

  framebuffer_clip = (MtkRectangle) {
    .x = floorf (x),
    .y = floorf (y),
    .width = ceilf (width),
    .height = ceilf (height),
  };

  if (clip)
    {
      MtkRectangle tmp_clip;

      if (!mtk_rectangle_intersect (&framebuffer_clip, clip, &tmp_clip))
        goto out;

      framebuffer_clip = tmp_clip;
    }

  framebuffer = create_framebuffer_from_window_actor (self,
                                                      &framebuffer_clip,
                                                      error);
  if (!framebuffer)
    goto out;

  texture = cogl_offscreen_get_texture (COGL_OFFSCREEN (framebuffer));
  content = clutter_texture_content_new_from_texture (texture, NULL);

  g_object_unref (framebuffer);

out:
  clutter_actor_uninhibit_culling (actor);
  return content;
}
