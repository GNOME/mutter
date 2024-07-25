#include <cogl/cogl.h>
#include <string.h>
#include <stdlib.h>

#include "tests/cogl-test-utils.h"

typedef struct _TestState
{
  int fb_width;
  int fb_height;
} TestState;

#define PRIM_COLOR 0xff00ffff
#define TEX_COLOR 0x0000ffff

#define N_ATTRIBS 8

typedef CoglPrimitive * (* TestPrimFunc) (CoglContext *ctx, uint32_t *expected_color);

static CoglPrimitive *
test_prim_p2 (CoglContext *ctx, uint32_t *expected_color)
{
  static const CoglVertexP2 verts[] =
    { { 0, 0 }, { 0, 10 }, { 10, 0 } };

  return cogl_primitive_new_p2 (test_ctx,
                                COGL_VERTICES_MODE_TRIANGLES,
                                3, /* n_vertices */
                                verts);
}

static CoglPrimitive *
test_prim_p3 (CoglContext *ctx, uint32_t *expected_color)
{
  static const CoglVertexP3 verts[] =
    { { 0, 0, 0 }, { 0, 10, 0 }, { 10, 0, 0 } };

  return cogl_primitive_new_p3 (test_ctx,
                                COGL_VERTICES_MODE_TRIANGLES,
                                3, /* n_vertices */
                                verts);
}

static CoglPrimitive *
test_prim_p2c4 (CoglContext *ctx, uint32_t *expected_color)
{
  static const CoglVertexP2C4 verts[] =
    { { 0, 0, 255, 255, 0, 255 },
      { 0, 10, 255, 255, 0, 255 },
      { 10, 0, 255, 255, 0, 255 } };

  *expected_color = 0xffff00ff;

  return cogl_primitive_new_p2c4 (test_ctx,
                                  COGL_VERTICES_MODE_TRIANGLES,
                                  3, /* n_vertices */
                                  verts);
}

static CoglPrimitive *
test_prim_p2t2 (CoglContext *ctx, uint32_t *expected_color)
{
  static const CoglVertexP2T2 verts[] =
    { { 0, 0, 1, 0 },
      { 0, 10, 1, 0 },
      { 10, 0, 1, 0 } };

  *expected_color = TEX_COLOR;

  return cogl_primitive_new_p2t2 (test_ctx,
                                  COGL_VERTICES_MODE_TRIANGLES,
                                  3, /* n_vertices */
                                  verts);
}

static CoglPrimitive *
test_prim_p3t2 (CoglContext *ctx, uint32_t *expected_color)
{
  static const CoglVertexP3T2 verts[] =
    { { 0, 0, 0, 1, 0 },
      { 0, 10, 0, 1, 0 },
      { 10, 0, 0, 1, 0 } };

  *expected_color = TEX_COLOR;

  return cogl_primitive_new_p3t2 (test_ctx,
                                  COGL_VERTICES_MODE_TRIANGLES,
                                  3, /* n_vertices */
                                  verts);
}

static const TestPrimFunc
test_prim_funcs[] =
  {
    test_prim_p2,
    test_prim_p3,
    test_prim_p2c4,
    test_prim_p2t2,
    test_prim_p3t2,
  };

static void
test_paint (TestState *state)
{
  CoglPipeline *pipeline;
  CoglTexture *tex;
  CoglColor color;
  uint8_t tex_data[6];
  int i;

  /* Create a two pixel texture. The first pixel is white and the
     second pixel is tex_color. The assumption is that if no texture
     coordinates are specified then it will default to 0,0 and get
     white */
  tex_data[0] = 255;
  tex_data[1] = 255;
  tex_data[2] = 255;
  tex_data[3] = (TEX_COLOR >> 24) & 0xff;
  tex_data[4] = (TEX_COLOR >> 16) & 0xff;
  tex_data[5] = (TEX_COLOR >> 8) & 0xff;
  tex = test_utils_texture_new_from_data (test_ctx,
                                          2, 1, /* size */
                                          TEST_UTILS_TEXTURE_NO_ATLAS,
                                          COGL_PIXEL_FORMAT_RGB_888,
                                          6, /* rowstride */
                                          tex_data);
  pipeline = cogl_pipeline_new (test_ctx);
  cogl_color_init_from_4f (&color,
                           ((PRIM_COLOR >> 24) & 0xff) / 255.0,
                           ((PRIM_COLOR >> 16) & 0xff) / 255.0,
                           ((PRIM_COLOR >> 8) & 0xff) / 255.0,
                           ((PRIM_COLOR >> 0) & 0xff) / 255.0);
  cogl_pipeline_set_color (pipeline, &color);
  cogl_pipeline_set_layer_texture (pipeline, 0, tex);
  g_object_unref (tex);

  for (i = 0; i < G_N_ELEMENTS (test_prim_funcs); i++)
    {
      CoglPrimitive *prim;
      uint32_t expected_color = PRIM_COLOR;

      prim = test_prim_funcs[i] (test_ctx, &expected_color);

      cogl_framebuffer_push_matrix (test_fb);
      cogl_framebuffer_translate (test_fb, i * 10, 0, 0);
      cogl_primitive_draw (prim, test_fb, pipeline);
      cogl_framebuffer_pop_matrix (test_fb);

      test_utils_check_pixel (test_fb, i * 10 + 2, 2, expected_color);

      g_object_unref (prim);
    }

  g_object_unref (pipeline);
}

static void
test_primitive (void)
{
  TestState state;

  state.fb_width = cogl_framebuffer_get_width (test_fb);
  state.fb_height = cogl_framebuffer_get_height (test_fb);

  cogl_framebuffer_orthographic (test_fb,
                                 0, 0,
                                 state.fb_width,
                                 state.fb_height,
                                 -1,
                                 100);

  test_paint (&state);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

COGL_TEST_SUITE (
  g_test_add_func ("/primitive", test_primitive);
)
