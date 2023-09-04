/*
 * Copyright 2009 Sander Dijkhuis
 * Copyright 2014 Red Hat, Inc.
 * Copyright 2020 Endless Foundation.
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
 *
 * Portions adapted from gnome-shell/src/shell-global.c
 */


/**
 * MetaBackgroundContent:
 *
 * This class handles tracking and painting the root window background.
 *
 * By integrating with [class@Meta.WindowGroup] we can avoid painting parts of
 * the background that are obscured by other windows.
 *
 * The overall model drawing model of this content is that we have one
 * texture, or two interpolated textures, possibly with alpha or
 * margins that let the underlying background show through, blended
 * over a solid color or a gradient. The result of that combination
 * can then be affected by a "vignette" that darkens the background
 * away from a central point (or as a no-GLSL fallback, simply darkens
 * the background) and by overall opacity.
 *
 * As of GNOME 3.14, GNOME is only using a fraction of this when the
 * user sets the background through the control center - what can be
 * set is:
 *
 *  A single image without a border
 *  An animation of images without a border that blend together,
 *   with the blend changing every 4-5 minutes
 *  A solid color with a repeated noise texture blended over it
 *
 * This all is pretty easy to do in a fragment shader, except when:
 *
 *  A) We don't have GLSL - in this case, the operation of
 *     interpolating the two textures and blending the result over the
 *     background can't be expressed with Cogl's fixed-function layer
 *     combining (which is confined to what GL's texture environment
 *     combining can do) So we can only handle the above directly if
 *     there are no margins or alpha.
 *
 *  B) The image textures are sliced. Texture size limits on older
 *     hardware (pre-965 intel hardware, r300, etc.)  is often 2048,
 *     and it would be common to use a texture larger than this for a
 *     background and expect it to be scaled down. Cogl can compensate
 *     for this by breaking the texture up into multiple textures, but
 *     can't multitexture with sliced textures. So we can only handle
 *     the above if there's a single texture.
 *
 * However, even when we *can* represent everything in a single pass,
 * it's not necessarily efficient. If we want to draw a 1024x768
 * background, it's pretty inefficient to bilinearly texture from
 * two 2560x1440 images and mix that. So the drawing model we take
 * here is that MetaBackground generates a single texture (which
 * might be a 1x1 texture for a solid color, or a 1x2 texture for a
 * gradient, or a repeated texture for wallpaper, or a pre-rendered
 * texture the size of the screen), and we draw with that, possibly
 * adding the vignette and opacity.
 */

#include "config.h"

#include "compositor/meta-background-content-private.h"

#include "backends/meta-backend-private.h"
#include "clutter/clutter.h"
#include "compositor/clutter-utils.h"
#include "compositor/cogl-utils.h"
#include "compositor/meta-background-private.h"
#include "compositor/meta-cullable.h"
#include "meta/display.h"

typedef enum
{
  CHANGED_BACKGROUND = 1 << 0,
  CHANGED_EFFECTS = 1 << 2,
  CHANGED_VIGNETTE_PARAMETERS = 1 << 3,
  CHANGED_GRADIENT_PARAMETERS = 1 << 4,
  CHANGED_ROUNDED_CLIP_PARAMETERS = 1 << 5,
  CHANGED_ALL = 0xFFFF
} ChangedFlags;

#define GRADIENT_VERTEX_SHADER_DECLARATIONS                             \
"uniform vec2 scale;\n"                                                 \
"varying vec2 position;\n"                                              \

#define GRADIENT_VERTEX_SHADER_CODE                                     \
"position = cogl_tex_coord0_in.xy * scale;\n"                           \

#define GRADIENT_FRAGMENT_SHADER_DECLARATIONS                           \
"uniform float gradient_height_perc;\n"                                 \
"uniform float gradient_max_darkness;\n"                                \
"varying vec2 position;\n"                                              \

#define GRADIENT_FRAGMENT_SHADER_CODE                                                    \
"float min_brightness = 1.0 - gradient_max_darkness;\n"                                  \
"float gradient_y_pos = min(position.y, gradient_height_perc) / gradient_height_perc;\n" \
"float pixel_brightness = (1.0 - min_brightness) * gradient_y_pos + min_brightness;\n"   \
"cogl_color_out.rgb = cogl_color_out.rgb * pixel_brightness;\n"                          \

#define VIGNETTE_VERTEX_SHADER_DECLARATIONS                             \
"uniform vec2 scale;\n"                                                 \
"uniform vec2 offset;\n"                                                \
"varying vec2 position;\n"                                              \

#define VIGNETTE_VERTEX_SHADER_CODE                                     \
"position = cogl_tex_coord0_in.xy * scale + offset;\n"                  \

#define VIGNETTE_SQRT_2 "1.4142"

#define VIGNETTE_FRAGMENT_SHADER_DECLARATIONS                                                   \
"uniform float vignette_sharpness;\n"                                                           \
"varying vec2 position;\n"                                                                      \
"float rand(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453123); }\n"    \

#define VIGNETTE_FRAGMENT_SHADER_CODE                                          \
"float t = " VIGNETTE_SQRT_2 " * length(position);\n"                          \
"t = min(t, 1.0);\n"                                                           \
"float pixel_brightness = 1.0 - t * vignette_sharpness;\n"                     \
"cogl_color_out.rgb = cogl_color_out.rgb * pixel_brightness;\n"                \
"cogl_color_out.rgb += (rand(position) - 0.5) / 255.0;\n"                      \

#define ROUNDED_CLIP_FRAGMENT_SHADER_DECLARATIONS                            \
"uniform vec4 bounds;           // x, y: top left; z, w: bottom right     \n"\
"uniform float clip_radius;                                               \n"\
"uniform vec2 pixel_step;                                                 \n"\
"                                                                         \n"\
"float                                                                    \n"\
"rounded_rect_coverage (vec2 p)                                           \n"\
"{                                                                        \n"\
"  float center_left  = bounds.x + clip_radius;                           \n"\
"  float center_right = bounds.z - clip_radius;                           \n"\
"  float center_x;                                                        \n"\
"                                                                         \n"\
"  if (p.x < center_left)                                                 \n"\
"    center_x = center_left;                                              \n"\
"  else if (p.x > center_right)                                           \n"\
"    center_x = center_right;                                             \n"\
"  else                                                                   \n"\
"    return 1.0; // The vast majority of pixels exit early here           \n"\
"                                                                         \n"\
"  float center_top    = bounds.y + clip_radius;                          \n"\
"  float center_bottom = bounds.w - clip_radius;                          \n"\
"  float center_y;                                                        \n"\
"                                                                         \n"\
"  if (p.y < center_top)                                                  \n"\
"    center_y = center_top;                                               \n"\
"  else if (p.y > center_bottom)                                          \n"\
"    center_y = center_bottom;                                            \n"\
"  else                                                                   \n"\
"    return 1.0;                                                          \n"\
"                                                                         \n"\
"  vec2 delta = p - vec2 (center_x, center_y);                            \n"\
"  float dist_squared = dot (delta, delta);                               \n"\
"                                                                         \n"\
"  // Fully outside the circle                                            \n"\
"  float outer_radius = clip_radius + 0.5;                                \n"\
"  if (dist_squared >= (outer_radius * outer_radius))                     \n"\
"    return 0.0;                                                          \n"\
"                                                                         \n"\
"  // Fully inside the circle                                             \n"\
"  float inner_radius = clip_radius - 0.5;                                \n"\
"  if (dist_squared <= (inner_radius * inner_radius))                     \n"\
"    return 1.0;                                                          \n"\
"                                                                         \n"\
"  // Only pixels on the edge of the curve need expensive antialiasing    \n"\
"  return outer_radius - sqrt (dist_squared);                             \n"\
"}                                                                        \n"

#define ROUNDED_CLIP_FRAGMENT_SHADER_CODE                                    \
"vec2 texture_coord;                                                      \n"\
"                                                                         \n"\
"texture_coord = cogl_tex_coord0_in.xy / pixel_step;                      \n"\
"                                                                         \n"\
"cogl_color_out *= rounded_rect_coverage (texture_coord);                 \n"

typedef struct _MetaBackgroundLayer MetaBackgroundLayer;

typedef enum
{
  PIPELINE_VIGNETTE = (1 << 0),
  PIPELINE_BLEND = (1 << 1),
  PIPELINE_GRADIENT = (1 << 2),
  PIPELINE_ROUNDED_CLIP = (1 << 3),

  PIPELINE_ALL = (PIPELINE_VIGNETTE |
                  PIPELINE_BLEND |
                  PIPELINE_GRADIENT |
                  PIPELINE_ROUNDED_CLIP)
} PipelineFlags;

struct _MetaBackgroundContent
{
  GObject parent;

  MetaDisplay *display;
  int monitor;

  MetaBackground *background;

  gboolean gradient;
  double gradient_max_darkness;
  int gradient_height;

  gboolean vignette;
  double vignette_brightness;
  double vignette_sharpness;

  gboolean has_rounded_clip;
  float rounded_clip_radius;
  gboolean rounded_clip_bounds_set;
  graphene_rect_t rounded_clip_bounds;

  ChangedFlags changed;
  CoglPipeline *pipeline;
  PipelineFlags pipeline_flags;
  MtkRectangle texture_area;
  int texture_width, texture_height;

  MtkRegion *clip_region;
  MtkRegion *unobscured_region;
};

static void clutter_content_iface_init (ClutterContentInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaBackgroundContent,
                         meta_background_content,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT,
                                                clutter_content_iface_init));

enum
{
  PROP_0,
  PROP_META_DISPLAY,
  PROP_MONITOR,
  PROP_BACKGROUND,
  PROP_GRADIENT,
  PROP_GRADIENT_HEIGHT,
  PROP_GRADIENT_MAX_DARKNESS,
  PROP_VIGNETTE,
  PROP_VIGNETTE_SHARPNESS,
  PROP_VIGNETTE_BRIGHTNESS,
  PROP_ROUNDED_CLIP_RADIUS,
  N_PROPS,
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static void
set_clip_region (MetaBackgroundContent *self,
                 MtkRegion             *clip_region)
{
  g_clear_pointer (&self->clip_region, mtk_region_unref);
  if (clip_region)
    {
      if (mtk_region_is_empty (clip_region))
        self->clip_region = mtk_region_ref (clip_region);
      else
        self->clip_region = mtk_region_copy (clip_region);
    }
}

static void
set_unobscured_region (MetaBackgroundContent *self,
                       MtkRegion             *unobscured_region)
{
  g_clear_pointer (&self->unobscured_region, mtk_region_unref);
  if (unobscured_region)
    {
      if (mtk_region_is_empty (unobscured_region))
        self->unobscured_region = mtk_region_ref (unobscured_region);
      else
        self->unobscured_region = mtk_region_copy (unobscured_region);
    }
}

static void
invalidate_pipeline (MetaBackgroundContent *self,
                     ChangedFlags           changed)
{
  self->changed |= changed;
}

static void
on_background_changed (MetaBackground        *background,
                       MetaBackgroundContent *self)
{
  invalidate_pipeline (self, CHANGED_BACKGROUND);
  clutter_content_invalidate (CLUTTER_CONTENT (self));
}

static CoglPipeline *
make_pipeline (PipelineFlags pipeline_flags)
{
  static CoglPipeline *templates[PIPELINE_ALL + 1];
  CoglPipeline **templatep;

  g_assert (pipeline_flags < G_N_ELEMENTS (templates));

  templatep = &templates[pipeline_flags];
  if (*templatep == NULL)
    {
      /* Cogl automatically caches pipelines with no eviction policy,
       * so we need to prevent identical pipelines from getting cached
       * separately, by reusing the same shader snippets.
       */
      *templatep = COGL_PIPELINE (meta_create_texture_pipeline (NULL));

      if ((pipeline_flags & PIPELINE_VIGNETTE) != 0)
        {
          static CoglSnippet *vignette_vertex_snippet;
          static CoglSnippet *vignette_fragment_snippet;

          if (!vignette_vertex_snippet)
            vignette_vertex_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
                                                        VIGNETTE_VERTEX_SHADER_DECLARATIONS,
                                                        VIGNETTE_VERTEX_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, vignette_vertex_snippet);

          if (!vignette_fragment_snippet)
            vignette_fragment_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                                          VIGNETTE_FRAGMENT_SHADER_DECLARATIONS,
                                                          VIGNETTE_FRAGMENT_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, vignette_fragment_snippet);
        }

      if ((pipeline_flags & PIPELINE_GRADIENT) != 0)
        {
          static CoglSnippet *gradient_vertex_snippet;
          static CoglSnippet *gradient_fragment_snippet;

          if (!gradient_vertex_snippet)
            gradient_vertex_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
                                                        GRADIENT_VERTEX_SHADER_DECLARATIONS,
                                                        GRADIENT_VERTEX_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, gradient_vertex_snippet);

          if (!gradient_fragment_snippet)
            gradient_fragment_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                                          GRADIENT_FRAGMENT_SHADER_DECLARATIONS,
                                                          GRADIENT_FRAGMENT_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, gradient_fragment_snippet);
        }

      if ((pipeline_flags & PIPELINE_ROUNDED_CLIP) != 0)
        {
          static CoglSnippet *rounded_clip_fragment_snippet;

          if (!rounded_clip_fragment_snippet)
            {
              rounded_clip_fragment_snippet =
                cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                  ROUNDED_CLIP_FRAGMENT_SHADER_DECLARATIONS,
                                  ROUNDED_CLIP_FRAGMENT_SHADER_CODE);
            }

          cogl_pipeline_add_snippet (*templatep, rounded_clip_fragment_snippet);
        }


      if ((pipeline_flags & PIPELINE_BLEND) == 0)
        cogl_pipeline_set_blend (*templatep, "RGBA = ADD (SRC_COLOR, 0)", NULL);
    }

  return cogl_pipeline_copy (*templatep);
}

static void
setup_pipeline (MetaBackgroundContent *self,
                ClutterActor          *actor,
                ClutterPaintContext   *paint_context,
                MtkRectangle          *actor_pixel_rect)
{
  MetaContext *context = meta_display_get_context (self->display);
  MetaBackend *backend = meta_context_get_backend (context);
  PipelineFlags pipeline_flags = 0;
  guint8 opacity;
  float color_component;
  CoglFramebuffer *fb;
  CoglPipelineFilter min_filter, mag_filter;

  opacity = clutter_actor_get_paint_opacity (actor);
  if (opacity < 255)
    pipeline_flags |= PIPELINE_BLEND;
  if (self->vignette)
    pipeline_flags |= PIPELINE_VIGNETTE;
  if (self->gradient)
    pipeline_flags |= PIPELINE_GRADIENT;
  if (self->has_rounded_clip)
    pipeline_flags |= PIPELINE_ROUNDED_CLIP | PIPELINE_BLEND;

  if (pipeline_flags != self->pipeline_flags)
    g_clear_object (&self->pipeline);

  if (self->pipeline == NULL)
    {
      self->pipeline_flags = pipeline_flags;
      self->pipeline = make_pipeline (pipeline_flags);
      self->changed = CHANGED_ALL;
    }

  if (self->changed & CHANGED_BACKGROUND)
    {
      CoglPipelineWrapMode wrap_mode;
      CoglTexture *texture = meta_background_get_texture (self->background,
                                                          self->monitor,
                                                          &self->texture_area,
                                                          &wrap_mode);

      if (texture)
        {
          self->texture_width = cogl_texture_get_width (texture);
          self->texture_height = cogl_texture_get_height (texture);
        }
      else
        {
          self->texture_width = 0;
          self->texture_height = 0;
        }

      cogl_pipeline_set_layer_texture (self->pipeline, 0, texture);
      cogl_pipeline_set_layer_wrap_mode (self->pipeline, 0, wrap_mode);

      self->changed &= ~CHANGED_BACKGROUND;
    }

  if (self->changed & CHANGED_VIGNETTE_PARAMETERS)
    {
      cogl_pipeline_set_uniform_1f (self->pipeline,
                                    cogl_pipeline_get_uniform_location (self->pipeline,
                                                                        "vignette_sharpness"),
                                    self->vignette_sharpness);

      self->changed &= ~CHANGED_VIGNETTE_PARAMETERS;
    }

  if (self->changed & CHANGED_GRADIENT_PARAMETERS)
    {
      MtkRectangle monitor_geometry;
      float gradient_height_perc;

      meta_display_get_monitor_geometry (self->display,
                                         self->monitor, &monitor_geometry);
      gradient_height_perc = MAX (0.0001, self->gradient_height / (float)monitor_geometry.height);
      cogl_pipeline_set_uniform_1f (self->pipeline,
                                    cogl_pipeline_get_uniform_location (self->pipeline,
                                                                        "gradient_height_perc"),
                                    gradient_height_perc);
      cogl_pipeline_set_uniform_1f (self->pipeline,
                                    cogl_pipeline_get_uniform_location (self->pipeline,
                                                                        "gradient_max_darkness"),
                                    self->gradient_max_darkness);

      self->changed &= ~CHANGED_GRADIENT_PARAMETERS;
    }

  if (self->changed & CHANGED_ROUNDED_CLIP_PARAMETERS)
    {
      float monitor_scale;
      float bounds_x1, bounds_x2, bounds_y1, bounds_y2;
      float clip_radius;

      monitor_scale = meta_backend_is_stage_views_scaled (backend)
        ? meta_display_get_monitor_scale (self->display, self->monitor)
        : 1.0;

      if (self->rounded_clip_bounds_set)
        {
          bounds_x1 = self->rounded_clip_bounds.origin.x;
          bounds_x2 = self->rounded_clip_bounds.origin.x + self->rounded_clip_bounds.size.width;
          bounds_y1 = self->rounded_clip_bounds.origin.y;
          bounds_y2 = self->rounded_clip_bounds.origin.y + self->rounded_clip_bounds.size.height;

          bounds_x1 *= monitor_scale;
          bounds_x2 *= monitor_scale;
          bounds_y1 *= monitor_scale;
          bounds_y2 *= monitor_scale;
        }
      else
        {
          bounds_x1 = 0.0;
          bounds_x2 = self->texture_width;
          bounds_y1 = 0.0;
          bounds_y2 = self->texture_height;
        }

      clip_radius = self->rounded_clip_radius * monitor_scale;

      float bounds[] = {
        bounds_x1,
        bounds_y1,
        bounds_x2,
        bounds_y2,
      };

      int bounds_uniform_location;
      int clip_radius_uniform_location;

      bounds_uniform_location =
        cogl_pipeline_get_uniform_location (self->pipeline, "bounds");
      clip_radius_uniform_location =
        cogl_pipeline_get_uniform_location (self->pipeline, "clip_radius");

      cogl_pipeline_set_uniform_float (self->pipeline,
                                       bounds_uniform_location,
                                       4, 1,
                                       bounds);

      cogl_pipeline_set_uniform_1f (self->pipeline,
                                    clip_radius_uniform_location,
                                    clip_radius);

      self->changed &= ~CHANGED_ROUNDED_CLIP_PARAMETERS;
    }

  if (self->vignette)
    color_component = self->vignette_brightness * opacity / 255.;
  else
    color_component = opacity / 255.;

  cogl_pipeline_set_color4f (self->pipeline,
                             color_component,
                             color_component,
                             color_component,
                             opacity / 255.);

  fb = clutter_paint_context_get_framebuffer (paint_context);
  if (meta_actor_painting_untransformed (fb,
                                         actor_pixel_rect->width,
                                         actor_pixel_rect->height,
                                         self->texture_width,
                                         self->texture_height,
                                         NULL))
    {
      min_filter = COGL_PIPELINE_FILTER_NEAREST;
      mag_filter = COGL_PIPELINE_FILTER_NEAREST;
    }
  else
    {
      min_filter = COGL_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST;
      mag_filter = COGL_PIPELINE_FILTER_LINEAR;
    }

  cogl_pipeline_set_layer_filters (self->pipeline, 0, min_filter, mag_filter);
}

static void
set_glsl_parameters (MetaBackgroundContent *self,
                     MtkRectangle          *actor_pixel_rect)
{
  MetaContext *context = meta_display_get_context (self->display);
  MetaBackend *backend = meta_context_get_backend (context);
  float monitor_scale;
  float scale[2];
  float offset[2];
  int pixel_step_uniform_location;

  monitor_scale = meta_backend_is_stage_views_scaled (backend)
    ? meta_display_get_monitor_scale (self->display, self->monitor)
    : 1.0;

  float pixel_step[] = {
    1.f / (self->texture_area.width * monitor_scale),
    1.f / (self->texture_area.height * monitor_scale),
  };

  pixel_step_uniform_location =
    cogl_pipeline_get_uniform_location (self->pipeline,
                                        "pixel_step");

  /* Compute a scale and offset for transforming texture coordinates to the
   * coordinate system from [-0.5 to 0.5] across the area of the actor
   */
  scale[0] = self->texture_area.width / (float)actor_pixel_rect->width;
  scale[1] = self->texture_area.height / (float)actor_pixel_rect->height;
  offset[0] = self->texture_area.x / (float)actor_pixel_rect->width - 0.5;
  offset[1] = self->texture_area.y / (float)actor_pixel_rect->height - 0.5;

  cogl_pipeline_set_uniform_float (self->pipeline,
                                   cogl_pipeline_get_uniform_location (self->pipeline,
                                                                       "scale"),
                                   2, 1, scale);

  cogl_pipeline_set_uniform_float (self->pipeline,
                                   cogl_pipeline_get_uniform_location (self->pipeline,
                                                                       "offset"),
                                   2, 1, offset);

  cogl_pipeline_set_uniform_float (self->pipeline,
                                   pixel_step_uniform_location,
                                   2, 1,
                                   pixel_step);
}

static void
paint_clipped_rectangle (MetaBackgroundContent *self,
                         ClutterPaintNode      *node,
                         ClutterActorBox       *actor_box,
                         MtkRectangle          *rect)
{
  g_autoptr (ClutterPaintNode) pipeline_node = NULL;
  float h_scale, v_scale;
  float x1, y1, x2, y2;
  float tx1, ty1, tx2, ty2;

  h_scale = self->texture_area.width / clutter_actor_box_get_width (actor_box);
  v_scale = self->texture_area.height / clutter_actor_box_get_height (actor_box);

  x1 = rect->x;
  y1 = rect->y;
  x2 = rect->x + rect->width;
  y2 = rect->y + rect->height;

  tx1 = (x1 * h_scale - self->texture_area.x) / (float)self->texture_area.width;
  ty1 = (y1 * v_scale - self->texture_area.y) / (float)self->texture_area.height;
  tx2 = (x2 * h_scale - self->texture_area.x) / (float)self->texture_area.width;
  ty2 = (y2 * v_scale - self->texture_area.y) / (float)self->texture_area.height;

  pipeline_node = clutter_pipeline_node_new (self->pipeline);
  clutter_paint_node_set_name (pipeline_node, "MetaBackgroundContent (Slice)");
  clutter_paint_node_add_texture_rectangle (pipeline_node,
                                            &(ClutterActorBox) {
                                              .x1 = x1,
                                              .y1 = y1,
                                              .x2 = x2,
                                              .y2 = y2,
                                            },
                                            tx1, ty1,
                                            tx2, ty2);

  clutter_paint_node_add_child (node, pipeline_node);
}

static void
meta_background_content_paint_content (ClutterContent      *content,
                                       ClutterActor        *actor,
                                       ClutterPaintNode    *node,
                                       ClutterPaintContext *paint_context)
{
  MetaBackgroundContent *self = META_BACKGROUND_CONTENT (content);
  ClutterActorBox actor_box;
  MtkRectangle rect_within_actor;
  MtkRectangle rect_within_stage;
  g_autoptr (MtkRegion) region = NULL;
  int i, n_rects;
  float transformed_x, transformed_y, transformed_width, transformed_height;
  gboolean untransformed;

  if ((self->clip_region && mtk_region_is_empty (self->clip_region)))
    return;

  clutter_actor_get_content_box (actor, &actor_box);
  rect_within_actor.x = actor_box.x1;
  rect_within_actor.y = actor_box.y1;
  rect_within_actor.width = actor_box.x2 - actor_box.x1;
  rect_within_actor.height = actor_box.y2 - actor_box.y1;

  if (clutter_actor_is_in_clone_paint (actor))
    {
      untransformed = FALSE;
    }
  else
    {
      clutter_actor_get_transformed_position (actor,
                                              &transformed_x,
                                              &transformed_y);
      rect_within_stage.x = floorf (transformed_x);
      rect_within_stage.y = floorf (transformed_y);

      clutter_actor_get_transformed_size (actor,
                                          &transformed_width,
                                          &transformed_height);
      rect_within_stage.width = ceilf (transformed_width);
      rect_within_stage.height = ceilf (transformed_height);

      untransformed =
        rect_within_actor.x == rect_within_stage.x &&
        rect_within_actor.y == rect_within_stage.y &&
        rect_within_actor.width == rect_within_stage.width &&
        rect_within_actor.height == rect_within_stage.height;
    }

  if (untransformed) /* actor and stage space are the same */
    {
      if (self->clip_region)
        {
          region = mtk_region_copy (self->clip_region);
          mtk_region_intersect_rectangle (region, &rect_within_stage);
        }
      else
        {
          const MtkRegion *redraw_clip;

          redraw_clip = clutter_paint_context_get_redraw_clip (paint_context);
          if (redraw_clip)
            {
              region = mtk_region_copy (redraw_clip);
              mtk_region_intersect_rectangle (region, &rect_within_stage);
            }
          else
            {
              region = mtk_region_create_rectangle (&rect_within_stage);
            }
        }
    }
  else /* actor and stage space are different but we need actor space */
    {
      if (self->clip_region)
        {
          region = mtk_region_copy (self->clip_region);
          mtk_region_intersect_rectangle (region, &rect_within_actor);
        }
      else
        {
          region = mtk_region_create_rectangle (&rect_within_actor);
        }
    }

  if (self->unobscured_region)
    mtk_region_intersect (region, self->unobscured_region);

  /* region is now in actor space */
  if (mtk_region_is_empty (region))
    return;

  setup_pipeline (self, actor, paint_context, &rect_within_actor);
  set_glsl_parameters (self, &rect_within_actor);

  /* Limit to how many separate rectangles we'll draw; beyond this just
   * fall back and draw the whole thing */
#define MAX_RECTS 64

  n_rects = mtk_region_num_rectangles (region);
  if (n_rects <= MAX_RECTS)
    {
      for (i = 0; i < n_rects; i++)
        {
          MtkRectangle rect;
          rect = mtk_region_get_rectangle (region, i);
          paint_clipped_rectangle (self, node, &actor_box, &rect);
        }
    }
  else
    {
      MtkRectangle rect;
      rect = mtk_region_get_extents (region);
      paint_clipped_rectangle (self, node, &actor_box, &rect);
    }
}

static gboolean
meta_background_content_get_preferred_size (ClutterContent *content,
                                            float          *width,
                                            float          *height)

{
  MetaBackgroundContent *background_content = META_BACKGROUND_CONTENT (content);
  MtkRectangle monitor_geometry;

  meta_display_get_monitor_geometry (background_content->display,
                                     background_content->monitor,
                                     &monitor_geometry);

  if (width)
    *width = monitor_geometry.width;

  if (height)
    *height = monitor_geometry.height;

  return TRUE;
}

static void
clutter_content_iface_init (ClutterContentInterface *iface)
{
  iface->paint_content = meta_background_content_paint_content;
  iface->get_preferred_size = meta_background_content_get_preferred_size;
}

static void
set_monitor (MetaBackgroundContent *self,
             int                    monitor)
{
  MtkRectangle old_monitor_geometry;
  MtkRectangle new_monitor_geometry;
  MetaDisplay *display = self->display;

  if(self->monitor == monitor)
      return;

  meta_display_get_monitor_geometry (display, self->monitor, &old_monitor_geometry);
  meta_display_get_monitor_geometry (display, monitor, &new_monitor_geometry);
  if(old_monitor_geometry.height != new_monitor_geometry.height)
      invalidate_pipeline (self, CHANGED_GRADIENT_PARAMETERS);

  self->monitor = monitor;
}

static void
meta_background_content_dispose (GObject *object)
{
  MetaBackgroundContent *self = META_BACKGROUND_CONTENT (object);

  set_clip_region (self, NULL);
  set_unobscured_region (self, NULL);
  meta_background_content_set_background (self, NULL);

  g_clear_object (&self->pipeline);

  G_OBJECT_CLASS (meta_background_content_parent_class)->dispose (object);
}

static void
meta_background_content_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  MetaBackgroundContent *self = META_BACKGROUND_CONTENT (object);

  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      self->display = g_value_get_object (value);
      break;
    case PROP_MONITOR:
      set_monitor (self, g_value_get_int (value));
      break;
    case PROP_BACKGROUND:
      meta_background_content_set_background (self, g_value_get_object (value));
      break;
    case PROP_GRADIENT:
      meta_background_content_set_gradient (self,
                                            g_value_get_boolean (value),
                                            self->gradient_height,
                                            self->gradient_max_darkness);
      break;
    case PROP_GRADIENT_HEIGHT:
      meta_background_content_set_gradient (self,
                                            self->gradient,
                                            g_value_get_int (value),
                                            self->gradient_max_darkness);
      break;
    case PROP_GRADIENT_MAX_DARKNESS:
      meta_background_content_set_gradient (self,
                                            self->gradient,
                                            self->gradient_height,
                                            g_value_get_double (value));
      break;
    case PROP_VIGNETTE:
      meta_background_content_set_vignette (self,
                                            g_value_get_boolean (value),
                                            self->vignette_brightness,
                                            self->vignette_sharpness);
      break;
    case PROP_VIGNETTE_SHARPNESS:
      meta_background_content_set_vignette (self,
                                            self->vignette,
                                            self->vignette_brightness,
                                            g_value_get_double (value));
      break;
    case PROP_VIGNETTE_BRIGHTNESS:
      meta_background_content_set_vignette (self,
                                            self->vignette,
                                            g_value_get_double (value),
                                            self->vignette_sharpness);
      break;
    case PROP_ROUNDED_CLIP_RADIUS:
      meta_background_content_set_rounded_clip_radius (self,
                                                       g_value_get_float (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_content_get_property (GObject      *object,
                                    guint         prop_id,
                                    GValue       *value,
                                    GParamSpec   *pspec)
{
  MetaBackgroundContent *self = META_BACKGROUND_CONTENT (object);

  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      g_value_set_object (value, self->display);
      break;
    case PROP_MONITOR:
      g_value_set_int (value, self->monitor);
      break;
    case PROP_BACKGROUND:
      g_value_set_object (value, self->background);
      break;
    case PROP_GRADIENT:
      g_value_set_boolean (value, self->gradient);
      break;
    case PROP_GRADIENT_HEIGHT:
      g_value_set_int (value, self->gradient_height);
      break;
    case PROP_GRADIENT_MAX_DARKNESS:
      g_value_set_double (value, self->gradient_max_darkness);
      break;
    case PROP_VIGNETTE:
      g_value_set_boolean (value, self->vignette);
      break;
    case PROP_VIGNETTE_BRIGHTNESS:
      g_value_set_double (value, self->vignette_brightness);
      break;
    case PROP_VIGNETTE_SHARPNESS:
      g_value_set_double (value, self->vignette_sharpness);
      break;
    case PROP_ROUNDED_CLIP_RADIUS:
      g_value_set_float (value, self->rounded_clip_radius);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_content_class_init (MetaBackgroundContentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_background_content_dispose;
  object_class->set_property = meta_background_content_set_property;
  object_class->get_property = meta_background_content_get_property;

  properties[PROP_META_DISPLAY] =
    g_param_spec_object ("meta-display", NULL, NULL,
                         META_TYPE_DISPLAY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  properties[PROP_MONITOR] =
    g_param_spec_int ("monitor", NULL, NULL,
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  properties[PROP_BACKGROUND] =
    g_param_spec_object ("background", NULL, NULL,
                         META_TYPE_BACKGROUND,
                         G_PARAM_READWRITE);

  properties[PROP_GRADIENT] =
    g_param_spec_boolean ("gradient", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE);

  properties[PROP_GRADIENT_HEIGHT] =
    g_param_spec_int ("gradient-height", NULL, NULL,
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE);

  properties[PROP_GRADIENT_MAX_DARKNESS] =
    g_param_spec_double ("gradient-max-darkness", NULL, NULL,
                         0.0, 1.0, 0.0,
                         G_PARAM_READWRITE);

  properties[PROP_VIGNETTE] =
    g_param_spec_boolean ("vignette", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE);

  properties[PROP_VIGNETTE_BRIGHTNESS] =
    g_param_spec_double ("brightness", NULL, NULL,
                         0.0, 1.0, 1.0,
                         G_PARAM_READWRITE);

  properties[PROP_VIGNETTE_SHARPNESS] =
    g_param_spec_double ("vignette-sharpness", NULL, NULL,
                         0.0, G_MAXDOUBLE, 0.0,
                         G_PARAM_READWRITE);

  properties[PROP_ROUNDED_CLIP_RADIUS] =
    g_param_spec_float ("rounded-clip-radius", NULL, NULL,
                        0.0, G_MAXFLOAT, 0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
meta_background_content_init (MetaBackgroundContent *self)
{
  self->gradient = FALSE;
  self->gradient_height = 0;
  self->gradient_max_darkness = 0.0;

  self->vignette = FALSE;
  self->vignette_brightness = 1.0;
  self->vignette_sharpness = 0.0;

  self->has_rounded_clip = FALSE;
  self->rounded_clip_radius = 0.0;
}

/**
 * meta_background_content_new:
 * @monitor: Index of the monitor for which to draw the background
 *
 * Creates a new actor to draw the background for the given monitor.
 *
 * Return value: (transfer full): the newly created background actor
 */
ClutterContent *
meta_background_content_new (MetaDisplay *display,
                             int          monitor)
{
  return g_object_new (META_TYPE_BACKGROUND_CONTENT,
                       "meta-display", display,
                       "monitor", monitor,
                       NULL);
}

void
meta_background_content_set_background (MetaBackgroundContent *self,
                                        MetaBackground        *background)
{
  g_return_if_fail (META_IS_BACKGROUND_CONTENT (self));
  g_return_if_fail (background == NULL || META_IS_BACKGROUND (background));

  if (background == self->background)
    return;

  if (self->background)
    {
      g_signal_handlers_disconnect_by_func (self->background,
                                            (gpointer)on_background_changed,
                                            self);
      g_clear_object (&self->background);
    }

  if (background)
    {
      self->background = g_object_ref (background);
      g_signal_connect (self->background, "changed",
                        G_CALLBACK (on_background_changed), self);
    }

  invalidate_pipeline (self, CHANGED_BACKGROUND);
  clutter_content_invalidate (CLUTTER_CONTENT (self));
}

void
meta_background_content_set_gradient (MetaBackgroundContent *self,
                                      gboolean               enabled,
                                      int                    height,
                                      double                 max_darkness)
{
  gboolean changed = FALSE;

  g_return_if_fail (META_IS_BACKGROUND_CONTENT (self));
  g_return_if_fail (height >= 0);
  g_return_if_fail (max_darkness >= 0. && max_darkness <= 1.);

  enabled = enabled != FALSE && height != 0;

  if (enabled != self->gradient)
    {
      self->gradient = enabled;
      invalidate_pipeline (self, CHANGED_EFFECTS);
      changed = TRUE;
    }

  if (height != self->gradient_height || max_darkness != self->gradient_max_darkness)
    {
      self->gradient_height = height;
      self->gradient_max_darkness = max_darkness;
      invalidate_pipeline (self, CHANGED_GRADIENT_PARAMETERS);
      changed = TRUE;
    }

  if (changed)
    clutter_content_invalidate (CLUTTER_CONTENT (self));
}

void
meta_background_content_set_vignette (MetaBackgroundContent *self,
                                      gboolean               enabled,
                                      double                 brightness,
                                      double                 sharpness)
{
  gboolean changed = FALSE;

  g_return_if_fail (META_IS_BACKGROUND_CONTENT (self));
  g_return_if_fail (brightness >= 0. && brightness <= 1.);
  g_return_if_fail (sharpness >= 0.);

  enabled = enabled != FALSE;

  if (enabled != self->vignette)
    {
      self->vignette = enabled;
      invalidate_pipeline (self, CHANGED_EFFECTS);
      changed = TRUE;
    }

  if (brightness != self->vignette_brightness || sharpness != self->vignette_sharpness)
    {
      self->vignette_brightness = brightness;
      self->vignette_sharpness = sharpness;
      invalidate_pipeline (self, CHANGED_VIGNETTE_PARAMETERS);
      changed = TRUE;
    }

  if (changed)
    clutter_content_invalidate (CLUTTER_CONTENT (self));
}

void
meta_background_content_set_rounded_clip_radius (MetaBackgroundContent *self,
                                                 float                  radius)
{
  gboolean enabled;
  gboolean changed = FALSE;

  g_return_if_fail (META_IS_BACKGROUND_CONTENT (self));
  g_return_if_fail (radius >= 0.0);

  enabled = radius > 0.0;

  if (enabled != self->has_rounded_clip)
    {
      self->has_rounded_clip = enabled;
      invalidate_pipeline (self, CHANGED_EFFECTS);
      changed = TRUE;
    }

  if (!G_APPROX_VALUE (radius, self->rounded_clip_radius, FLT_EPSILON))
    {
      self->rounded_clip_radius = radius;
      invalidate_pipeline (self, CHANGED_ROUNDED_CLIP_PARAMETERS);
      changed = TRUE;
    }

  if (changed)
    {
      clutter_content_invalidate (CLUTTER_CONTENT (self));
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROUNDED_CLIP_RADIUS]);
    }
}

/**
 * meta_background_content_set_rounded_clip_bounds:
 * @self: The #MetaBackgroundContent
 * @bounds: (allow-none): The new bounding clip rectangle, or %NULL
 *
 * Sets the bounding clip rectangle of the #MetaBackgroundContent that's used
 * when a rounded clip set via meta_background_content_set_rounded_clip_radius()
 * is in effect, set it to  %NULL to use no bounding clip, rounding the edges
 * of the full texture.
 */
void
meta_background_content_set_rounded_clip_bounds (MetaBackgroundContent *self,
                                                 const graphene_rect_t *bounds)
{
  g_return_if_fail (META_IS_BACKGROUND_CONTENT (self));

  if (bounds == NULL)
    {
      if (!self->rounded_clip_bounds_set)
        return;

      self->rounded_clip_bounds_set = FALSE;
    }
  else
    {
      if (self->rounded_clip_bounds_set &&
          graphene_rect_equal (&self->rounded_clip_bounds, bounds))
        return;

      self->rounded_clip_bounds_set = TRUE;
      graphene_rect_init_from_rect (&self->rounded_clip_bounds, bounds);
    }

  invalidate_pipeline (self, CHANGED_ROUNDED_CLIP_PARAMETERS);
  clutter_content_invalidate (CLUTTER_CONTENT (self));
}

MtkRegion *
meta_background_content_get_clip_region (MetaBackgroundContent *self)
{
  return self->clip_region;
}

void
meta_background_content_cull_unobscured (MetaBackgroundContent *self,
                                         MtkRegion             *unobscured_region)
{
  set_unobscured_region (self, unobscured_region);
}

void
meta_background_content_cull_redraw_clip (MetaBackgroundContent *self,
                                          MtkRegion             *clip_region)
{
  set_clip_region (self, clip_region);
}
