#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/mman.h>

#include "clutter-mutter.h"
#include "tests/clutter-test-utils.h"

static void
assert_pipeline_matches (ClutterColorPipeline *pipeline,
                         const char           *expected_pipeline_str)
{
  g_autofree char *pipeline_str = NULL;

  pipeline_str = clutter_color_pipeline_to_string (pipeline);
  g_assert_cmpstr (pipeline_str, ==, expected_pipeline_str);
}

static void
assert_transform_matches (ClutterColorPipeline *pipeline,
                          float                *input,
                          float                *expected,
                          size_t                size)
{
  g_autofree float *result = NULL;

  result = g_memdup2 (input, sizeof (*input) * size * 4);

  clutter_color_pipeline_do_transform (pipeline, result, size);

  for (size_t i = 0; i < size * 4; i++)
    {
      g_assert_cmpfloat_with_epsilon (result[i],
                                      expected[i],
                                      0.001);
    }
}

static void
assert_transform_matches_clamped (ClutterColorPipeline *pipeline,
                                  float                *input,
                                  float                *expected,
                                  size_t                size)
{
  g_autofree float *result = NULL;

  result = g_memdup2 (input, sizeof (*input) * size * 4);

  for (size_t i = 0; i < size * 4; i++)
    result[i] = (float) CLAMP (result[i], 0.0, 1.0);

  clutter_color_pipeline_do_transform (pipeline, result, size);

  for (size_t i = 0; i < size * 4; i++)
    {
      g_assert_cmpfloat_with_epsilon (result[i],
                                      CLAMP (expected[i], 0.0, 1.0),
                                      0.001);
    }
}

static void
assert_transform_matches_loose (ClutterColorPipeline *pipeline,
                                float                *input,
                                float                *expected,
                                size_t                size)
{
  g_autofree float *result = NULL;

  result = g_memdup2 (input, sizeof (*input) * size * 4);

  clutter_color_pipeline_do_transform (pipeline, result, size);

  for (size_t i = 0; i < size * 4; i++)
    {
      g_assert_cmpfloat_with_epsilon (result[i],
                                      expected[i],
                                      0.003);
    }
}

static void
color_pipeline_basic (void)
{
  g_autoptr (ClutterColorPipeline) p1 = NULL;

  p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
  assert_pipeline_matches (p1, "ClutterColorPipeline: [empty]");

  clutter_color_pipeline_take_op (p1, clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE));
  assert_pipeline_matches (p1, "ClutterColorPipeline: srgb_piecewise_eotf");

  {
    float *v = g_new0 (float, 1);
    v[0] = 0.5f;

    clutter_color_pipeline_take_op (p1, clutter_color_op_curve_1d_new_rgb (1, v));
    assert_pipeline_matches (p1, "ClutterColorPipeline: srgb_piecewise_eotf -> curve_1d");
  }

  clutter_color_pipeline_take_op (p1, clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE));
  assert_pipeline_matches (p1, "ClutterColorPipeline: srgb_piecewise_eotf -> curve_1d -> srgb_piecewise_eotf");

  /* simplify does not combine curve_1d ops, so pipeline stays the same */
  clutter_color_pipeline_simplify (p1);
  assert_pipeline_matches (p1, "ClutterColorPipeline: srgb_piecewise_eotf -> curve_1d -> srgb_piecewise_eotf");

  /* test inverse pair cancellation: srgb_piecewise_eotf + srgb_piecewise_inv_eotf should cancel */
  clutter_color_pipeline_take_op (p1, clutter_color_op_srgb_piecewise_inv_eotf_new (/* unit_range_only = */ TRUE));
  assert_pipeline_matches (p1, "ClutterColorPipeline: srgb_piecewise_eotf -> curve_1d -> srgb_piecewise_eotf -> srgb_piecewise_inv_eotf");

  clutter_color_pipeline_simplify (p1);
  assert_pipeline_matches (p1, "ClutterColorPipeline: srgb_piecewise_eotf -> curve_1d");
}

static void
color_pipeline_eval_srgb (void)
{
  g_autoptr (ClutterColorPipeline) p1 = NULL;
  float input[] = {
    0.0f, 0.1f, 0.2f, 1.0f,
    0.4f, 0.5f, 0.6f, 0.5f,
    0.8f, 0.9f, 1.0f, 0.1f,
  };
  float expected[] = {
    0.000f, 0.010f, 0.033f, 1.000f,
    0.133f, 0.214f, 0.319f, 0.500f,
    0.604f, 0.787f, 1.000f, 0.100f,
  };

  p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
  assert_pipeline_matches (p1, "ClutterColorPipeline: [empty]");

  /* Test simplify: srgb_piecewise_eotf + srgb_piecewise_inv_eotf should cancel */
  clutter_color_pipeline_take_op (p1, clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE));
  assert_pipeline_matches (p1, "ClutterColorPipeline: srgb_piecewise_eotf");

  clutter_color_pipeline_take_op (p1, clutter_color_op_srgb_piecewise_inv_eotf_new (/* unit_range_only = */ TRUE));
  assert_pipeline_matches (p1, "ClutterColorPipeline: srgb_piecewise_eotf -> srgb_piecewise_inv_eotf");

  clutter_color_pipeline_simplify (p1);
  assert_pipeline_matches (p1, "ClutterColorPipeline: [empty]");

  /* Test srgb_piecewise_eotf transform */
  clutter_color_pipeline_take_op (p1, clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE));
  assert_pipeline_matches (p1, "ClutterColorPipeline: srgb_piecewise_eotf");

  assert_transform_matches (p1, input, expected, G_N_ELEMENTS (input) / 4);

  /* Test lowering srgb_piecewise_eotf to curve_1d */
  {
    g_autoptr (ClutterColorPipeline) p2 = NULL;
    g_autoptr (ClutterColorOp) eotf_op = NULL;
    g_autoptr (ClutterColorOp) lowered = NULL;

    eotf_op = clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE);
    g_assert_true (clutter_color_op_can_lower_to_curve_1d (eotf_op, 256));
    lowered = clutter_color_op_lower_to_curve_1d (eotf_op, 256);
    g_assert_nonnull (lowered);
    g_assert_true (CLUTTER_IS_COLOR_OP_CURVE_1D (lowered));

    p2 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_add_op (p2, lowered);
    assert_pipeline_matches (p2, "ClutterColorPipeline: curve_1d");

    assert_transform_matches_clamped (p2, input, expected, G_N_ELEMENTS (input) / 4);
  }
}

static void
color_pipeline_eval_gamma (void)
{
  g_autoptr (ClutterColorPipeline) p1 = NULL;
  float input[] = {
    0.0f,  1.2f,  0.2f, 0.3f,
    0.8f, -1.0f, -0.2f, 1.0f,
  };
  float expected[] = {
    0.000f,  1.493f,  0.029f, 0.300f,
    0.612f, -1.000f, -0.029f, 1.000f,
  };

  p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
  assert_pipeline_matches (p1, "ClutterColorPipeline: [empty]");

  clutter_color_pipeline_take_op (p1, clutter_color_op_gamma_power_new (2.2f));
  assert_pipeline_matches (p1, "ClutterColorPipeline: gamma_power(2.20)");

  clutter_color_pipeline_take_op (p1, clutter_color_op_gamma_power_new (1.0f));
  assert_pipeline_matches (p1, "ClutterColorPipeline: gamma_power(2.20) -> gamma_power(1.00)");

  clutter_color_pipeline_take_op (p1, clutter_color_op_gamma_power_new (1.0f / 2.2f));
  assert_pipeline_matches (p1, "ClutterColorPipeline: gamma_power(2.20) -> gamma_power(1.00) -> gamma_power(0.45)");

  /* simplify only cancels inverse pairs (a*b approx 1.0); gamma(2.2)*gamma(1.0)
   * is not an inverse pair, so all 3 ops remain */
  clutter_color_pipeline_simplify (p1);
  assert_pipeline_matches (p1, "ClutterColorPipeline: gamma_power(2.20) -> gamma_power(1.00) -> gamma_power(0.45)");

  /* Test that gamma_power(2.2) + gamma_power(1/2.2) ARE an inverse pair */
  {
    g_autoptr (ClutterColorPipeline) p2 = NULL;

    p2 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_take_op (p2, clutter_color_op_gamma_power_new (2.2f));
    clutter_color_pipeline_take_op (p2, clutter_color_op_gamma_power_new (1.0f / 2.2f));
    assert_pipeline_matches (p2, "ClutterColorPipeline: gamma_power(2.20) -> gamma_power(0.45)");

    clutter_color_pipeline_simplify (p2);
    assert_pipeline_matches (p2, "ClutterColorPipeline: [empty]");
  }

  /* Test gamma_power transform */
  {
    g_autoptr (ClutterColorPipeline) p3 = NULL;

    p3 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_take_op (p3, clutter_color_op_gamma_power_new (2.2f));
    assert_pipeline_matches (p3, "ClutterColorPipeline: gamma_power(2.20)");

    assert_transform_matches (p3, input, expected, G_N_ELEMENTS (input) / 4);
  }

  /* Test lowering gamma_power to curve_1d */
  {
    g_autoptr (ClutterColorPipeline) p4 = NULL;
    g_autoptr (ClutterColorOp) gamma_op = NULL;
    g_autoptr (ClutterColorOp) lowered = NULL;

    gamma_op = clutter_color_op_gamma_power_new (2.2f);
    g_assert_true (clutter_color_op_can_lower_to_curve_1d (gamma_op, 256));
    lowered = clutter_color_op_lower_to_curve_1d (gamma_op, 256);
    g_assert_nonnull (lowered);
    g_assert_true (CLUTTER_IS_COLOR_OP_CURVE_1D (lowered));

    p4 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_add_op (p4, lowered);
    assert_pipeline_matches (p4, "ClutterColorPipeline: curve_1d");

    assert_transform_matches_clamped (p4, input, expected, G_N_ELEMENTS (input) / 4);
  }
}

static void
color_pipeline_eval_pq (void)
{
  g_autoptr (ClutterColorPipeline) p1 = NULL;
  g_autoptr (ClutterColorPipeline) p2 = NULL;
  float linear[] = {
    0.0f, 0.1f, 0.2f, 1.0f,
    0.5f, 1.0f, 0.0f, 1.0f,
  };
  float pq_encoded[] = {
    0.000f, 0.752f, 0.827f, 1.000f,
    0.927f, 1.000f, 0.000f, 1.0f,
  };

  /* Test PQ EOTF: PQ-encoded -> linear (with loose tolerance due to float precision) */
  p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
  clutter_color_pipeline_take_op (p1, clutter_color_op_pq_eotf_new ());
  assert_pipeline_matches (p1, "ClutterColorPipeline: pq_eotf");
  assert_transform_matches_loose (p1, pq_encoded, linear, G_N_ELEMENTS (linear) / 4);

  /* Test PQ inverse EOTF: linear -> PQ-encoded (with loose tolerance) */
  p2 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
  clutter_color_pipeline_take_op (p2, clutter_color_op_pq_inv_eotf_new ());
  assert_pipeline_matches (p2, "ClutterColorPipeline: pq_inv_eotf");
  assert_transform_matches_loose (p2, linear, pq_encoded, G_N_ELEMENTS (linear) / 4);

  /* Test round-trip: PQ EOTF followed by PQ inverse EOTF */
  clutter_color_pipeline_take_op (p1, clutter_color_op_pq_inv_eotf_new ());
  assert_pipeline_matches (p1, "ClutterColorPipeline: pq_eotf -> pq_inv_eotf");
  assert_transform_matches_loose (p1, pq_encoded, pq_encoded, G_N_ELEMENTS (linear) / 4);

  /* Test simplify: PQ EOTF + PQ inverse EOTF should simplify to empty pipeline */
  clutter_color_pipeline_simplify (p1);
  assert_pipeline_matches (p1, "ClutterColorPipeline: [empty]");
  assert_transform_matches (p1, pq_encoded, pq_encoded, G_N_ELEMENTS (linear) / 4);

  /* PQ needs at least 4096 entries for adequate precision in the darks */
  {
    g_autoptr (ClutterColorOp) pq_op = NULL;
    g_autoptr (ClutterColorOp) lowered = NULL;

    pq_op = clutter_color_op_pq_eotf_new ();
    g_assert_false (clutter_color_op_can_lower_to_curve_1d (pq_op, 1024));
    g_assert_true (clutter_color_op_can_lower_to_curve_1d (pq_op, 4096));
    lowered = clutter_color_op_lower_to_curve_1d (pq_op, 4096);
    g_assert_nonnull (lowered);
  }
}

static void
color_pipeline_3d_lut (void)
{
  g_autoptr (ClutterColorPipeline) pipeline = NULL;
  float input[] = {
    0.0f, 0.0f, 0.0f, 1.0f,
    0.5f, 0.5f, 0.5f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f,
  };
  float expected[] = {
    0.0f, 0.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f,
    2.0f, 2.0f, 2.0f, 1.0f,
  };

  /* Create a simple 3D LUT that doubles the input values */
  size_t lut_size = 3;
  g_autofree float *lut_data = g_new (float, lut_size * lut_size * lut_size * 3);

  for (size_t b = 0; b < lut_size; b++)
    for (size_t g = 0; g < lut_size; g++)
      for (size_t r = 0; r < lut_size; r++)
        {
          size_t idx = (b * lut_size * lut_size + g * lut_size + r) * 3;
          float r_val = (float) r / (lut_size - 1);
          float g_val = (float) g / (lut_size - 1);
          float b_val = (float) b / (lut_size - 1);

          lut_data[idx + 0] = r_val * 2.0f;
          lut_data[idx + 1] = g_val * 2.0f;
          lut_data[idx + 2] = b_val * 2.0f;
        }

  pipeline = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_3d_lut_new (lut_size, lut_data));
  assert_pipeline_matches (pipeline, "ClutterColorPipeline: 3d_lut(3)");
  assert_transform_matches (pipeline, input, expected, G_N_ELEMENTS (input) / 4);

  /* Test 3D LUT + 3D LUT composition */
  {
    g_autoptr (ClutterColorPipeline) p2 = NULL;
    g_autofree float *lut_data2 = g_new (float, lut_size * lut_size * lut_size * 3);
    float input2[] = {
      0.0f, 0.0f, 0.0f, 1.0f,
      0.5f, 0.5f, 0.5f, 1.0f,
    };
    float expected2[] = {
      0.0f, 0.0f, 0.0f, 1.0f,
      2.0f, 2.0f, 2.0f, 1.0f,
    };

    /* Second LUT also doubles values */
    for (size_t b = 0; b < lut_size; b++)
      for (size_t g = 0; g < lut_size; g++)
        for (size_t r = 0; r < lut_size; r++)
          {
            size_t idx = (b * lut_size * lut_size + g * lut_size + r) * 3;
            float r_val = (float) r / (lut_size - 1);
            float g_val = (float) g / (lut_size - 1);
            float b_val = (float) b / (lut_size - 1);

            lut_data2[idx + 0] = r_val * 2.0f;
            lut_data2[idx + 1] = g_val * 2.0f;
            lut_data2[idx + 2] = b_val * 2.0f;
          }

    p2 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_take_op (p2, clutter_color_op_3d_lut_new (lut_size, lut_data));
    clutter_color_pipeline_take_op (p2, clutter_color_op_3d_lut_new (lut_size, lut_data2));
    assert_pipeline_matches (p2, "ClutterColorPipeline: 3d_lut(3) -> 3d_lut(3)");

    /* First LUT: 0.5 -> 1.0, Second LUT: 1.0 -> 2.0 */
    assert_transform_matches (p2, input2, expected2, G_N_ELEMENTS (input2) / 4);
  }

  /* Test 3D LUT + curve_1d composition */
  {
    g_autoptr (ClutterColorPipeline) p3 = NULL;
    float input3[] = {
      0.0f, 0.0f, 0.0f, 1.0f,
      0.5f, 0.5f, 0.5f, 1.0f,
    };
    float expected3[] = {
      0.0f, 0.0f, 0.0f, 1.0f,
      1.0f, 1.0f, 1.0f, 1.0f,
    };

    /* Identity curve: 0->0, 0.5->0.5, 1->1 */
    float *curve_data = g_new0 (float, 3);
    curve_data[0] = 0.0f;
    curve_data[1] = 0.5f;
    curve_data[2] = 1.0f;

    p3 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_take_op (p3, clutter_color_op_3d_lut_new (lut_size, lut_data));
    clutter_color_pipeline_take_op (p3, clutter_color_op_curve_1d_new_rgb (3, curve_data));
    assert_pipeline_matches (p3, "ClutterColorPipeline: 3d_lut(3) -> curve_1d");

    /* LUT doubles (0.5 -> 1.0), curve identity (1.0 -> 1.0), result: 1.0 */
    assert_transform_matches (p3, input3, expected3, G_N_ELEMENTS (input3) / 4);
  }
}

static void
color_pipeline_matrix_swap_channels (void)
{
  g_autoptr (ClutterColorPipeline) p1 = NULL;
  graphene_matrix_t *swap_rb;
  float input[] = {
    0.2f, 0.4f, 0.6f, 0.8f,
    1.0f, 0.5f, 0.25f, 0.125f,
  };
  float expected[] = {
    0.6f, 0.4f, 0.2f, 0.8f,
    0.25f, 0.5f, 1.0f, 0.125f,
  };

  /* Swap red and blue channels */
  swap_rb = graphene_matrix_alloc ();
  graphene_matrix_init_from_float (swap_rb, (float[16]) {
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });

  p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
  clutter_color_pipeline_take_op (p1, clutter_color_op_matrix_4x4_new (swap_rb));
  assert_pipeline_matches (p1, "ClutterColorPipeline: matrix_4x4");

  assert_transform_matches (p1, input, expected, G_N_ELEMENTS (input) / 4);
}

static void
color_pipeline_matrix_merge (void)
{
  g_autoptr (ClutterColorPipeline) p1 = NULL;
  g_autoptr (ClutterColorOp) op1 = NULL;
  g_autoptr (ClutterColorOp) op2 = NULL;
  g_autoptr (ClutterColorOp) combined = NULL;
  graphene_matrix_t *matrix;
  float input[] = {
    0.2f, 0.4f, 0.6f, 0.8f,
  };
  float expected[] = {
    0.4f, 1.2f, 0.6f, 0.8f,
  };

  /* Test two-op pipeline produces correct transform */
  p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);

  /* First matrix: scale R by 2 */
  matrix = graphene_matrix_alloc ();
  graphene_matrix_init_from_float (matrix, (float[16]) {
    2.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });
  clutter_color_pipeline_take_op (p1, clutter_color_op_matrix_4x4_new (matrix));

  /* Second matrix: scale G by 3 */
  matrix = graphene_matrix_alloc ();
  graphene_matrix_init_from_float (matrix, (float[16]) {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 3.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });
  clutter_color_pipeline_take_op (p1, clutter_color_op_matrix_4x4_new (matrix));

  assert_pipeline_matches (p1, "ClutterColorPipeline: matrix_4x4 -> matrix_4x4");
  assert_transform_matches (p1, input, expected, G_N_ELEMENTS (input) / 4);

  /* Test try_combine: matrix + matrix should combine into single matrix */
  matrix = graphene_matrix_alloc ();
  graphene_matrix_init_from_float (matrix, (float[16]) {
    2.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });
  op1 = clutter_color_op_matrix_4x4_new (matrix);

  matrix = graphene_matrix_alloc ();
  graphene_matrix_init_from_float (matrix, (float[16]) {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 3.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });
  op2 = clutter_color_op_matrix_4x4_new (matrix);

  combined = clutter_color_op_try_combine (op1, op2);
  g_assert_nonnull (combined);
  g_assert_true (CLUTTER_IS_COLOR_OP_MATRIX_4X4 (combined));

  /* Combined matrix should produce same result */
  {
    g_autoptr (ClutterColorPipeline) p2 = NULL;

    p2 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_add_op (p2, combined);
    assert_pipeline_matches (p2, "ClutterColorPipeline: matrix_4x4");
    assert_transform_matches (p2, input, expected, G_N_ELEMENTS (input) / 4);
  }
}

static void
color_pipeline_multiply_basic (void)
{
  g_autoptr (ClutterColorPipeline) p1 = NULL;
  float input[] = {
    0.2f, 0.4f, 0.6f, 0.8f,
    1.0f, 0.5f, 0.25f, 1.0f,
  };
  float expected[] = {
    0.4f, 0.8f, 1.2f, 0.8f,
    2.0f, 1.0f, 0.5f, 1.0f,
  };

  p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
  clutter_color_pipeline_take_op (p1, clutter_color_op_multiply_new (2.0f));
  assert_pipeline_matches (p1, "ClutterColorPipeline: multiply(2.00)");

  /* RGB channels should be multiplied by 2, alpha unchanged */
  assert_transform_matches (p1, input, expected, G_N_ELEMENTS (input) / 4);
}

static void
color_pipeline_multiply_merge (void)
{
  g_autoptr (ClutterColorPipeline) p1 = NULL;
  g_autoptr (ClutterColorOp) op1 = NULL;
  g_autoptr (ClutterColorOp) op2 = NULL;
  g_autoptr (ClutterColorOp) combined = NULL;
  float input[] = {
    0.2f, 0.4f, 0.6f, 0.8f,
  };
  float expected[] = {
    1.2f, 2.4f, 3.6f, 0.8f,
  };

  /* Test two-op pipeline produces correct transform */
  p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
  clutter_color_pipeline_take_op (p1, clutter_color_op_multiply_new (2.0f));
  clutter_color_pipeline_take_op (p1, clutter_color_op_multiply_new (3.0f));
  assert_pipeline_matches (p1, "ClutterColorPipeline: multiply(2.00) -> multiply(3.00)");
  assert_transform_matches (p1, input, expected, G_N_ELEMENTS (input) / 4);

  /* Test try_combine: multiply + multiply should combine into single multiply */
  op1 = clutter_color_op_multiply_new (2.0f);
  op2 = clutter_color_op_multiply_new (3.0f);
  combined = clutter_color_op_try_combine (op1, op2);
  g_assert_nonnull (combined);
  g_assert_true (CLUTTER_IS_COLOR_OP_MULTIPLY (combined));
  g_assert_cmpfloat_with_epsilon (clutter_color_op_multiply_get_value (combined), 6.0f, 0.001f);

  /* Combined multiply should produce same result */
  {
    g_autoptr (ClutterColorPipeline) p2 = NULL;

    p2 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_add_op (p2, combined);
    assert_pipeline_matches (p2, "ClutterColorPipeline: multiply(6.00)");
    assert_transform_matches (p2, input, expected, G_N_ELEMENTS (input) / 4);
  }
}

static void
color_pipeline_multiply_matrix_merge (void)
{
  graphene_matrix_t *matrix;
  float input[] = {
    0.2f, 0.4f, 0.6f, 0.8f,
  };
  float expected[] = {
    1.2f, 0.8f, 0.4f, 0.8f,
  };

  /* Test: multiply followed by matrix (swap R and B) */
  {
    g_autoptr (ClutterColorPipeline) p1 = NULL;
    g_autoptr (ClutterColorOp) mul_op = NULL;
    g_autoptr (ClutterColorOp) mat_op = NULL;
    g_autoptr (ClutterColorOp) combined = NULL;

    p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);

    clutter_color_pipeline_take_op (p1, clutter_color_op_multiply_new (2.0f));

    matrix = graphene_matrix_alloc ();
    graphene_matrix_init_from_float (matrix, (float[16]) {
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
    });
    clutter_color_pipeline_take_op (p1, clutter_color_op_matrix_4x4_new (matrix));

    assert_pipeline_matches (p1, "ClutterColorPipeline: multiply(2.00) -> matrix_4x4");
    assert_transform_matches (p1, input, expected, G_N_ELEMENTS (input) / 4);

    /* Test try_combine: multiply + matrix should combine into single matrix */
    mul_op = clutter_color_op_multiply_new (2.0f);
    matrix = graphene_matrix_alloc ();
    graphene_matrix_init_from_float (matrix, (float[16]) {
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
    });
    mat_op = clutter_color_op_matrix_4x4_new (matrix);
    combined = clutter_color_op_try_combine (mul_op, mat_op);
    g_assert_nonnull (combined);
    g_assert_true (CLUTTER_IS_COLOR_OP_MATRIX_4X4 (combined));

    {
      g_autoptr (ClutterColorPipeline) p_combined = NULL;

      p_combined = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
      clutter_color_pipeline_add_op (p_combined, combined);
      assert_pipeline_matches (p_combined, "ClutterColorPipeline: matrix_4x4");
      assert_transform_matches (p_combined, input, expected, G_N_ELEMENTS (input) / 4);
    }
  }

  /* Test: matrix (swap R and B) followed by multiply */
  {
    g_autoptr (ClutterColorPipeline) p2 = NULL;
    g_autoptr (ClutterColorOp) mat_op2 = NULL;
    g_autoptr (ClutterColorOp) mul_op2 = NULL;
    g_autoptr (ClutterColorOp) combined2 = NULL;

    p2 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);

    matrix = graphene_matrix_alloc ();
    graphene_matrix_init_from_float (matrix, (float[16]) {
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
    });
    clutter_color_pipeline_take_op (p2, clutter_color_op_matrix_4x4_new (matrix));
    clutter_color_pipeline_take_op (p2, clutter_color_op_multiply_new (2.0f));

    assert_pipeline_matches (p2, "ClutterColorPipeline: matrix_4x4 -> multiply(2.00)");
    assert_transform_matches (p2, input, expected, G_N_ELEMENTS (input) / 4);

    /* Test try_combine: matrix + multiply should combine into single matrix */
    matrix = graphene_matrix_alloc ();
    graphene_matrix_init_from_float (matrix, (float[16]) {
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
    });
    mat_op2 = clutter_color_op_matrix_4x4_new (matrix);
    mul_op2 = clutter_color_op_multiply_new (2.0f);
    combined2 = clutter_color_op_try_combine (mat_op2, mul_op2);
    g_assert_nonnull (combined2);
    g_assert_true (CLUTTER_IS_COLOR_OP_MATRIX_4X4 (combined2));

    {
      g_autoptr (ClutterColorPipeline) p_combined = NULL;

      p_combined = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
      clutter_color_pipeline_add_op (p_combined, combined2);
      assert_pipeline_matches (p_combined, "ClutterColorPipeline: matrix_4x4");
      assert_transform_matches (p_combined, input, expected, G_N_ELEMENTS (input) / 4);
    }
  }
}

static void
color_pipeline_clamp_merge (void)
{
  g_autoptr (ClutterColorPipeline) p1 = NULL;
  g_autoptr (ClutterColorPipeline) p2 = NULL;
  float *v;

  p1 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);

  /* Add clamp followed by curve_1d (which clamps input) */
  clutter_color_pipeline_take_op (p1, clutter_color_op_clamp_unit_new ());

  v = g_new0 (float, 2);
  v[0] = 0.0f;
  v[1] = 1.0f;
  clutter_color_pipeline_take_op (p1, clutter_color_op_curve_1d_new_rgb (2, v));

  /* Add clamp after curve_1d (which clamps output) */
  clutter_color_pipeline_take_op (p1, clutter_color_op_clamp_unit_new ());

  assert_pipeline_matches (p1, "ClutterColorPipeline: clamp_unit -> curve_1d -> clamp_unit");

  /* Simplify should remove both redundant clamp_unit operations */
  clutter_color_pipeline_simplify (p1);
  assert_pipeline_matches (p1, "ClutterColorPipeline: curve_1d");

  /* Test case where clamp should NOT be removed */
  p2 = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);

  /* multiply doesn't clamp output, srgb_piecewise_eotf doesn't clamp input */
  clutter_color_pipeline_take_op (p2, clutter_color_op_multiply_new (2.0f));
  clutter_color_pipeline_take_op (p2, clutter_color_op_clamp_unit_new ());
  clutter_color_pipeline_take_op (p2, clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE));

  assert_pipeline_matches (p2, "ClutterColorPipeline: multiply(2.00) -> clamp_unit -> srgb_piecewise_eotf");

  /* Simplify should keep the clamp since it's needed */
  clutter_color_pipeline_simplify (p2);
  assert_pipeline_matches (p2, "ClutterColorPipeline: multiply(2.00) -> clamp_unit -> srgb_piecewise_eotf");
}

static void
assert_gpu_cpu_matches (ClutterColorPipeline *pipeline,
                        float                *input,
                        size_t                n_pixels)
{
  ClutterBackend *backend;
  CoglContext *cogl_context;
  g_autoptr (CoglPipeline) cogl_pipeline = NULL;
  g_autoptr (CoglOffscreen) offscreen = NULL;
  g_autoptr (CoglTexture) texture = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree float *cpu_result = NULL;
  g_autofree float *gpu_result = NULL;
  CoglFramebuffer *framebuffer;

  backend = clutter_test_get_backend ();
  cogl_context = clutter_backend_get_cogl_context (backend);
  cpu_result = g_memdup2 (input, sizeof (*input) * n_pixels * 4);
  /* CPU path now handles premultiplication automatically, matching GPU path */
  clutter_color_pipeline_do_transform (pipeline, cpu_result, n_pixels);

  texture = cogl_texture_2d_new_with_format (cogl_context,
                                             n_pixels, 1,
                                             COGL_PIXEL_FORMAT_RGBA_FP_32323232);
  offscreen = cogl_offscreen_new_with_texture (texture);
  framebuffer = COGL_FRAMEBUFFER (offscreen);

  if (!cogl_framebuffer_allocate (framebuffer, &error))
    g_error ("Failed to allocate framebuffer: %s", error->message);

  cogl_framebuffer_orthographic (framebuffer, 0, 0, n_pixels, 1, -1, 1);
  cogl_framebuffer_clear4f (framebuffer, COGL_BUFFER_BIT_COLOR, 0.0f, 0.0f, 0.0f, 0.0f);

  cogl_pipeline = cogl_pipeline_new (cogl_context);
  clutter_color_pipeline_shader_add_transform (pipeline, cogl_pipeline);

  for (size_t i = 0; i < n_pixels; i++)
    {
      CoglColor color;

      cogl_color_init_from_4f (&color,
                               input[i * 4 + 0],
                               input[i * 4 + 1],
                               input[i * 4 + 2],
                               input[i * 4 + 3]);
      cogl_pipeline_set_color (cogl_pipeline, &color);

      cogl_framebuffer_draw_rectangle (framebuffer,
                                       cogl_pipeline,
                                       i, 0,
                                       i + 1, 1);
    }

  cogl_framebuffer_finish (framebuffer);

  gpu_result = g_malloc (n_pixels * 4 * sizeof (float));
  cogl_framebuffer_read_pixels (framebuffer,
                                0, 0,
                                n_pixels, 1,
                                COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE,
                                (uint8_t *) gpu_result);

  for (size_t i = 0; i < n_pixels * 4; i++)
    g_assert_cmpfloat_with_epsilon (cpu_result[i], gpu_result[i], 0.005f);
}

static void
color_pipeline_shader_gpu_vs_cpu (void)
{
  g_autoptr (ClutterColorPipeline) pipeline = NULL;
  graphene_matrix_t *matrix;
  float input[] = {
    0.0f, 0.1f, 0.2f, 1.0f,
    0.3f, 0.4f, 0.5f, 1.0f,
    0.6f, 0.7f, 0.8f, 1.0f,
  };

  /* Test with curve_1d operation */
  pipeline = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);

  {
    float *v = g_new0 (float, 5);
    v[0] = 0.0f;
    v[1] = 0.25f;
    v[2] = 0.5f;
    v[3] = 0.75f;
    v[4] = 1.0f;
    clutter_color_pipeline_take_op (pipeline, clutter_color_op_curve_1d_new_rgb (5, v));
  }
  assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);

  /* Add gamma and multiply operations */
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_gamma_power_new (2.2f));
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_multiply_new (0.5f));
  assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);

  /* Add another multiply operation */
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_multiply_new (1.5f));
  assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);

  /* Add a matrix operation */
  matrix = graphene_matrix_alloc ();
  graphene_matrix_init_from_float (matrix, (float[16]) {
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_matrix_4x4_new (matrix));
  assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);

  /* Add sRGB EOTF */
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE));
  assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);

  /* Add sRGB inverse EOTF */
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_srgb_piecewise_inv_eotf_new (/* unit_range_only = */ TRUE));
  assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);

  /* Add clamp operation */
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_clamp_unit_new ());
  assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);

  /* Add PQ EOTF */
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_pq_eotf_new ());
  assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);

  /* Add PQ inverse EOTF */
  clutter_color_pipeline_take_op (pipeline, clutter_color_op_pq_inv_eotf_new ());
  assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);

  /* Add 3D LUT operation */
  {
    size_t lut_size = 3;
    g_autofree float *lut_data = g_new (float, lut_size * lut_size * lut_size * 3);

    /* Simple identity LUT */
    for (size_t b = 0; b < lut_size; b++)
      for (size_t g = 0; g < lut_size; g++)
        for (size_t r = 0; r < lut_size; r++)
          {
            size_t idx = (b * lut_size * lut_size + g * lut_size + r) * 3;
            lut_data[idx + 0] = (float) r / (lut_size - 1);
            lut_data[idx + 1] = (float) g / (lut_size - 1);
            lut_data[idx + 2] = (float) b / (lut_size - 1);
          }

    clutter_color_pipeline_take_op (pipeline, clutter_color_op_3d_lut_new (lut_size, lut_data));
    assert_gpu_cpu_matches (pipeline, input, G_N_ELEMENTS (input) / 4);
  }
}

static void
color_pipeline_premultiplication (void)
{
  /* Test with premultiplication (unpremultiply + op + premultiply) */
  {
    g_autoptr (ClutterColorPipeline) pipeline = NULL;
    float input_premult[] = {
      /* Premultiplied RGBA: (R*A, G*A, B*A, A) */
      0.0f, 0.0f, 0.0f, 1.0f,     /* Black, opaque */
      0.4f, 0.25f, 0.15f, 0.5f,   /* (0.8, 0.5, 0.3) * 0.5 */
      0.08f, 0.08f, 0.08f, 0.1f,  /* (0.8, 0.8, 0.8) * 0.1 */
    };
    float expected_premult[] = {
      /* After sRGB EOTF, should be (EOTF(R), EOTF(G), EOTF(B)) * A */
      0.0f, 0.0f, 0.0f, 1.0f,
      /* EOTF(0.8) ≈ 0.604, EOTF(0.5) ≈ 0.214, EOTF(0.3) ≈ 0.073 */
      0.302f, 0.107f, 0.0365f, 0.5f,  /* (0.604, 0.214, 0.073) * 0.5 */
      /* EOTF(0.8) ≈ 0.604 */
      0.0604f, 0.0604f, 0.0604f, 0.1f,  /* (0.604, 0.604, 0.604) * 0.1 */
    };

    g_autofree float *result = NULL;

    pipeline = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_take_op (pipeline, clutter_color_op_unpremultiply_new ());
    clutter_color_pipeline_take_op (pipeline, clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE));
    clutter_color_pipeline_take_op (pipeline, clutter_color_op_premultiply_new ());

    result = g_memdup2 (input_premult, sizeof (input_premult));
    clutter_color_pipeline_do_transform (pipeline, result,
                                         G_N_ELEMENTS (input_premult) / 4);

    for (size_t i = 0; i < G_N_ELEMENTS (input_premult); i++)
      g_assert_cmpfloat_with_epsilon (result[i], expected_premult[i], 0.003);
  }

  /* Test without premultiplication - input is unpremultiplied */
  {
    g_autoptr (ClutterColorPipeline) pipeline = NULL;
    float input_unpremult[] = {
      0.0f, 0.0f, 0.0f, 1.0f,
      0.8f, 0.5f, 0.3f, 0.5f,   /* Unpremultiplied RGB with alpha */
      0.8f, 0.8f, 0.8f, 0.1f,
    };
    float expected_unpremult[] = {
      /* After sRGB EOTF, RGB transformed but alpha unchanged */
      0.0f, 0.0f, 0.0f, 1.0f,
      0.604f, 0.214f, 0.073f, 0.5f,
      0.604f, 0.604f, 0.604f, 0.1f,
    };

    g_autofree float *result = NULL;

    pipeline = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);
    clutter_color_pipeline_take_op (pipeline, clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE));

    result = g_memdup2 (input_unpremult, sizeof (input_unpremult));
    clutter_color_pipeline_do_transform (pipeline, result,
                                         G_N_ELEMENTS (input_unpremult) / 4);

    for (size_t i = 0; i < G_N_ELEMENTS (input_unpremult); i++)
      g_assert_cmpfloat_with_epsilon (result[i], expected_unpremult[i], 0.003);
  }
}

static void
color_pipeline_temperature_compose (void)
{
  g_autoptr (ClutterColorPipeline) composed = NULL;
  graphene_matrix_t *m_to, *m_from, *m_temp;
  float input[] = { 0.5f, 0.5f, 0.5f, 1.0f };

  composed = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);

  /* Simulate compose_post_blending_color_pipeline with color temperature:
   * to_linear: srgb_piecewise_eotf -> matrix(sRGB->bt2020) -> matrix(bt2020->sRGB)
   * effect: matrix(temperature_scale)
   * from_linear: matrix(sRGB->bt2020) -> matrix(bt2020->sRGB) -> srgb_piecewise_inv_eotf
   * transform: empty (sRGB->sRGB)
   *
   * For simplicity, use identity for gamut matrices since sRGB->bt2020->sRGB = I
   */

  /* to_linear */
  clutter_color_pipeline_take_op (composed, clutter_color_op_srgb_piecewise_eotf_new (/* unit_range_only = */ TRUE));
  m_to = graphene_matrix_alloc ();
  graphene_matrix_init_identity (m_to);
  clutter_color_pipeline_take_op (composed, clutter_color_op_matrix_4x4_new (m_to));

  /* effect: temperature scale (warm = reduce blue, boost red) */
  m_temp = graphene_matrix_alloc ();
  graphene_matrix_init_scale (m_temp, 1.2f, 1.0f, 0.7f);
  clutter_color_pipeline_take_op (composed, clutter_color_op_matrix_4x4_new (m_temp));

  /* from_linear */
  m_from = graphene_matrix_alloc ();
  graphene_matrix_init_identity (m_from);
  clutter_color_pipeline_take_op (composed, clutter_color_op_matrix_4x4_new (m_from));
  clutter_color_pipeline_take_op (composed, clutter_color_op_srgb_piecewise_inv_eotf_new (/* unit_range_only = */ TRUE));

  assert_pipeline_matches (composed,
    "ClutterColorPipeline: srgb_piecewise_eotf -> matrix_4x4 -> matrix_4x4 -> matrix_4x4 -> srgb_piecewise_inv_eotf");

  /* After simplify + combine: matrices should merge into one */
  clutter_color_pipeline_simplify (composed);
  clutter_color_pipeline_combine (composed);
  assert_pipeline_matches (composed,
    "ClutterColorPipeline: srgb_piecewise_eotf -> matrix_4x4 -> srgb_piecewise_inv_eotf");

  /* The combined matrix should be the temperature scale (identity * temp * identity = temp).
   * Applying to 0.5,0.5,0.5: linearize, scale (1.2, 1.0, 0.7), delinearize.
   * srgb_piecewise_eotf(0.5) ≈ 0.214, scale → (0.257, 0.214, 0.150), srgb_inv(0.257) ≈ 0.553 */
  {
    g_autofree float *result = g_memdup2 (input, sizeof (input));
    clutter_color_pipeline_do_transform (composed, result, 1);

    /* Red should increase, blue should decrease */
    g_assert_cmpfloat (result[0], >, input[0]);
    g_assert_cmpfloat (result[2], <, input[2]);
  }

  /* GPU should match CPU */
  assert_gpu_cpu_matches (composed, input, 1);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/color-pipeline/shader-gpu-vs-cpu", color_pipeline_shader_gpu_vs_cpu)
  CLUTTER_TEST_UNIT ("/color-pipeline/temperature-compose", color_pipeline_temperature_compose)
  CLUTTER_TEST_UNIT ("/color-pipeline/basic", color_pipeline_basic)
  CLUTTER_TEST_UNIT ("/color-pipeline/eval-srgb", color_pipeline_eval_srgb)
  CLUTTER_TEST_UNIT ("/color-pipeline/eval-gamma", color_pipeline_eval_gamma)
  CLUTTER_TEST_UNIT ("/color-pipeline/eval-pq", color_pipeline_eval_pq)
  CLUTTER_TEST_UNIT ("/color-pipeline/3d-lut", color_pipeline_3d_lut)
  CLUTTER_TEST_UNIT ("/color-pipeline/matrix-swap-channels", color_pipeline_matrix_swap_channels)
  CLUTTER_TEST_UNIT ("/color-pipeline/matrix-merge", color_pipeline_matrix_merge)
  CLUTTER_TEST_UNIT ("/color-pipeline/multiply-basic", color_pipeline_multiply_basic)
  CLUTTER_TEST_UNIT ("/color-pipeline/multiply-merge", color_pipeline_multiply_merge)
  CLUTTER_TEST_UNIT ("/color-pipeline/multiply-matrix-merge", color_pipeline_multiply_matrix_merge)
  CLUTTER_TEST_UNIT ("/color-pipeline/clamp-merge", color_pipeline_clamp_merge)
  CLUTTER_TEST_UNIT ("/color-pipeline/premultiplication", color_pipeline_premultiplication)
)
