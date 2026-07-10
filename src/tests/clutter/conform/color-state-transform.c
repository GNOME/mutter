#include <fcntl.h>
#include <glib/gstdio.h>
#include <lcms2.h>
#include <sys/mman.h>

#include "clutter-mutter.h"
#include "clutter/clutter/clutter-color-transform-private.h"
#include "clutter/clutter/clutter-color-pipeline.h"
#include "tests/clutter-test-utils.h"

#define COLOR_TRANSFORM_EPSILON 0.05f
#define COLOR_TRANSFORM_EPSILON_ICC 0.055f

typedef struct _TestColor
{
  float r, g, b, a;
} TestColor;

static TestColor test_colors[] = {
  { 0.0f,   0.0f,  0.0f,  1.0f },
  { 1.0f,   0.0f,  0.0f,  1.0f },
  { 0.0f,   1.0f,  0.0f,  1.0f },
  { 0.0f,   0.0f,  1.0f,  1.0f },
  { 1.0f,   1.0f,  1.0f,  1.0f },
  { 0.22f,  0.33f, 0.44f, 1.0f },
  { 0.88f,  0.66f, 0.5f,  1.0f },
  { 0.0f,   0.66f, 0.44f, 1.0f },
  { 0.166f, 0.0f,  0.93f, 1.0f },
  { 0.99f,  0.75f, 0.0f,  1.0f },
  { 1.0f,   0.5f,  0.25f, 0.5f },
  { 1.0f,   1.0f,  1.0f,  0.9f },
  { 1.0f,   1.0f,  1.0f,  0.5f },
  { 1.0f,   1.0f,  1.0f,  0.3f },
  { 1.0f,   1.0f,  1.0f,  0.1f },
};

#define ACTOR_SIZE 10.0f

static ClutterColorState *
create_icc_color_state (const char *icc_filename)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autofree char *icc_path = NULL;
  g_autofd int srgb_icc_fd = -1;
  struct stat stat = { 0 };
  uint8_t *icc_bytes;
  uint32_t icc_size;
  ClutterColorState *color_state;

  icc_path = g_build_filename (g_getenv ("TEST_DATADIR"),
                               "icc-profiles",
                               icc_filename,
                               NULL);

  g_assert_true (g_file_test (icc_path, G_FILE_TEST_EXISTS));

  srgb_icc_fd = open (icc_path, O_RDONLY);
  g_assert_cmpint (srgb_icc_fd, !=, -1);

  fstat (srgb_icc_fd, &stat);
  icc_size = stat.st_size;
  g_assert_cmpuint (icc_size, >, 0);

  icc_bytes = mmap (NULL, icc_size, PROT_READ, MAP_PRIVATE, srgb_icc_fd, 0);
  g_assert_true (icc_bytes != MAP_FAILED);

  color_state = clutter_color_state_icc_new (context,
                                             icc_bytes,
                                             icc_size,
                                             NULL);
  g_assert_nonnull (color_state);

  munmap (icc_bytes, icc_size);

  return color_state;
}

static cmsInt32Number
a2b_sampler_16bit (const cmsUInt16Number in[],
                   cmsUInt16Number       out[],
                   void                 *cargo)
{
  cmsHTRANSFORM xform = cargo;
  cmsDoTransform (xform, in, out, 1);
  return TRUE;
}

static ClutterColorState *
create_clut_icc_color_state (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) color_state = NULL;
  cmsHPROFILE srgb_profile;
  cmsHPROFILE xyz_profile;
  cmsHPROFILE clut_profile;
  cmsHTRANSFORM to_pcs, from_pcs;
  cmsPipeline *a2b, *b2a;
  cmsStage *clut_stage;
  int grid_points = 17;
  cmsUInt32Number profile_size = 0;
  g_autofree void *profile_data = NULL;

  srgb_profile = cmsCreate_sRGBProfile ();
  xyz_profile = cmsCreateXYZProfile ();

  to_pcs = cmsCreateTransform (srgb_profile, TYPE_RGB_16,
                               xyz_profile, TYPE_XYZ_16,
                               INTENT_RELATIVE_COLORIMETRIC, 0);
  g_assert_nonnull (to_pcs);
  from_pcs = cmsCreateTransform (xyz_profile, TYPE_XYZ_16,
                                 srgb_profile, TYPE_RGB_16,
                                 INTENT_RELATIVE_COLORIMETRIC, 0);
  g_assert_nonnull (from_pcs);

  clut_profile = cmsCreateProfilePlaceholder (NULL);
  cmsSetProfileVersion (clut_profile, 2.4);
  cmsSetDeviceClass (clut_profile, cmsSigDisplayClass);
  cmsSetColorSpace (clut_profile, cmsSigRgbData);
  cmsSetPCS (clut_profile, cmsSigXYZData);

  cmsWriteTag (clut_profile, cmsSigMediaWhitePointTag,
               cmsReadTag (srgb_profile, cmsSigMediaWhitePointTag));

  /* A2B0: device RGB → PCS XYZ */
  a2b = cmsPipelineAlloc (NULL, 3, 3);
  clut_stage = cmsStageAllocCLut16bit (NULL, grid_points, 3, 3, NULL);
  cmsStageSampleCLut16bit (clut_stage, a2b_sampler_16bit, to_pcs, 0);
  cmsPipelineInsertStage (a2b, cmsAT_END, clut_stage);
  cmsWriteTag (clut_profile, cmsSigAToB0Tag, a2b);
  cmsPipelineFree (a2b);

  /* B2A0: PCS XYZ → device RGB */
  b2a = cmsPipelineAlloc (NULL, 3, 3);
  clut_stage = cmsStageAllocCLut16bit (NULL, grid_points, 3, 3, NULL);
  cmsStageSampleCLut16bit (clut_stage, a2b_sampler_16bit, from_pcs, 0);
  cmsPipelineInsertStage (b2a, cmsAT_END, clut_stage);
  cmsWriteTag (clut_profile, cmsSigBToA0Tag, b2a);
  cmsPipelineFree (b2a);

  cmsDeleteTransform (to_pcs);
  cmsDeleteTransform (from_pcs);
  cmsCloseProfile (srgb_profile);
  cmsCloseProfile (xyz_profile);

  g_assert_true (cmsSaveProfileToMem (clut_profile, NULL, &profile_size));
  g_assert_cmpuint (profile_size, >, 0);
  profile_data = g_malloc (profile_size);
  g_assert_true (cmsSaveProfileToMem (clut_profile, profile_data, &profile_size));
  cmsCloseProfile (clut_profile);

  {
    g_autoptr (GError) error = NULL;
    color_state = clutter_color_state_icc_new (context,
                                               profile_data,
                                               profile_size,
                                               &error);
    g_assert_no_error (error);
    g_assert_nonnull (color_state);
  }

  return g_steal_pointer (&color_state);
}

static GList *
create_actors (ClutterActor *stage)
{
  GList *actors = NULL;

  const CoglColor black = { 0, 0, 0, UINT8_MAX };
  ClutterActor *background = clutter_actor_new ();
  clutter_actor_set_background_color (background, &black);
  clutter_actor_set_size (background,
                          G_N_ELEMENTS (test_colors) * ACTOR_SIZE,
                          ACTOR_SIZE);
  clutter_actor_set_position (background, 0.0f, 0.0f);
  clutter_actor_add_child (stage, background);

  actors = g_list_prepend (actors, background);

  for (int i = 0; i < G_N_ELEMENTS (test_colors); i++)
    {
      const CoglColor color = {
        (uint8_t) (test_colors[i].r * UINT8_MAX),
        (uint8_t) (test_colors[i].g * UINT8_MAX),
        (uint8_t) (test_colors[i].b * UINT8_MAX),
        (uint8_t) (test_colors[i].a * UINT8_MAX),
      };

      ClutterActor *actor = clutter_actor_new ();
      clutter_actor_set_background_color (actor, &color);
      clutter_actor_set_size (actor, ACTOR_SIZE, ACTOR_SIZE);
      clutter_actor_set_position (actor, i * ACTOR_SIZE, 0.0f);
      clutter_actor_add_child (background, actor);

      actors = g_list_prepend (actors, actor);
    }

  return actors;
}

static void
actors_set_color_state (GList             *actors,
                        ClutterColorState *color_state)
{
  GList *l;

  for (l = actors; l; l = l->next)
    {
      ClutterActor *actor = l->data;
      clutter_actor_set_color_state (actor, color_state);
    }
}

static void
stage_view_set_color_state (ClutterStageView  *stage_view,
                            ClutterColorState *color_state)
{
  g_autoptr (ClutterColorState) view_color_state = NULL;

  view_color_state = clutter_color_state_get_blending (color_state, FALSE);

  clutter_stage_view_set_color_state (stage_view, view_color_state);
  clutter_stage_view_set_output_color_state (stage_view, color_state);
}

static ClutterStageView *
get_stage_view (ClutterActor *stage)
{
  GList *l;

  l = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  return l->data;
}

static void
view_painted_cb (ClutterStage     *stage,
                 ClutterStageView *view,
                 MtkRegion        *redraw_clip,
                 ClutterFrame     *frame,
                 gpointer          data)
{
  gboolean *was_painted = data;

  *was_painted = TRUE;
}

static void
wait_for_paint (ClutterActor *stage)
{
  gboolean was_painted = FALSE;
  int handler_id;

  clutter_actor_show (stage);

  handler_id = g_signal_connect_after (stage, "paint-view",
                                       G_CALLBACK (view_painted_cb),
                                       &was_painted);

  while (!was_painted)
    g_main_context_iteration (NULL, FALSE);

  g_signal_handler_disconnect (stage, handler_id);
}

static gboolean
validate_one_transform (CoglFramebuffer *fb,
                        int              x,
                        float           *cpu_color,
                        TestColor       *test_color,
                        const char      *name)
{
  float shader_color[4];

  cogl_framebuffer_read_pixels (fb,
                                x, 0, 1, 1,
                                COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE,
                                (uint8_t *) shader_color);

  if (!G_APPROX_VALUE (cpu_color[0],
                       shader_color[0],
                       COLOR_TRANSFORM_EPSILON) ||
      !G_APPROX_VALUE (cpu_color[1],
                       shader_color[1],
                       COLOR_TRANSFORM_EPSILON) ||
      !G_APPROX_VALUE (cpu_color[2],
                       shader_color[2],
                       COLOR_TRANSFORM_EPSILON))
    {
      g_test_message ("Failed %s color transform:\n"
                      "input  (%.5f, %.5f, %.5f, %.5f)\n"
                      "cpu    (%.5f, %.5f, %.5f)\n"
                      "shader (%.5f, %.5f, %.5f)\n"
                      "diff   (%.5f, %.5f, %.5f)\n",
                      name,
                      test_color->r, test_color->g, test_color->b,
                      test_color->a,
                      cpu_color[0], cpu_color[1], cpu_color[2],
                      shader_color[0], shader_color[1], shader_color[2],
                      ABS (cpu_color[0] - shader_color[0]),
                      ABS (cpu_color[1] - shader_color[1]),
                      ABS (cpu_color[2] - shader_color[2]));

      return FALSE;
    }

  return TRUE;
}

static void
validate_transform (ClutterActor      *stage,
                    ClutterColorState *src_color_state,
                    ClutterColorState *blend_color_state,
                    ClutterColorState *output_color_state)
{
  ClutterContext *context = clutter_color_state_get_context (src_color_state);
  ClutterStageView *view = get_stage_view (stage);
  CoglFramebuffer *output_fb = clutter_stage_view_get_onscreen (view);
  CoglFramebuffer *blend_fb = clutter_stage_view_get_framebuffer (view);
  ClutterColorTransform *src_to_blend_transform = NULL;
  ClutterColorTransform *blend_to_output_transform = NULL;
  ClutterColorTransform *src_to_output_transform;
  ClutterColorPipeline *pipeline;
  float cpu_color[4];
  gboolean transform_passed;

  if (blend_color_state)
    {
      src_to_blend_transform =
        clutter_color_transform_from_color_states (context, src_color_state,
                                                   blend_color_state, 0);
      blend_to_output_transform =
        clutter_color_transform_from_color_states (context, blend_color_state,
                                                   output_color_state, 0);
    }

  src_to_output_transform =
    clutter_color_transform_from_color_states (context, src_color_state,
                                               output_color_state, 0);

  for (int i = 0; i < G_N_ELEMENTS (test_colors); i++)
    {
      if (blend_color_state)
        {
          /* Start unpremultiplied */
          cpu_color[0] = test_colors[i].r;
          cpu_color[1] = test_colors[i].g;
          cpu_color[2] = test_colors[i].b;
          cpu_color[3] = 1.0f;

          pipeline =
            clutter_color_transform_get_pipeline (src_to_blend_transform);
          clutter_color_pipeline_do_transform (pipeline, cpu_color, 1);

          /* Premultiply */
          cpu_color[0] *= test_colors[i].a;
          cpu_color[1] *= test_colors[i].a;
          cpu_color[2] *= test_colors[i].a;

          transform_passed = validate_one_transform (blend_fb,
                                                     (int) (i * ACTOR_SIZE),
                                                     cpu_color,
                                                     test_colors + i,
                                                     "source -> blend");
          g_assert_true (transform_passed);

          pipeline =
            clutter_color_transform_get_pipeline (blend_to_output_transform);
          clutter_color_pipeline_do_transform (pipeline, cpu_color, 1);

          transform_passed = validate_one_transform (output_fb,
                                                     (int) (i * ACTOR_SIZE),
                                                     cpu_color,
                                                     test_colors + i,
                                                     "blend -> output");
          g_assert_true (transform_passed);
        }

      if (test_colors[i].a == 1.0f)
        {
          cpu_color[0] = test_colors[i].r;
          cpu_color[1] = test_colors[i].g;
          cpu_color[2] = test_colors[i].b;
          cpu_color[3] = 1.0f;

          pipeline =
            clutter_color_transform_get_pipeline (src_to_output_transform);
          clutter_color_pipeline_do_transform (pipeline, cpu_color, 1);

          transform_passed = validate_one_transform (output_fb,
                                                     (int) (i * ACTOR_SIZE),
                                                     cpu_color,
                                                     test_colors + i,
                                                     "source -> output");
          g_assert_true (transform_passed);
        }
    }
}

static void
color_state_transform_icc_to_params (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
  ClutterColorState *blend_color_state = NULL;
  g_autoptr (ClutterColorState) target_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state = create_icc_color_state ("sRGB.icc");
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  target_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_BT2020,
                                    CLUTTER_TRANSFER_FUNCTION_PQ);
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, target_color_state);
  blend_color_state =
    clutter_stage_view_get_color_state (stage_view);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, blend_color_state,
                      target_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

static void
color_state_transform_params_to_icc (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
  ClutterColorState *blend_color_state = NULL;
  g_autoptr (ClutterColorState) target_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_SRGB,
                                    CLUTTER_TRANSFER_FUNCTION_GAMMA22);
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  target_color_state = create_icc_color_state ("sRGB.icc");
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, target_color_state);
  blend_color_state =
    clutter_stage_view_get_color_state (stage_view);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, blend_color_state,
                      target_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

static void
color_state_transform_icc_to_icc (void)
{
  g_autoptr (ClutterColorState) src_color_state = NULL;
  ClutterColorState *blend_color_state = NULL;
  g_autoptr (ClutterColorState) target_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state = create_icc_color_state ("vx239-calibrated.icc");
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  target_color_state = create_icc_color_state ("sRGB.icc");
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, target_color_state);
  blend_color_state =
    clutter_stage_view_get_color_state (stage_view);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, blend_color_state,
                      target_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

static void
color_state_transform_params_to_params (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
  ClutterColorState *blend_color_state = NULL;
  g_autoptr (ClutterColorState) output_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_SRGB,
                                    CLUTTER_TRANSFER_FUNCTION_GAMMA22);
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  output_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_BT2020,
                                    CLUTTER_TRANSFER_FUNCTION_PQ);
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, output_color_state);
  blend_color_state =
    clutter_stage_view_get_color_state (stage_view);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, blend_color_state,
                      output_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

static void
color_state_transform_bt2020_to_bt2020 (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
  g_autoptr (ClutterColorState) output_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state =
    clutter_color_state_params_new_full (context,
                                         CLUTTER_COLORSPACE_BT2020,
                                         CLUTTER_TRANSFER_FUNCTION_GAMMA22,
                                         NULL,
                                         -1.0f,
                                         0.005f,
                                         203.0f,
                                         203.0f,
                                         -1.0f);
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  output_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_BT2020,
                                    CLUTTER_TRANSFER_FUNCTION_PQ);
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, output_color_state);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, NULL, output_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

typedef enum _ColorStateType
{
  COLOR_STATE_TYPE_PARAMS,
  COLOR_STATE_TYPE_ICC,
  COLOR_STATE_TYPE_ICC_CLUT,
} ColorStateType;

typedef struct _TransformTestCase
{
  const char *name;
  ColorStateType src_type;
  union {
    struct {
      ClutterColorspace colorspace;
      ClutterTransferFunction tf;
    } params;
    const char *icc_filename;
  } src;
  ColorStateType target_type;
  union {
    struct {
      ClutterColorspace colorspace;
      ClutterTransferFunction tf;
    } params;
    const char *icc_filename;
  } target;
} TransformTestCase;

static TransformTestCase test_cases[] = {
  {
    .name = "ICC sRGB to ICC vx239",
    .src_type = COLOR_STATE_TYPE_ICC,
    .src.icc_filename = "sRGB.icc",
    .target_type = COLOR_STATE_TYPE_ICC,
    .target.icc_filename = "vx239-calibrated.icc",
  },
  {
    .name = "sRGB identity",
    .src_type = COLOR_STATE_TYPE_PARAMS,
    .src.params = {
      .colorspace = CLUTTER_COLORSPACE_SRGB,
      .tf = CLUTTER_TRANSFER_FUNCTION_GAMMA22,
    },
    .target_type = COLOR_STATE_TYPE_PARAMS,
    .target.params = {
      .colorspace = CLUTTER_COLORSPACE_SRGB,
      .tf = CLUTTER_TRANSFER_FUNCTION_GAMMA22,
    },
  },
  {
    .name = "sRGB to BT2020/PQ",
    .src_type = COLOR_STATE_TYPE_PARAMS,
    .src.params = {
      .colorspace = CLUTTER_COLORSPACE_SRGB,
      .tf = CLUTTER_TRANSFER_FUNCTION_GAMMA22,
    },
    .target_type = COLOR_STATE_TYPE_PARAMS,
    .target.params = {
      .colorspace = CLUTTER_COLORSPACE_BT2020,
      .tf = CLUTTER_TRANSFER_FUNCTION_PQ,
    },
  },
  {
    .name = "ICC sRGB to PARAMS sRGB",
    .src_type = COLOR_STATE_TYPE_ICC,
    .src.icc_filename = "sRGB.icc",
    .target_type = COLOR_STATE_TYPE_PARAMS,
    .target.params = {
      .colorspace = CLUTTER_COLORSPACE_SRGB,
      .tf = CLUTTER_TRANSFER_FUNCTION_GAMMA22,
    },
  },
  {
    .name = "PARAMS sRGB to ICC sRGB",
    .src_type = COLOR_STATE_TYPE_PARAMS,
    .src.params = {
      .colorspace = CLUTTER_COLORSPACE_SRGB,
      .tf = CLUTTER_TRANSFER_FUNCTION_GAMMA22,
    },
    .target_type = COLOR_STATE_TYPE_ICC,
    .target.icc_filename = "sRGB.icc",
  },
  {
    .name = "ICC CLUT to PARAMS sRGB",
    .src_type = COLOR_STATE_TYPE_ICC_CLUT,
    .target_type = COLOR_STATE_TYPE_PARAMS,
    .target.params = {
      .colorspace = CLUTTER_COLORSPACE_SRGB,
      .tf = CLUTTER_TRANSFER_FUNCTION_GAMMA22,
    },
  },
  {
    .name = "PARAMS sRGB to ICC CLUT",
    .src_type = COLOR_STATE_TYPE_PARAMS,
    .src.params = {
      .colorspace = CLUTTER_COLORSPACE_SRGB,
      .tf = CLUTTER_TRANSFER_FUNCTION_GAMMA22,
    },
    .target_type = COLOR_STATE_TYPE_ICC_CLUT,
  },
  {
    .name = "ICC CLUT to ICC sRGB",
    .src_type = COLOR_STATE_TYPE_ICC_CLUT,
    .target_type = COLOR_STATE_TYPE_ICC,
    .target.icc_filename = "sRGB.icc",
  },
};

static ClutterColorState *
create_test_color_state (ClutterContext          *context,
                         ColorStateType           type,
                         ClutterColorspace        colorspace,
                         ClutterTransferFunction  tf,
                         const char              *icc_filename)
{
  switch (type)
    {
    case COLOR_STATE_TYPE_PARAMS:
      return clutter_color_state_params_new (context, colorspace, tf);
    case COLOR_STATE_TYPE_ICC:
      return create_icc_color_state (icc_filename);
    case COLOR_STATE_TYPE_ICC_CLUT:
      return create_clut_icc_color_state ();
    }

  g_assert_not_reached ();
}

static void
test_cpu_transform (TransformTestCase *test_case)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
  g_autoptr (ClutterColorState) target_color_state = NULL;
  ClutterColorTransform *transform;
  ClutterColorPipeline *color_pipeline;

  src_color_state = create_test_color_state (context,
                                             test_case->src_type,
                                             test_case->src.params.colorspace,
                                             test_case->src.params.tf,
                                             test_case->src.icc_filename);
  target_color_state = create_test_color_state (context,
                                                test_case->target_type,
                                                test_case->target.params.colorspace,
                                                test_case->target.params.tf,
                                                test_case->target.icc_filename);

  transform = clutter_color_transform_from_color_states (context,
                                                         src_color_state,
                                                         target_color_state,
                                                         CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE);
  color_pipeline = clutter_color_transform_get_pipeline (transform);

  for (int i = 0; i < G_N_ELEMENTS (test_colors); i++)
    {
      float pipeline_result[4];

      pipeline_result[0] = test_colors[i].r;
      pipeline_result[1] = test_colors[i].g;
      pipeline_result[2] = test_colors[i].b;
      pipeline_result[3] = test_colors[i].a;
      clutter_color_pipeline_do_transform (color_pipeline,
                                           pipeline_result,
                                           1);

      g_assert_true (isfinite (pipeline_result[0]));
      g_assert_true (isfinite (pipeline_result[1]));
      g_assert_true (isfinite (pipeline_result[2]));
    }
}

static void
color_pipeline_vs_color_state_cpu (void)
{
  for (int i = 0; i < G_N_ELEMENTS (test_cases); i++)
    test_cpu_transform (&test_cases[i]);
}

static void
test_shader_transform (TransformTestCase *test_case)
{
  ClutterContext *context = clutter_test_get_context ();
  ClutterBackend *backend = clutter_test_get_backend ();
  CoglContext *cogl_context = clutter_backend_get_cogl_context (backend);
  g_autoptr (ClutterColorState) src_color_state = NULL;
  g_autoptr (ClutterColorState) target_color_state = NULL;
  ClutterColorTransform *transform;
  g_autoptr (CoglPipeline) pipeline1 = NULL;
  g_autoptr (CoglPipeline) pipeline2 = NULL;
  g_autoptr (CoglOffscreen) offscreen1 = NULL;
  g_autoptr (CoglOffscreen) offscreen2 = NULL;
  g_autoptr (CoglTexture) texture1 = NULL;
  g_autoptr (CoglTexture) texture2 = NULL;
  g_autoptr (GError) error = NULL;
  CoglFramebuffer *fb1, *fb2;
  ClutterColorPipeline *color_pipeline;
  int n_colors = G_N_ELEMENTS (test_colors);
  float epsilon;

  src_color_state = create_test_color_state (context,
                                             test_case->src_type,
                                             test_case->src.params.colorspace,
                                             test_case->src.params.tf,
                                             test_case->src.icc_filename);
  target_color_state = create_test_color_state (context,
                                                test_case->target_type,
                                                test_case->target.params.colorspace,
                                                test_case->target.params.tf,
                                                test_case->target.icc_filename);

  /* Create textures and framebuffers */
  texture1 = cogl_texture_2d_new_with_format (cogl_context,
                                              n_colors, 1,
                                              COGL_PIXEL_FORMAT_RGBA_FP_32323232);
  offscreen1 = cogl_offscreen_new_with_texture (texture1);
  fb1 = COGL_FRAMEBUFFER (offscreen1);
  g_assert_true (cogl_framebuffer_allocate (fb1, &error));

  texture2 = cogl_texture_2d_new_with_format (cogl_context,
                                              n_colors, 1,
                                              COGL_PIXEL_FORMAT_RGBA_FP_32323232);
  offscreen2 = cogl_offscreen_new_with_texture (texture2);
  fb2 = COGL_FRAMEBUFFER (offscreen2);
  g_assert_true (cogl_framebuffer_allocate (fb2, &error));

  cogl_framebuffer_orthographic (fb1, 0, 0, n_colors, 1, -1, 1);
  cogl_framebuffer_orthographic (fb2, 0, 0, n_colors, 1, -1, 1);
  cogl_framebuffer_clear4f (fb1, COGL_BUFFER_BIT_COLOR, 0.0f, 0.0f, 0.0f, 0.0f);
  cogl_framebuffer_clear4f (fb2, COGL_BUFFER_BIT_COLOR, 0.0f, 0.0f, 0.0f, 0.0f);

  /* Pipeline 1: using cached ClutterColorTransform */
  pipeline1 = cogl_pipeline_new (cogl_context);
  {
    ClutterColorTransform *transform1;
    ClutterColorPipeline *cp;

    transform1 =
      clutter_color_transform_from_color_states (context,
                                                 src_color_state,
                                                 target_color_state,
                                                 CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE);
    cp = clutter_color_transform_get_pipeline (transform1);
    clutter_color_pipeline_shader_add_transform (cp, pipeline1);
  }

  for (int i = 0; i < n_colors; i++)
    {
      CoglColor color;
      cogl_color_init_from_4f (&color,
                               test_colors[i].r,
                               test_colors[i].g,
                               test_colors[i].b,
                               test_colors[i].a);
      cogl_pipeline_set_color (pipeline1, &color);
      cogl_framebuffer_draw_rectangle (fb1, pipeline1, i, 0, i + 1, 1);
    }
  cogl_framebuffer_finish (fb1);

  /* Pipeline 2: using same cached transform */
  pipeline2 = cogl_pipeline_new (cogl_context);
  transform = clutter_color_transform_from_color_states (context,
                                                         src_color_state,
                                                         target_color_state,
                                                         CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE);
  color_pipeline = clutter_color_transform_get_pipeline (transform);

  clutter_color_pipeline_shader_add_transform (color_pipeline,
                                               pipeline2);

  for (int i = 0; i < n_colors; i++)
    {
      CoglColor color;
      cogl_color_init_from_4f (&color,
                               test_colors[i].r,
                               test_colors[i].g,
                               test_colors[i].b,
                               test_colors[i].a);
      cogl_pipeline_set_color (pipeline2, &color);
      cogl_framebuffer_draw_rectangle (fb2, pipeline2, i, 0, i + 1, 1);
    }
  cogl_framebuffer_finish (fb2);

  /* Compare results - use looser epsilon for ICC transforms due to 3D LUT sampling */
  epsilon = (test_case->src_type == COLOR_STATE_TYPE_ICC ||
             test_case->target_type == COLOR_STATE_TYPE_ICC)
            ? COLOR_TRANSFORM_EPSILON_ICC
            : COLOR_TRANSFORM_EPSILON;

  for (int i = 0; i < n_colors; i++)
    {
      float result1[4], result2[4];

      cogl_framebuffer_read_pixels (fb1, i, 0, 1, 1,
                                    COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE,
                                    (uint8_t *) result1);
      cogl_framebuffer_read_pixels (fb2, i, 0, 1, 1,
                                    COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE,
                                    (uint8_t *) result2);

      if (!G_APPROX_VALUE (result1[0], result2[0], epsilon) ||
          !G_APPROX_VALUE (result1[1], result2[1], epsilon) ||
          !G_APPROX_VALUE (result1[2], result2[2], epsilon))
        {
          g_autofree char *pipeline_str = NULL;

          pipeline_str = clutter_color_pipeline_to_string (color_pipeline);
          g_test_message ("Shader transform mismatch [%s] pixel %d:\n"
                          "pipeline:        %s\n"
                          "input:           (%.5f, %.5f, %.5f, %.5f)\n"
                          "color_state:     (%.5f, %.5f, %.5f)\n"
                          "color_pipeline:  (%.5f, %.5f, %.5f)\n"
                          "diff:            (%.5f, %.5f, %.5f)\n",
                          test_case->name, i,
                          pipeline_str,
                          test_colors[i].r, test_colors[i].g,
                          test_colors[i].b, test_colors[i].a,
                          result1[0], result1[1], result1[2],
                          result2[0], result2[1], result2[2],
                          ABS (result1[0] - result2[0]),
                          ABS (result1[1] - result2[1]),
                          ABS (result1[2] - result2[2]));
          g_assert_not_reached ();
        }
    }
}

static void
color_pipeline_vs_color_state_shader (void)
{
  for (int i = 0; i < G_N_ELEMENTS (test_cases); i++)
    test_shader_transform (&test_cases[i]);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/color-state-transform/pipeline-vs-state-cpu", color_pipeline_vs_color_state_cpu)
  CLUTTER_TEST_UNIT ("/color-state-transform/pipeline-vs-state-shader", color_pipeline_vs_color_state_shader)
  CLUTTER_TEST_UNIT ("/color-state-transform/icc-to-params", color_state_transform_icc_to_params)
  CLUTTER_TEST_UNIT ("/color-state-transform/params-to-icc", color_state_transform_params_to_icc)
  CLUTTER_TEST_UNIT ("/color-state-transform/icc-to-icc", color_state_transform_icc_to_icc)
  CLUTTER_TEST_UNIT ("/color-state-transform/params-to-params", color_state_transform_params_to_params)
  CLUTTER_TEST_UNIT ("/color-state-transform/bt2020-to-bt2020", color_state_transform_bt2020_to_bt2020)
)
