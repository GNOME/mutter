#include <stdlib.h>
#include <stdio.h>

#include <glib.h>
#include <gmodule.h>

#include <clutter/clutter.h>
#include <clutter/clutter-keysyms.h>

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

  gint selected_index;
};

struct _KeyGroupClass
{
  ClutterActorClass parent_class;

  void (* activate) (KeyGroup     *group,
                     ClutterActor *child);
};

GType key_group_get_type (void);

int
test_binding_pool_main (int argc, char *argv[]);

const char *
test_binding_pool_describe (void);

G_DEFINE_TYPE (KeyGroup, key_group, CLUTTER_TYPE_ACTOR)

enum
{
  ACTIVATE,

  LAST_SIGNAL
};

static guint group_signals[LAST_SIGNAL] = { 0, };

static gboolean
key_group_action_move_left (KeyGroup            *self,
                            const gchar         *action_name,
                            guint                key_val,
                            ClutterModifierType  modifiers)
{
  gint n_children;

  g_debug ("%s: activated '%s' (k:%d, m:%d)",
           G_STRLOC,
           action_name,
           key_val,
           modifiers);

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

  g_debug ("%s: activated '%s' (k:%d, m:%d)",
           G_STRLOC,
           action_name,
           key_val,
           modifiers);

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

  g_debug ("%s: activated '%s' (k:%d, m:%d)",
           G_STRLOC,
           action_name,
           key_val,
           modifiers);

  if (self->selected_index == -1)
    return FALSE;

  child = clutter_actor_get_child_at_index (CLUTTER_ACTOR (self),
                                            self->selected_index);

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
  ClutterBindingPool *pool;
  gboolean res;

  pool = clutter_binding_pool_find (G_OBJECT_TYPE_NAME (actor));
  g_assert (pool != NULL);

  res = clutter_binding_pool_activate (pool,
                                       clutter_event_get_key_symbol (event),
                                       clutter_event_get_state (event),
                                       G_OBJECT (actor));

  /* if we activate a key binding, redraw the actor */
  if (res)
    clutter_actor_queue_redraw (actor);

  return res ? CLUTTER_EVENT_STOP : CLUTTER_EVENT_PROPAGATE;
}

static void
key_group_paint (ClutterActor        *actor,
                 ClutterPaintContext *paint_context)
{
  KeyGroup *self = KEY_GROUP (actor);
  ClutterActorIter iter;
  ClutterActor *child;
  gint i = 0;
  CoglFramebuffer *framebuffer =
    clutter_paint_context_get_framebuffer (paint_context);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglPipeline *pipeline;

  pipeline = cogl_pipeline_new (ctx);

  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      /* paint the selection rectangle */
      if (i == self->selected_index)
        {
          ClutterActorBox box = { 0, };
          CoglColor color;

          clutter_actor_get_allocation_box (child, &box);

          box.x1 -= 2;
          box.y1 -= 2;
          box.x2 += 2;
          box.y2 += 2;

          cogl_color_init_from_4f (&color, 1.0, 1.0, 0.0, 224.0 / 255.0);
          cogl_pipeline_set_color (pipeline, &color);

          cogl_framebuffer_draw_rectangle (framebuffer, pipeline,
                                           box.x1, box.y1, box.x2, box.y2);
        }

      clutter_actor_paint (child, paint_context);

      i += 1;
    }
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
  self->selected_index = -1;
}

static void
on_key_group_activate (KeyGroup     *group,
                       ClutterActor *child)
{
  g_print ("Child '%s' activated!\n", clutter_actor_get_name (child));
}

G_MODULE_EXPORT int
test_binding_pool_main (int argc, char *argv[])
{
  ClutterActor *stage, *key_group;
  gint group_x, group_y;

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Key Binding Pool");
  g_signal_connect (stage,
                    "button-press-event", G_CALLBACK (clutter_test_quit),
                    NULL);
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  key_group = g_object_new (TYPE_KEY_GROUP, NULL);
  clutter_actor_add_child (stage, key_group);

  /* add three rectangles to the key group */
  clutter_actor_add_child (key_group,
                           g_object_new (CLUTTER_TYPE_ACTOR,
                                         "background-color", &CLUTTER_COLOR_INIT (255, 0, 0, 255),
                                         "name", "Red Rectangle",
                                         "width", 100.0,
                                         "height", 100.0,
                                         "x", 0.0,
                                         "y", 0.0,
                                         NULL));
  clutter_actor_add_child (key_group,
                           g_object_new (CLUTTER_TYPE_ACTOR,
                                         "background-color", &CLUTTER_COLOR_INIT (0, 255, 0, 255),
                                         "name", "Green Rectangle",
                                         "width", 100.0,
                                         "height", 100.0,
                                         "x", 125.0,
                                         "y", 0.0,
                                         NULL));
  clutter_actor_add_child (key_group,
                           g_object_new (CLUTTER_TYPE_ACTOR,
                                         "background-color", &CLUTTER_COLOR_INIT (0, 0, 255, 255),
                                         "name", "Blue Rectangle",
                                         "width", 100.0,
                                         "height", 100.0,
                                         "x", 250.0,
                                         "y", 0.0,
                                         NULL));

  g_signal_connect (key_group,
                    "activate", G_CALLBACK (on_key_group_activate),
                    NULL);

  group_x =
    (clutter_actor_get_width (stage) - clutter_actor_get_width (key_group))
    / 2;
  group_y =
    (clutter_actor_get_height (stage) - clutter_actor_get_height (key_group))
    / 2;

  clutter_actor_set_position (key_group, group_x, group_y);
  clutter_actor_set_reactive (key_group, TRUE);

  clutter_stage_set_key_focus (CLUTTER_STAGE (stage), key_group);

  clutter_actor_show (stage);

  clutter_test_main ();

  return EXIT_SUCCESS;
}

G_MODULE_EXPORT const char *
test_binding_pool_describe (void)
{
  return "Binding pools example";
}
