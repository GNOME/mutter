#include <string.h>

#include <clutter/clutter.h>
#include <clutter/clutter-mutter.h>

#include "tests/clutter-test-utils.h"

#define TYPE_KEY_GROUP                  (key_group_get_type ())
#define KEY_GROUP(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_KEY_GROUP, KeyGroup))
#define IS_KEY_GROUP(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_KEY_GROUP))
#define KEY_GROUP_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_KEY_GROUP, KeyGroupClass))
#define IS_KEY_GROUP_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_KEY_GROUP))

typedef struct _KeyGroup        KeyGroup;
typedef struct _KeyGroupClass   KeyGroupClass;

struct _KeyGroup
{
  ClutterActor parent_instance;

  ClutterVirtualInputDevice *keyboard;

  gint selected_index;
  gint serial;
};

struct _KeyGroupClass
{
  ClutterActorClass parent_class;

  void (* activate) (KeyGroup     *group,
                     ClutterActor *child);
};

GType key_group_get_type (void);

G_DEFINE_TYPE (KeyGroup, key_group, CLUTTER_TYPE_ACTOR)

enum
{
  ACTIVATE,

  LAST_SIGNAL
};

static guint group_signals[LAST_SIGNAL] = { 0, };

static void
wait_for_event (KeyGroup *group)
{
  int serial = group->serial;

  while (group->serial == serial)
    g_main_context_iteration (NULL, FALSE);
}

static gboolean
key_group_action_move_left (KeyGroup            *self,
                            const gchar         *action_name,
                            guint                key_val,
                            ClutterModifierType  modifiers)
{
  gint n_children;

  g_assert_cmpstr (action_name, ==, "move-left");
  g_assert_cmpint (key_val, ==, CLUTTER_KEY_Left);

  n_children = clutter_actor_get_n_children (CLUTTER_ACTOR (self));

  self->selected_index -= 1;

  if (self->selected_index < 0)
    self->selected_index = n_children - 1;

  return TRUE;
}

static gboolean
key_group_action_move_right (KeyGroup            *self,
                             const gchar         *action_name,
                             guint                key_val,
                             ClutterModifierType  modifiers)
{
  gint n_children;

  g_assert_cmpstr (action_name, ==, "move-right");
  g_assert_cmpint (key_val, ==, CLUTTER_KEY_Right);

  n_children = clutter_actor_get_n_children (CLUTTER_ACTOR (self));

  self->selected_index += 1;

  if (self->selected_index >= n_children)
    self->selected_index = 0;

  return TRUE;
}

static gboolean
key_group_action_activate (KeyGroup            *self,
                           const gchar         *action_name,
                           guint                key_val,
                           ClutterModifierType  modifiers)
{
  ClutterActor *child = NULL;

  g_assert_cmpstr (action_name, ==, "activate");
  g_assert_true (key_val == CLUTTER_KEY_Return ||
                 key_val == CLUTTER_KEY_KP_Enter ||
                 key_val == CLUTTER_KEY_ISO_Enter);

  if (self->selected_index == -1)
    return FALSE;

  child = clutter_actor_get_child_at_index (CLUTTER_ACTOR (self), self->selected_index);
  if (child != NULL)
    {
      g_signal_emit (self, group_signals[ACTIVATE], 0, child);
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
key_group_key_press (ClutterActor *actor,
                     ClutterEvent *event)
{
  KeyGroup *group = (KeyGroup *) actor;
  ClutterBindingPool *pool;
  gboolean res;

  pool = clutter_binding_pool_find (G_OBJECT_TYPE_NAME (actor));
  g_assert_nonnull (pool);

  res = clutter_binding_pool_activate (pool,
                                       clutter_event_get_key_symbol (event),
                                       clutter_event_get_state (event),
                                       G_OBJECT (actor));

  /* if we activate a key binding, redraw the actor */
  if (res)
    clutter_actor_queue_redraw (actor);

  group->serial++;

  return res;
}

static void
key_group_paint (ClutterActor        *actor,
                 ClutterPaintContext *paint_context)
{
  KeyGroup *self = KEY_GROUP (actor);
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_test_get_backend ());
  ClutterActorIter iter;
  ClutterActor *child;
  CoglPipeline *pipeline;
  CoglFramebuffer *framebuffer;
  CoglColor color;
  gint i = 0;

  pipeline = cogl_pipeline_new (ctx);
  cogl_color_init_from_4f (&color, 1.0f, 1.0f, 0.0f, 224.0f / 255.0f );
  cogl_pipeline_set_color (pipeline, &color);

  framebuffer = clutter_paint_context_get_framebuffer (paint_context);

  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      /* paint the selection rectangle */
      if (i == self->selected_index)
        {
          ClutterActorBox box = { 0, };

          clutter_actor_get_allocation_box (child, &box);

          box.x1 -= 2;
          box.y1 -= 2;
          box.x2 += 2;
          box.y2 += 2;

          cogl_framebuffer_draw_rectangle (framebuffer, pipeline,
                                           box.x1, box.y1, box.x2, box.y2);
        }

      clutter_actor_paint (child, paint_context);
    }

  g_object_unref (pipeline);
}

static void
key_group_class_init (KeyGroupClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  ClutterBindingPool *binding_pool;

  actor_class->paint = key_group_paint;
  actor_class->key_press_event = key_group_key_press;

  group_signals[ACTIVATE] =
    g_signal_new (g_intern_static_string ("activate"),
                  G_OBJECT_CLASS_TYPE (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (KeyGroupClass, activate),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);

  binding_pool = clutter_binding_pool_get_for_class (klass);

  clutter_binding_pool_install_action (binding_pool, "move-right",
                                       CLUTTER_KEY_Right, 0,
                                       G_CALLBACK (key_group_action_move_right),
                                       NULL, NULL);
  clutter_binding_pool_install_action (binding_pool, "move-left",
                                       CLUTTER_KEY_Left, 0,
                                       G_CALLBACK (key_group_action_move_left),
                                       NULL, NULL);
  clutter_binding_pool_install_action (binding_pool, "activate",
                                       CLUTTER_KEY_Return, 0,
                                       G_CALLBACK (key_group_action_activate),
                                       NULL, NULL);
  clutter_binding_pool_install_action (binding_pool, "activate",
                                       CLUTTER_KEY_KP_Enter, 0,
                                       G_CALLBACK (key_group_action_activate),
                                       NULL, NULL);
  clutter_binding_pool_install_action (binding_pool, "activate",
                                       CLUTTER_KEY_ISO_Enter, 0,
                                       G_CALLBACK (key_group_action_activate),
                                       NULL, NULL);
}

static void
key_group_init (KeyGroup *self)
{
  ClutterSeat *seat;

  self->selected_index = -1;

  seat = clutter_test_get_default_seat ();
  self->keyboard = clutter_seat_create_virtual_device (seat, CLUTTER_KEYBOARD_DEVICE);
}

static void
send_keyval (KeyGroup *group, int keyval)
{
  clutter_virtual_input_device_notify_keyval (group->keyboard,
                                              g_get_monotonic_time (),
                                              keyval,
                                              CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_keyval (group->keyboard,
                                              g_get_monotonic_time (),
                                              keyval,
                                              CLUTTER_KEY_STATE_RELEASED);

  wait_for_event (group);
}

static void
on_activate (KeyGroup     *key_group,
             ClutterActor *child,
             gpointer      data)
{
  gint _index = GPOINTER_TO_INT (data);

  g_assert_cmpint (key_group->selected_index, ==, _index);
}

static void
binding_pool (void)
{
  ClutterActor *stage;
  KeyGroup *key_group;

  key_group = g_object_new (TYPE_KEY_GROUP, NULL);
  g_object_ref_sink (key_group);

  clutter_actor_add_child (CLUTTER_ACTOR (key_group),
                           g_object_new (CLUTTER_TYPE_ACTOR,
                                         "width", 50.0,
                                         "height", 50.0,
                                         "x", 0.0, "y", 0.0,
                                         NULL));
  clutter_actor_add_child (CLUTTER_ACTOR (key_group),
                           g_object_new (CLUTTER_TYPE_ACTOR,
                                         "width", 50.0,
                                         "height", 50.0,
                                         "x", 75.0, "y", 0.0,
                                         NULL));
  clutter_actor_add_child (CLUTTER_ACTOR (key_group),
                           g_object_new (CLUTTER_TYPE_ACTOR,
                                         "width", 50.0,
                                         "height", 50.0,
                                         "x", 150.0, "y", 0.0,
                                         NULL));

  stage = clutter_test_get_stage ();
  clutter_actor_add_child (stage, CLUTTER_ACTOR (key_group));
  clutter_actor_set_reactive (CLUTTER_ACTOR (key_group), TRUE);
  clutter_actor_grab_key_focus (CLUTTER_ACTOR (key_group));

  g_assert_cmpint (key_group->selected_index, ==, -1);

  send_keyval (key_group, CLUTTER_KEY_Left);
  g_assert_cmpint (key_group->selected_index, ==, 2);

  send_keyval (key_group, CLUTTER_KEY_Left);
  g_assert_cmpint (key_group->selected_index, ==, 1);

  send_keyval (key_group, CLUTTER_KEY_Right);
  g_assert_cmpint (key_group->selected_index, ==, 2);

  send_keyval (key_group, CLUTTER_KEY_Right);
  g_assert_cmpint (key_group->selected_index, ==, 0);

  g_signal_connect (key_group,
                    "activate", G_CALLBACK (on_activate),
                    GINT_TO_POINTER (0));

  send_keyval (key_group, CLUTTER_KEY_Return);

  clutter_actor_destroy (CLUTTER_ACTOR (key_group));
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/binding-pool", binding_pool)
)
