#include <stdlib.h>
#include <gmodule.h>
#include <clutter/clutter.h>
#include <clutter/clutter-pango.h>

#include "tests/clutter-test-utils.h"

typedef struct _ColorContent {
  GObject parent_instance;

  double red;
  double green;
  double blue;
  double alpha;

  float padding;
} ColorContent;

typedef struct _ColorContentClass {
  GObjectClass parent_class;
} ColorContentClass;

static void clutter_content_iface_init (ClutterContentInterface *iface);

GType color_content_get_type (void);

int
test_content_main (int argc, char *argv[]);

const char *
test_content_describe (void);

G_DEFINE_TYPE_WITH_CODE (ColorContent, color_content, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT,
                                                clutter_content_iface_init))

static void
color_content_paint_content (ClutterContent      *content,
                             ClutterActor        *actor,
                             ClutterPaintNode    *root,
                             ClutterPaintContext *paint_context)
{
  ColorContent *self = (ColorContent *) content;
  ClutterActorBox box, content_box;
  CoglColor color;
  PangoLayout *layout;
  PangoRectangle logical;
  ClutterPaintNode *node;

#if 0
  g_debug ("Painting content [%p] "
           "{ r:%.2f, g:%.2f, b:%.2f, a:%.2f } "
           "for actor [%p] (context: [%p])",
           content,
           self->red,
           self->green,
           self->blue,
           self->alpha,
           actor, context);
#endif

  clutter_actor_get_content_box (actor, &content_box);

  box = content_box;
  box.x1 += self->padding;
  box.y1 += self->padding;
  box.x2 -= self->padding;
  box.y2 -= self->padding;

  color.alpha = (uint8_t) (self->alpha * 255);

  color.red = (uint8_t) (self->red * 255);
  color.green = (uint8_t) (self->green * 255);
  color.blue = (uint8_t) (self->blue * 255);

  node = clutter_color_node_new (&color);
  clutter_paint_node_add_rectangle (node, &box);
  clutter_paint_node_add_child (root, node);
  clutter_paint_node_unref (node);

  color.red = (uint8_t) ((1.0 - self->red) * 255);
  color.green = (uint8_t) ((1.0 - self->green) * 255);
  color.blue = (uint8_t) ((1.0 - self->blue) * 255);

  layout = clutter_actor_create_pango_layout (actor, "A");
  pango_layout_get_pixel_extents (layout, NULL, &logical);

  node = clutter_text_node_new (layout, &color);

  /* top-left */
  box.x1 = clutter_actor_box_get_x (&content_box);
  box.y1 = clutter_actor_box_get_y (&content_box);
  box.x2 = box.x1 + logical.width;
  box.y2 = box.y1 + logical.height;
  clutter_paint_node_add_rectangle (node, &box);

  /* top-right */
  box.x1 = clutter_actor_box_get_x (&content_box)
         + clutter_actor_box_get_width (&content_box)
         - logical.width;
  box.y1 = clutter_actor_box_get_y (&content_box);
  box.x2 = box.x1 + logical.width;
  box.y2 = box.y1 + logical.height;
  clutter_paint_node_add_rectangle (node, &box);

  /* bottom-right */
  box.x1 = clutter_actor_box_get_x (&content_box)
         + clutter_actor_box_get_width (&content_box)
         - logical.width;
  box.y1 = clutter_actor_box_get_y (&content_box)
         + clutter_actor_box_get_height (&content_box)
         - logical.height;
  box.x2 = box.x1 + logical.width;
  box.y2 = box.y1 + logical.height;
  clutter_paint_node_add_rectangle (node, &box);

  /* bottom-left */
  box.x1 = clutter_actor_box_get_x (&content_box);
  box.y1 = clutter_actor_box_get_y (&content_box)
         + clutter_actor_box_get_height (&content_box)
         - logical.height;
  box.x2 = box.x1 + logical.width;
  box.y2 = box.y1 + logical.height;
  clutter_paint_node_add_rectangle (node, &box);

  /* center */
  box.x1 = clutter_actor_box_get_x (&content_box)
         + (clutter_actor_box_get_width (&content_box) - logical.width) / 2.0f;
  box.y1 = clutter_actor_box_get_y (&content_box)
         + (clutter_actor_box_get_height (&content_box) - logical.height) / 2.0f;
  box.x2 = box.x1 + logical.width;
  box.y2 = box.y1 + logical.height;
  clutter_paint_node_add_rectangle (node, &box);

  clutter_paint_node_add_child (root, node);
  clutter_paint_node_unref (node);

  g_object_unref (layout);
}

static void
clutter_content_iface_init (ClutterContentInterface *iface)
{
  iface->paint_content = color_content_paint_content;
}

static void
color_content_class_init (ColorContentClass *klass)
{
}

static void
color_content_init (ColorContent *self)
{
}

static ClutterContent *
color_content_new (double red,
                   double green,
                   double blue,
                   double alpha,
                   float  padding)
{
  ColorContent *self = g_object_new (color_content_get_type (), NULL);

  self->red = red;
  self->green = green;
  self->blue = blue;
  self->alpha = alpha;
  self->padding = padding;

  return (ClutterContent *) self;
}

G_MODULE_EXPORT int
test_content_main (int argc, char *argv[])
{
  ClutterActor *stage, *grid;
  ClutterContent *content;
  int i, n_rects;

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  clutter_actor_set_name (stage, "Stage");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);
  clutter_actor_show (stage);

  grid = clutter_actor_new ();
  clutter_actor_set_name (grid, "Grid");
  clutter_actor_set_margin_top (grid, 12);
  clutter_actor_set_margin_right (grid, 12);
  clutter_actor_set_margin_bottom (grid, 12);
  clutter_actor_set_margin_left (grid, 12);
  clutter_actor_set_layout_manager (grid, clutter_flow_layout_new (CLUTTER_ORIENTATION_HORIZONTAL));
  clutter_actor_add_constraint (grid, clutter_bind_constraint_new (stage, CLUTTER_BIND_SIZE, 0.0));
  clutter_actor_add_child (stage, grid);

  content = color_content_new (g_random_double_range (0.0, 1.0),
                               g_random_double_range (0.0, 1.0),
                               g_random_double_range (0.0, 1.0),
                               1.0,
                               2.0);

  n_rects = g_random_int_range (12, 24);
  for (i = 0; i < n_rects; i++)
    {
      ClutterActor *box = clutter_actor_new ();
      CoglColor bg_color = {
        g_random_int_range (0, 255),
        g_random_int_range (0, 255),
        g_random_int_range (0, 255),
        255
      };
      char *name, *color;

      color = cogl_color_to_string (&bg_color);
      name = g_strconcat ("Box <", color, ">", NULL);
      clutter_actor_set_name (box, name);

      g_free (name);
      g_free (color);

      clutter_actor_set_background_color (box, &bg_color);
      clutter_actor_set_content (box, content);
      clutter_actor_set_size (box, 64, 64);

      clutter_actor_add_child (grid, box);
    }

  clutter_test_main ();

  g_object_unref (content);

  return EXIT_SUCCESS;
}

G_MODULE_EXPORT const char *
test_content_describe (void)
{
  return "A simple test for ClutterContent";
}
