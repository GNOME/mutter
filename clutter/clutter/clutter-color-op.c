/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "clutter-color-op.h"

#include <math.h>

static float
lut_lookup_interpolate (float   sample,
                        size_t  lut_size,
                        float  *lut)
{
  float spos, floor_val, ceil_val, w;

  sample = (float) CLAMP (sample, .0, 1.0);
  spos = sample * (lut_size - 1);
  floor_val = floorf (spos);
  ceil_val = ceilf (spos);
  w = spos - floor_val;

  return (lut[(size_t) floor_val] * (1.0f - w)) + (lut[(size_t) ceil_val] * w);
}

static void
do_matrix_transform (const graphene_matrix_t *matrix,
                     float                   *data,
                     size_t                   n_samples)
{
  graphene_vec4_t input, output;

  for (size_t i = 0; i < n_samples; i++)
    {
      graphene_vec4_init (&input, data[0], data[1], data[2], data[3]);
      graphene_matrix_transform_vec4 (matrix, &input, &output);

      data[0] = graphene_vec4_get_x (&output);
      data[1] = graphene_vec4_get_y (&output);
      data[2] = graphene_vec4_get_z (&output);
      data[3] = graphene_vec4_get_w (&output);

      data += 4;
    }
}

G_DEFINE_ABSTRACT_TYPE (ClutterColorOp,
                        clutter_color_op,
                        G_TYPE_OBJECT)

static char *
clutter_color_op_real_to_string (ClutterColorOp *op)
{
  return g_strdup (G_OBJECT_TYPE_NAME (op));
}

static void
clutter_color_op_real_do_transform (ClutterColorOp *op,
                                    float          *data,
                                    size_t          n_samples)
{
  ClutterColorOpClass *klass = CLUTTER_COLOR_OP_GET_CLASS (op);
  gboolean transforms_alpha;

  g_return_if_fail (klass->do_transform_one != NULL);

  transforms_alpha = clutter_color_op_get_transforms_alpha (op);

  for (size_t i = 0; i < n_samples; i++)
    {
      data[0] = klass->do_transform_one (op, data[0]);
      data[1] = klass->do_transform_one (op, data[1]);
      data[2] = klass->do_transform_one (op, data[2]);

      if (transforms_alpha)
        data[3] = klass->do_transform_one (op, data[3]);

      data += 4;
    }
}

static float
clutter_color_op_real_do_transform_one (ClutterColorOp *op,
                                        float           input)
{
  ClutterColorOpClass *klass = CLUTTER_COLOR_OP_GET_CLASS (op);
  float data[4];

  g_return_val_if_fail (klass->do_transform != NULL, input);

  data[0] = data[1] = data[2] = data[3] = input;
  klass->do_transform (op, data, 1);

  return data[0];
}

static gboolean
clutter_color_op_real_get_transforms_alpha (ClutterColorOp *op)
{
  return FALSE;
}

static gboolean
clutter_color_op_real_get_clamps_input (ClutterColorOp *op)
{
  return FALSE;
}

static gboolean
clutter_color_op_real_get_clamps_output (ClutterColorOp *op)
{
  return FALSE;
}

static void
clutter_color_op_class_init (ClutterColorOpClass *klass)
{
  klass->to_string = clutter_color_op_real_to_string;
  klass->do_transform = clutter_color_op_real_do_transform;
  klass->do_transform_one = clutter_color_op_real_do_transform_one;
  klass->get_transforms_alpha = clutter_color_op_real_get_transforms_alpha;
  klass->get_clamps_input = clutter_color_op_real_get_clamps_input;
  klass->get_clamps_output = clutter_color_op_real_get_clamps_output;
}

static void
clutter_color_op_init (ClutterColorOp *op)
{
}

char *
clutter_color_op_to_string (ClutterColorOp *op)
{
  ClutterColorOpClass *klass = CLUTTER_COLOR_OP_GET_CLASS (op);

  return klass->to_string (op);
}

void
clutter_color_op_do_transform (ClutterColorOp *op,
                               float          *data,
                               size_t          n_samples)
{
  ClutterColorOpClass *klass = CLUTTER_COLOR_OP_GET_CLASS (op);

  klass->do_transform (op, data, n_samples);
}

float
clutter_color_op_do_transform_one (ClutterColorOp *op,
                                   float           input)
{
  ClutterColorOpClass *klass = CLUTTER_COLOR_OP_GET_CLASS (op);

  return klass->do_transform_one (op, input);
}

gboolean
clutter_color_op_get_transforms_alpha (ClutterColorOp *op)
{
  ClutterColorOpClass *klass = CLUTTER_COLOR_OP_GET_CLASS (op);

  return klass->get_transforms_alpha (op);
}

gboolean
clutter_color_op_get_clamps_input (ClutterColorOp *op)
{
  ClutterColorOpClass *klass = CLUTTER_COLOR_OP_GET_CLASS (op);

  return klass->get_clamps_input (op);
}

gboolean
clutter_color_op_get_clamps_output (ClutterColorOp *op)
{
  ClutterColorOpClass *klass = CLUTTER_COLOR_OP_GET_CLASS (op);

  return klass->get_clamps_output (op);
}

static ClutterColorOp *
lower_to_3d_lut (ClutterColorOp *op,
                 uint32_t        size)
{
  g_autofree float *data = NULL;
  size_t n;

  n = (size_t) size * size * size;
  data = g_new (float, n * 3);

  for (size_t b = 0; b < size; b++)
    for (size_t g = 0; g < size; g++)
      for (size_t r = 0; r < size; r++)
        {
          float sample[4];
          size_t idx;

          idx = (b * size * size + g * size + r) * 3;
          sample[0] = (float) r / (size - 1);
          sample[1] = (float) g / (size - 1);
          sample[2] = (float) b / (size - 1);
          sample[3] = 1.0f;

          clutter_color_op_do_transform (op, sample, 1);
          data[idx + 0] = sample[0];
          data[idx + 1] = sample[1];
          data[idx + 2] = sample[2];
        }

  return clutter_color_op_3d_lut_new (size, data);
}

static ClutterColorOp *
lower_to_curve_uniform (ClutterColorOp *op,
                        size_t          size)
{
  float *v;

  v = g_new (float, size);

  for (size_t i = 0; i < size; i++)
    {
      float sample = (float) i / (float) (size - 1);
      v[i] = clutter_color_op_do_transform_one (op, sample);
    }

  return clutter_color_op_curve_1d_new_rgb (size, v);
}

struct _ClutterColorOpClampUnit
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE (ClutterColorOpClampUnit, clutter_color_op_clamp_unit,
               CLUTTER_TYPE_COLOR_OP)

static float
clutter_color_op_clamp_unit_do_transform_one (ClutterColorOp *op,
                                              float           input)
{
  return (float) CLAMP (input, 0.f, 1.f);
}

static gboolean
clutter_color_op_clamp_unit_get_transforms_alpha (ClutterColorOp *op)
{
  return TRUE;
}

static gboolean
clutter_color_op_clamp_unit_get_clamps_input (ClutterColorOp *op)
{
  return TRUE;
}

static gboolean
clutter_color_op_clamp_unit_get_clamps_output (ClutterColorOp *op)
{
  return TRUE;
}

static char *
clutter_color_op_clamp_unit_to_string (ClutterColorOp *op)
{
  return g_strdup ("clamp_unit");
}

static void
clutter_color_op_clamp_unit_class_init (ClutterColorOpClampUnitClass *klass)
{
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  op_class->to_string = clutter_color_op_clamp_unit_to_string;
  op_class->do_transform_one = clutter_color_op_clamp_unit_do_transform_one;
  op_class->get_transforms_alpha = clutter_color_op_clamp_unit_get_transforms_alpha;
  op_class->get_clamps_input = clutter_color_op_clamp_unit_get_clamps_input;
  op_class->get_clamps_output = clutter_color_op_clamp_unit_get_clamps_output;
}

static void
clutter_color_op_clamp_unit_init (ClutterColorOpClampUnit *op)
{
}

ClutterColorOp *
clutter_color_op_clamp_unit_new (void)
{
  return g_object_new (CLUTTER_TYPE_COLOR_OP_CLAMP_UNIT, NULL);
}

typedef struct _ClutterColorOpSrgbPiecewiseEotfPrivate
{
  gboolean unit_range_only;
} ClutterColorOpSrgbPiecewiseEotfPrivate;

struct _ClutterColorOpSrgbPiecewiseEotf
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorOpSrgbPiecewiseEotf,
                            clutter_color_op_srgb_piecewise_eotf,
                            CLUTTER_TYPE_COLOR_OP)

static float
clutter_color_op_srgb_piecewise_eotf_do_transform_one (ClutterColorOp *op,
                                                       float           input)
{
  float v;

  v = fabsf (input);

  if (v <= 0.04045f)
    v = v / 12.92f;
  else
    v = powf ((v + 0.055f) / 1.055f, 12.0f / 5.0f);

  return input < 0.f ? -v : v;
}

static char *
clutter_color_op_srgb_piecewise_eotf_to_string (ClutterColorOp *op)
{
  return g_strdup ("srgb_piecewise_eotf");
}

static void
clutter_color_op_srgb_piecewise_eotf_class_init (ClutterColorOpSrgbPiecewiseEotfClass *klass)
{
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  op_class->to_string = clutter_color_op_srgb_piecewise_eotf_to_string;
  op_class->do_transform_one = clutter_color_op_srgb_piecewise_eotf_do_transform_one;
}

static void
clutter_color_op_srgb_piecewise_eotf_init (ClutterColorOpSrgbPiecewiseEotf *op)
{
}

ClutterColorOp *
clutter_color_op_srgb_piecewise_eotf_new (gboolean unit_range_only)
{
  g_autoptr (ClutterColorOpSrgbPiecewiseEotf) op = NULL;
  ClutterColorOpSrgbPiecewiseEotfPrivate *priv;

  op = g_object_new (CLUTTER_TYPE_COLOR_OP_SRGB_PIECEWISE_EOTF, NULL);
  priv = clutter_color_op_srgb_piecewise_eotf_get_instance_private (op);
  priv->unit_range_only = unit_range_only;

  return CLUTTER_COLOR_OP (g_steal_pointer (&op));
}

typedef struct _ClutterColorOpSrgbPiecewiseInvEotfPrivate
{
  gboolean unit_range_only;
} ClutterColorOpSrgbPiecewiseInvEotfPrivate;

struct _ClutterColorOpSrgbPiecewiseInvEotf
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorOpSrgbPiecewiseInvEotf,
                            clutter_color_op_srgb_piecewise_inv_eotf,
                            CLUTTER_TYPE_COLOR_OP)

static float
clutter_color_op_srgb_piecewise_inv_eotf_do_transform_one (ClutterColorOp *op,
                                                           float           input)
{
  float v;

  v = fabsf (input);

  if (v <= 0.0031308f)
    v = v * 12.92f;
  else
    v = powf (v, (5.0f / 12.0f)) * 1.055f - 0.055f;

  return input < 0.f ? -v : v;
}

static char *
clutter_color_op_srgb_piecewise_inv_eotf_to_string (ClutterColorOp *op)
{
  return g_strdup ("srgb_piecewise_inv_eotf");
}

static void
clutter_color_op_srgb_piecewise_inv_eotf_class_init (ClutterColorOpSrgbPiecewiseInvEotfClass *klass)
{
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  op_class->to_string = clutter_color_op_srgb_piecewise_inv_eotf_to_string;
  op_class->do_transform_one = clutter_color_op_srgb_piecewise_inv_eotf_do_transform_one;
}

static void
clutter_color_op_srgb_piecewise_inv_eotf_init (ClutterColorOpSrgbPiecewiseInvEotf *op)
{
}

ClutterColorOp *
clutter_color_op_srgb_piecewise_inv_eotf_new (gboolean unit_range_only)
{
  g_autoptr (ClutterColorOpSrgbPiecewiseInvEotf) op = NULL;
  ClutterColorOpSrgbPiecewiseInvEotfPrivate *priv;

  op = g_object_new (CLUTTER_TYPE_COLOR_OP_SRGB_PIECEWISE_INV_EOTF, NULL);
  priv = clutter_color_op_srgb_piecewise_inv_eotf_get_instance_private (op);
  priv->unit_range_only = unit_range_only;

  return CLUTTER_COLOR_OP (g_steal_pointer (&op));
}

struct _ClutterColorOpPqEotf
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE (ClutterColorOpPqEotf, clutter_color_op_pq_eotf,
               CLUTTER_TYPE_COLOR_OP)

static float
clutter_color_op_pq_eotf_do_transform_one (ClutterColorOp *op,
                                           float           input)
{
  float c1, c2, c3, oo_m1, oo_m2, num, den;

  c1 = 0.8359375f;
  c2 = 18.8515625f;
  c3 = 18.6875f;
  oo_m1 = 1.0f / 0.1593017f;
  oo_m2 = 1.0f / 78.84375f;
  input = CLAMP (input, 0.0f, 1.0f);
  num = MAX (powf (input, oo_m2) - c1, 0.0f);
  den = c2 - c3 * powf (input, oo_m2);
  return powf (num / den, oo_m1);
}

static char *
clutter_color_op_pq_eotf_to_string (ClutterColorOp *op)
{
  return g_strdup ("pq_eotf");
}

static void
clutter_color_op_pq_eotf_class_init (ClutterColorOpPqEotfClass *klass)
{
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  op_class->to_string = clutter_color_op_pq_eotf_to_string;
  op_class->do_transform_one = clutter_color_op_pq_eotf_do_transform_one;
}

static void
clutter_color_op_pq_eotf_init (ClutterColorOpPqEotf *op)
{
}

ClutterColorOp *
clutter_color_op_pq_eotf_new (void)
{
  return g_object_new (CLUTTER_TYPE_COLOR_OP_PQ_EOTF, NULL);
}

struct _ClutterColorOpPqInvEotf
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE (ClutterColorOpPqInvEotf, clutter_color_op_pq_inv_eotf,
               CLUTTER_TYPE_COLOR_OP)

static float
clutter_color_op_pq_inv_eotf_do_transform_one (ClutterColorOp *op,
                                               float           input)
{
  float c1, c2, c3, m1, m2, in_pow_m1, num, den;

  c1 = 0.8359375f;
  c2 = 18.8515625f;
  c3 = 18.6875f;
  m1 = 0.1593017f;
  m2 = 78.84375f;
  input = CLAMP (input, 0.0f, 1.0f);
  in_pow_m1 = powf (input, m1);
  num = c1 + c2 * in_pow_m1;
  den = 1.0f + c3 * in_pow_m1;
  return powf (num / den, m2);
}

static char *
clutter_color_op_pq_inv_eotf_to_string (ClutterColorOp *op)
{
  return g_strdup ("pq_inv_eotf");
}

static void
clutter_color_op_pq_inv_eotf_class_init (ClutterColorOpPqInvEotfClass *klass)
{
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  op_class->to_string = clutter_color_op_pq_inv_eotf_to_string;
  op_class->do_transform_one = clutter_color_op_pq_inv_eotf_do_transform_one;
}

static void
clutter_color_op_pq_inv_eotf_init (ClutterColorOpPqInvEotf *op)
{
}

ClutterColorOp *
clutter_color_op_pq_inv_eotf_new (void)
{
  return g_object_new (CLUTTER_TYPE_COLOR_OP_PQ_INV_EOTF, NULL);
}

typedef struct _ClutterColorOp3DLutPrivate
{
  uint32_t size;
  float *data;
} ClutterColorOp3DLutPrivate;

struct _ClutterColorOp3DLut
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorOp3DLut,
                            clutter_color_op_3d_lut,
                            CLUTTER_TYPE_COLOR_OP)

static void
clutter_color_op_3d_lut_sample (ClutterColorOp3DLutPrivate *priv,
                                float                       r,
                                float                       g,
                                float                       b,
                                float                      *out_r,
                                float                      *out_g,
                                float                      *out_b)
{
  float scaled_r, scaled_g, scaled_b;
  int r_low, r_high, g_low, g_high, b_low, b_high;
  float t_r, t_g, t_b;
  float v000_r, v000_g, v000_b;
  float v111_r, v111_g, v111_b;
  int size = priv->size;
  float *lut = priv->data;

  r = CLAMP (r, 0.0f, 1.0f);
  g = CLAMP (g, 0.0f, 1.0f);
  b = CLAMP (b, 0.0f, 1.0f);

  scaled_r = r * (size - 1);
  scaled_g = g * (size - 1);
  scaled_b = b * (size - 1);

  r_low = (int) floorf (scaled_r);
  g_low = (int) floorf (scaled_g);
  b_low = (int) floorf (scaled_b);

  r_high = MIN (r_low + 1, size - 1);
  g_high = MIN (g_low + 1, size - 1);
  b_high = MIN (b_low + 1, size - 1);

  t_r = scaled_r - r_low;
  t_g = scaled_g - g_low;
  t_b = scaled_b - b_low;

  /* Tetrahedral interpolation - matches GPU implementation */
#define LUT_INDEX(r, g, b, c) (((b) * size * size + (g) * size + (r)) * 3 + (c))
#define SAMPLE(ri, gi, bi, c) lut[LUT_INDEX (ri, gi, bi, c)]

  v000_r = SAMPLE (r_low, g_low, b_low, 0);
  v000_g = SAMPLE (r_low, g_low, b_low, 1);
  v000_b = SAMPLE (r_low, g_low, b_low, 2);

  v111_r = SAMPLE (r_high, g_high, b_high, 0);
  v111_g = SAMPLE (r_high, g_high, b_high, 1);
  v111_b = SAMPLE (r_high, g_high, b_high, 2);

  if (t_r > t_g)
    {
      if (t_g > t_b)
        {
          float v100_r = SAMPLE (r_high, g_low, b_low, 0);
          float v100_g = SAMPLE (r_high, g_low, b_low, 1);
          float v100_b = SAMPLE (r_high, g_low, b_low, 2);
          float v110_r = SAMPLE (r_high, g_high, b_low, 0);
          float v110_g = SAMPLE (r_high, g_high, b_low, 1);
          float v110_b = SAMPLE (r_high, g_high, b_low, 2);

          *out_r = v000_r + t_r * (v100_r - v000_r) + t_g * (v110_r - v100_r) + t_b * (v111_r - v110_r);
          *out_g = v000_g + t_r * (v100_g - v000_g) + t_g * (v110_g - v100_g) + t_b * (v111_g - v110_g);
          *out_b = v000_b + t_r * (v100_b - v000_b) + t_g * (v110_b - v100_b) + t_b * (v111_b - v110_b);
        }
      else if (t_r > t_b)
        {
          float v100_r = SAMPLE (r_high, g_low, b_low, 0);
          float v100_g = SAMPLE (r_high, g_low, b_low, 1);
          float v100_b = SAMPLE (r_high, g_low, b_low, 2);
          float v101_r = SAMPLE (r_high, g_low, b_high, 0);
          float v101_g = SAMPLE (r_high, g_low, b_high, 1);
          float v101_b = SAMPLE (r_high, g_low, b_high, 2);

          *out_r = v000_r + t_r * (v100_r - v000_r) + t_g * (v111_r - v101_r) + t_b * (v101_r - v100_r);
          *out_g = v000_g + t_r * (v100_g - v000_g) + t_g * (v111_g - v101_g) + t_b * (v101_g - v100_g);
          *out_b = v000_b + t_r * (v100_b - v000_b) + t_g * (v111_b - v101_b) + t_b * (v101_b - v100_b);
        }
      else
        {
          float v001_r = SAMPLE (r_low, g_low, b_high, 0);
          float v001_g = SAMPLE (r_low, g_low, b_high, 1);
          float v001_b = SAMPLE (r_low, g_low, b_high, 2);
          float v101_r = SAMPLE (r_high, g_low, b_high, 0);
          float v101_g = SAMPLE (r_high, g_low, b_high, 1);
          float v101_b = SAMPLE (r_high, g_low, b_high, 2);

          *out_r = v000_r + t_r * (v101_r - v001_r) + t_g * (v111_r - v101_r) + t_b * (v001_r - v000_r);
          *out_g = v000_g + t_r * (v101_g - v001_g) + t_g * (v111_g - v101_g) + t_b * (v001_g - v000_g);
          *out_b = v000_b + t_r * (v101_b - v001_b) + t_g * (v111_b - v101_b) + t_b * (v001_b - v000_b);
        }
    }
  else
    {
      if (t_b > t_g)
        {
          float v001_r = SAMPLE (r_low, g_low, b_high, 0);
          float v001_g = SAMPLE (r_low, g_low, b_high, 1);
          float v001_b = SAMPLE (r_low, g_low, b_high, 2);
          float v011_r = SAMPLE (r_low, g_high, b_high, 0);
          float v011_g = SAMPLE (r_low, g_high, b_high, 1);
          float v011_b = SAMPLE (r_low, g_high, b_high, 2);

          *out_r = v000_r + t_r * (v111_r - v011_r) + t_g * (v011_r - v001_r) + t_b * (v001_r - v000_r);
          *out_g = v000_g + t_r * (v111_g - v011_g) + t_g * (v011_g - v001_g) + t_b * (v001_g - v000_g);
          *out_b = v000_b + t_r * (v111_b - v011_b) + t_g * (v011_b - v001_b) + t_b * (v001_b - v000_b);
        }
      else if (t_b > t_r)
        {
          float v010_r = SAMPLE (r_low, g_high, b_low, 0);
          float v010_g = SAMPLE (r_low, g_high, b_low, 1);
          float v010_b = SAMPLE (r_low, g_high, b_low, 2);
          float v011_r = SAMPLE (r_low, g_high, b_high, 0);
          float v011_g = SAMPLE (r_low, g_high, b_high, 1);
          float v011_b = SAMPLE (r_low, g_high, b_high, 2);

          *out_r = v000_r + t_r * (v111_r - v011_r) + t_g * (v010_r - v000_r) + t_b * (v011_r - v010_r);
          *out_g = v000_g + t_r * (v111_g - v011_g) + t_g * (v010_g - v000_g) + t_b * (v011_g - v010_g);
          *out_b = v000_b + t_r * (v111_b - v011_b) + t_g * (v010_b - v000_b) + t_b * (v011_b - v010_b);
        }
      else
        {
          float v010_r = SAMPLE (r_low, g_high, b_low, 0);
          float v010_g = SAMPLE (r_low, g_high, b_low, 1);
          float v010_b = SAMPLE (r_low, g_high, b_low, 2);
          float v110_r = SAMPLE (r_high, g_high, b_low, 0);
          float v110_g = SAMPLE (r_high, g_high, b_low, 1);
          float v110_b = SAMPLE (r_high, g_high, b_low, 2);

          *out_r = v000_r + t_r * (v110_r - v010_r) + t_g * (v010_r - v000_r) + t_b * (v111_r - v110_r);
          *out_g = v000_g + t_r * (v110_g - v010_g) + t_g * (v010_g - v000_g) + t_b * (v111_g - v110_g);
          *out_b = v000_b + t_r * (v110_b - v010_b) + t_g * (v010_b - v000_b) + t_b * (v111_b - v110_b);
        }
    }

#undef SAMPLE
#undef LUT_INDEX
}

static void
clutter_color_op_3d_lut_do_transform (ClutterColorOp *op,
                                      float          *data,
                                      size_t          n_samples)
{
  ClutterColorOp3DLutPrivate *priv =
    clutter_color_op_3d_lut_get_instance_private (CLUTTER_COLOR_OP_3D_LUT (op));

  for (size_t i = 0; i < n_samples; i++)
    {
      float r, g, b;
      clutter_color_op_3d_lut_sample (priv, data[0], data[1], data[2], &r, &g, &b);
      data[0] = r;
      data[1] = g;
      data[2] = b;
      data += 4;
    }
}

static char *
clutter_color_op_3d_lut_to_string (ClutterColorOp *op)
{
  ClutterColorOp3DLutPrivate *priv =
    clutter_color_op_3d_lut_get_instance_private (CLUTTER_COLOR_OP_3D_LUT (op));

  return g_strdup_printf ("3d_lut(%u)", priv->size);
}

static void
clutter_color_op_3d_lut_dispose (GObject *object)
{
  ClutterColorOp3DLut *lut_3d = CLUTTER_COLOR_OP_3D_LUT (object);
  ClutterColorOp3DLutPrivate *priv =
    clutter_color_op_3d_lut_get_instance_private (lut_3d);

  g_clear_pointer (&priv->data, g_free);

  G_OBJECT_CLASS (clutter_color_op_3d_lut_parent_class)->dispose (object);
}

static void
clutter_color_op_3d_lut_class_init (ClutterColorOp3DLutClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  object_class->dispose = clutter_color_op_3d_lut_dispose;

  op_class->to_string = clutter_color_op_3d_lut_to_string;
  op_class->do_transform = clutter_color_op_3d_lut_do_transform;
}

static void
clutter_color_op_3d_lut_init (ClutterColorOp3DLut *op)
{
}

ClutterColorOp *
clutter_color_op_3d_lut_new (uint32_t size,
                             float   *data)
{
  g_autoptr (ClutterColorOp) op = NULL;
  ClutterColorOp3DLutPrivate *priv;

  op = g_object_new (CLUTTER_TYPE_COLOR_OP_3D_LUT, NULL);
  priv = clutter_color_op_3d_lut_get_instance_private (CLUTTER_COLOR_OP_3D_LUT (op));
  priv->size = size;
  priv->data = g_memdup2 (data, size * size * size * 3 * sizeof (float));

  return g_steal_pointer (&op);
}

void
clutter_color_op_3d_lut_get_data (ClutterColorOp  *op,
                                  uint32_t        *out_size,
                                  const float    **out_data)
{
  ClutterColorOp3DLutPrivate *priv =
    clutter_color_op_3d_lut_get_instance_private (CLUTTER_COLOR_OP_3D_LUT (op));

  if (out_size)
    *out_size = priv->size;
  if (out_data)
    *out_data = priv->data;
}

typedef struct _ClutterColorOpGammaPowerPrivate
{
  float power;
} ClutterColorOpGammaPowerPrivate;

struct _ClutterColorOpGammaPower
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorOpGammaPower,
                            clutter_color_op_gamma_power,
                            CLUTTER_TYPE_COLOR_OP)

static char *
clutter_color_op_gamma_power_to_string (ClutterColorOp *op)
{
  ClutterColorOpGammaPowerPrivate *priv =
    clutter_color_op_gamma_power_get_instance_private (CLUTTER_COLOR_OP_GAMMA_POWER (op));

  return g_strdup_printf ("gamma_power(%.2f)", priv->power);
}

static float
clutter_color_op_gamma_power_do_transform_one (ClutterColorOp *op,
                                               float           input)
{
  ClutterColorOpGammaPowerPrivate *priv =
    clutter_color_op_gamma_power_get_instance_private (CLUTTER_COLOR_OP_GAMMA_POWER (op));
  float v;

  v = fabsf (input);

  v = powf (v, priv->power);

  return input < 0.f ? -v : v;
}

static void
clutter_color_op_gamma_power_class_init (ClutterColorOpGammaPowerClass *klass)
{
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  op_class->to_string = clutter_color_op_gamma_power_to_string;
  op_class->do_transform_one = clutter_color_op_gamma_power_do_transform_one;
}

static void
clutter_color_op_gamma_power_init (ClutterColorOpGammaPower *op)
{
}

ClutterColorOp *
clutter_color_op_gamma_power_new (float power)
{
  g_autoptr (ClutterColorOpGammaPower) op = NULL;
  ClutterColorOpGammaPowerPrivate *priv;

  op = g_object_new (CLUTTER_TYPE_COLOR_OP_GAMMA_POWER, NULL);
  priv = clutter_color_op_gamma_power_get_instance_private (op);
  priv->power = power;

  return CLUTTER_COLOR_OP (g_steal_pointer (&op));
}

typedef struct _ClutterColorOpCurve1DPrivate
{
  size_t size;
  float *r;
  float *g;
  float *b;
  float *a;
} ClutterColorOpCurve1DPrivate;

struct _ClutterColorOpCurve1D
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorOpCurve1D,
                            clutter_color_op_curve_1d,
                            CLUTTER_TYPE_COLOR_OP)

static void
clutter_color_op_curve_1d_do_transform (ClutterColorOp *op,
                                        float          *data,
                                        size_t          n_samples)
{
  ClutterColorOpCurve1DPrivate *priv =
    clutter_color_op_curve_1d_get_instance_private (CLUTTER_COLOR_OP_CURVE_1D (op));
  size_t size = priv->size;

  for (size_t i = 0; i < n_samples; i++)
    {
      if (priv->r)
        data[0] = lut_lookup_interpolate (data[0], size, priv->r);
      if (priv->g)
        data[1] = lut_lookup_interpolate (data[1], size, priv->g);
      if (priv->b)
        data[2] = lut_lookup_interpolate (data[2], size, priv->b);
      if (priv->a)
        data[3] = lut_lookup_interpolate (data[3], size, priv->a);

      data += 4;
    }
}

static gboolean
clutter_color_op_curve_1d_get_transforms_alpha (ClutterColorOp *op)
{
  return TRUE;
}

static gboolean
clutter_color_op_curve_1d_get_clamps_input (ClutterColorOp *op)
{
  return TRUE;
}

static gboolean
clutter_color_op_curve_1d_get_clamps_output (ClutterColorOp *op)
{
  return TRUE;
}

static char *
clutter_color_op_curve_1d_to_string (ClutterColorOp *op)
{
  return g_strdup ("curve_1d");
}

static void
clutter_color_op_curve_1d_dispose (GObject *object)
{
  ClutterColorOpCurve1D *curve = CLUTTER_COLOR_OP_CURVE_1D (object);
  ClutterColorOpCurve1DPrivate *priv =
    clutter_color_op_curve_1d_get_instance_private (curve);

  g_clear_pointer (&priv->r, g_free);
  g_clear_pointer (&priv->g, g_free);
  g_clear_pointer (&priv->b, g_free);
  g_clear_pointer (&priv->a, g_free);

  G_OBJECT_CLASS (clutter_color_op_curve_1d_parent_class)->dispose (object);
}

static void
clutter_color_op_curve_1d_class_init (ClutterColorOpCurve1DClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  object_class->dispose = clutter_color_op_curve_1d_dispose;

  op_class->to_string = clutter_color_op_curve_1d_to_string;
  op_class->do_transform = clutter_color_op_curve_1d_do_transform;
  op_class->get_transforms_alpha = clutter_color_op_curve_1d_get_transforms_alpha;
  op_class->get_clamps_input = clutter_color_op_curve_1d_get_clamps_input;
  op_class->get_clamps_output = clutter_color_op_curve_1d_get_clamps_output;
}

static void
clutter_color_op_curve_1d_init (ClutterColorOpCurve1D *op)
{
}

ClutterColorOp *
clutter_color_op_curve_1d_new (size_t  size,
                               float  *r,
                               float  *g,
                               float  *b,
                               float  *a)
{
  g_autoptr (ClutterColorOpCurve1D) op = NULL;
  ClutterColorOpCurve1DPrivate *priv;

  op = g_object_new (CLUTTER_TYPE_COLOR_OP_CURVE_1D, NULL);
  priv = clutter_color_op_curve_1d_get_instance_private (op);
  priv->size = size;
  priv->r = r;
  priv->g = g;
  priv->b = b;
  priv->a = a;

  return CLUTTER_COLOR_OP (g_steal_pointer (&op));
}

ClutterColorOp *
clutter_color_op_curve_1d_new_rgb (size_t  size,
                                   float  *v)
{
  g_autoptr (ClutterColorOpCurve1D) op = NULL;
  ClutterColorOpCurve1DPrivate *priv;

  op = g_object_new (CLUTTER_TYPE_COLOR_OP_CURVE_1D, NULL);
  priv = clutter_color_op_curve_1d_get_instance_private (op);
  priv->size = size;
  priv->r = v;
  priv->g = g_memdup2 (v, sizeof (float) * size);
  priv->b = g_memdup2 (v, sizeof (float) * size);
  priv->a = NULL;

  return CLUTTER_COLOR_OP (g_steal_pointer (&op));
}

void
clutter_color_op_curve_1d_get_data (ClutterColorOp  *op,
                                    size_t          *out_size,
                                    const float    **out_r,
                                    const float    **out_g,
                                    const float    **out_b,
                                    const float    **out_a)
{
  ClutterColorOpCurve1DPrivate *priv =
    clutter_color_op_curve_1d_get_instance_private (CLUTTER_COLOR_OP_CURVE_1D (op));

  if (out_size)
    *out_size = priv->size;
  if (out_r)
    *out_r = priv->r;
  if (out_g)
    *out_g = priv->g;
  if (out_b)
    *out_b = priv->b;
  if (out_a)
    *out_a = priv->a;
}

typedef struct _ClutterColorOpMultiplyPrivate
{
  float value;
} ClutterColorOpMultiplyPrivate;

struct _ClutterColorOpMultiply
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorOpMultiply,
                            clutter_color_op_multiply,
                            CLUTTER_TYPE_COLOR_OP)

static char *
clutter_color_op_multiply_to_string (ClutterColorOp *op)
{
  ClutterColorOpMultiplyPrivate *priv =
    clutter_color_op_multiply_get_instance_private (CLUTTER_COLOR_OP_MULTIPLY (op));

  return g_strdup_printf ("multiply(%.2f)", priv->value);
}

static float
clutter_color_op_multiply_do_transform_one (ClutterColorOp *op,
                                            float           input)
{
  ClutterColorOpMultiplyPrivate *priv =
    clutter_color_op_multiply_get_instance_private (CLUTTER_COLOR_OP_MULTIPLY (op));

  return input * priv->value;
}

static void
clutter_color_op_multiply_class_init (ClutterColorOpMultiplyClass *klass)
{
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  op_class->to_string = clutter_color_op_multiply_to_string;
  op_class->do_transform_one = clutter_color_op_multiply_do_transform_one;
}

static void
clutter_color_op_multiply_init (ClutterColorOpMultiply *op)
{
}

ClutterColorOp *
clutter_color_op_multiply_new (float value)
{
  g_autoptr (ClutterColorOpMultiply) op = NULL;
  ClutterColorOpMultiplyPrivate *priv;

  op = g_object_new (CLUTTER_TYPE_COLOR_OP_MULTIPLY, NULL);
  priv = clutter_color_op_multiply_get_instance_private (op);
  priv->value = value;

  return CLUTTER_COLOR_OP (g_steal_pointer (&op));
}

typedef struct _ClutterColorOpYcbcrMatrixPrivate
{
  ClutterYcbcrCoefficients  coeffs;
  graphene_matrix_t        *matrix;
} ClutterColorOpYcbcrMatrixPrivate;

struct _ClutterColorOpYcbcrMatrix
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorOpYcbcrMatrix,
                            clutter_color_op_ycbcr_matrix,
                            CLUTTER_TYPE_COLOR_OP)

static void
clutter_color_op_ycbcr_matrix_do_transform (ClutterColorOp *op,
                                             float          *data,
                                             size_t          n_samples)
{
  ClutterColorOpYcbcrMatrixPrivate *priv =
    clutter_color_op_ycbcr_matrix_get_instance_private (
      CLUTTER_COLOR_OP_YCBCR_MATRIX (op));

  do_matrix_transform (priv->matrix, data, n_samples);
}

static char *
clutter_color_op_ycbcr_matrix_to_string (ClutterColorOp *op)
{
  ClutterColorOpYcbcrMatrixPrivate *priv =
    clutter_color_op_ycbcr_matrix_get_instance_private (
      CLUTTER_COLOR_OP_YCBCR_MATRIX (op));
  const char *name;

  switch (priv->coeffs)
    {
    case CLUTTER_YCBCR_COEFFICIENTS_IDENTITY_LIMITED:
      name = "identity_limited";
      break;
    case CLUTTER_YCBCR_COEFFICIENTS_BT601_FULL:
      name = "bt601_full";
      break;
    case CLUTTER_YCBCR_COEFFICIENTS_BT601_LIMITED:
      name = "bt601_limited";
      break;
    case CLUTTER_YCBCR_COEFFICIENTS_BT709_FULL:
      name = "bt709_full";
      break;
    case CLUTTER_YCBCR_COEFFICIENTS_BT709_LIMITED:
      name = "bt709_limited";
      break;
    case CLUTTER_YCBCR_COEFFICIENTS_BT2020_FULL:
      name = "bt2020_full";
      break;
    case CLUTTER_YCBCR_COEFFICIENTS_BT2020_LIMITED:
      name = "bt2020_limited";
      break;
    default:
      name = "unknown";
      break;
    }

  return g_strdup_printf ("ycbcr_matrix(%s)", name);
}

static gboolean
clutter_color_op_ycbcr_matrix_get_transforms_alpha (ClutterColorOp *op)
{
  return TRUE;
}

static void
clutter_color_op_ycbcr_matrix_dispose (GObject *object)
{
  ClutterColorOpYcbcrMatrix *ycbcr_op = CLUTTER_COLOR_OP_YCBCR_MATRIX (object);
  ClutterColorOpYcbcrMatrixPrivate *priv =
    clutter_color_op_ycbcr_matrix_get_instance_private (ycbcr_op);

  g_clear_pointer (&priv->matrix, graphene_matrix_free);

  G_OBJECT_CLASS (clutter_color_op_ycbcr_matrix_parent_class)->dispose (object);
}

static void
clutter_color_op_ycbcr_matrix_class_init (ClutterColorOpYcbcrMatrixClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  object_class->dispose = clutter_color_op_ycbcr_matrix_dispose;

  op_class->to_string = clutter_color_op_ycbcr_matrix_to_string;
  op_class->do_transform = clutter_color_op_ycbcr_matrix_do_transform;
  op_class->get_transforms_alpha = clutter_color_op_ycbcr_matrix_get_transforms_alpha;
}

static void
clutter_color_op_ycbcr_matrix_init (ClutterColorOpYcbcrMatrix *op)
{
}

ClutterColorOp *
clutter_color_op_ycbcr_matrix_new (ClutterYcbcrCoefficients  coeffs,
                                   graphene_matrix_t        *matrix)
{
  g_autoptr (ClutterColorOpYcbcrMatrix) op = NULL;
  ClutterColorOpYcbcrMatrixPrivate *priv;

  op = g_object_new (CLUTTER_TYPE_COLOR_OP_YCBCR_MATRIX, NULL);
  priv = clutter_color_op_ycbcr_matrix_get_instance_private (op);
  priv->coeffs = coeffs;
  priv->matrix = matrix;

  return CLUTTER_COLOR_OP (g_steal_pointer (&op));
}

ClutterYcbcrCoefficients
clutter_color_op_ycbcr_matrix_get_coeffs (ClutterColorOp *op)
{
  ClutterColorOpYcbcrMatrixPrivate *priv =
    clutter_color_op_ycbcr_matrix_get_instance_private (
      CLUTTER_COLOR_OP_YCBCR_MATRIX (op));

  return priv->coeffs;
}

typedef struct _ClutterColorOpMatrix4x4Private
{
  graphene_matrix_t *matrix;
} ClutterColorOpMatrix4x4Private;

struct _ClutterColorOpMatrix4x4
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorOpMatrix4x4,
                            clutter_color_op_matrix_4x4,
                            CLUTTER_TYPE_COLOR_OP)

static void
clutter_color_op_matrix_4x4_do_transform (ClutterColorOp *op,
                                          float          *data,
                                          size_t          n_samples)
{
  ClutterColorOpMatrix4x4Private *priv =
    clutter_color_op_matrix_4x4_get_instance_private (CLUTTER_COLOR_OP_MATRIX_4X4 (op));

  do_matrix_transform (priv->matrix, data, n_samples);
}

static gboolean
clutter_color_op_matrix_4x4_get_transforms_alpha (ClutterColorOp *op)
{
  return TRUE;
}

static char *
clutter_color_op_matrix_4x4_to_string (ClutterColorOp *op)
{
  return g_strdup ("matrix_4x4");
}

static void
clutter_color_op_matrix_4x4_dispose (GObject *object)
{
  ClutterColorOpMatrix4x4 *matrix_op = CLUTTER_COLOR_OP_MATRIX_4X4 (object);
  ClutterColorOpMatrix4x4Private *priv =
    clutter_color_op_matrix_4x4_get_instance_private (matrix_op);

  g_clear_pointer (&priv->matrix, graphene_matrix_free);

  G_OBJECT_CLASS (clutter_color_op_matrix_4x4_parent_class)->dispose (object);
}

static void
clutter_color_op_matrix_4x4_class_init (ClutterColorOpMatrix4x4Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  object_class->dispose = clutter_color_op_matrix_4x4_dispose;

  op_class->to_string = clutter_color_op_matrix_4x4_to_string;
  op_class->do_transform = clutter_color_op_matrix_4x4_do_transform;
  op_class->get_transforms_alpha = clutter_color_op_matrix_4x4_get_transforms_alpha;
}

static void
clutter_color_op_matrix_4x4_init (ClutterColorOpMatrix4x4 *op)
{
}

ClutterColorOp *
clutter_color_op_matrix_4x4_new (graphene_matrix_t *matrix)
{
  g_autoptr (ClutterColorOpMatrix4x4) op = NULL;
  ClutterColorOpMatrix4x4Private *priv;

  op = g_object_new (CLUTTER_TYPE_COLOR_OP_MATRIX_4X4, NULL);
  priv = clutter_color_op_matrix_4x4_get_instance_private (op);
  priv->matrix = matrix;

  return CLUTTER_COLOR_OP (g_steal_pointer (&op));
}

const graphene_matrix_t *
clutter_color_op_matrix_4x4_get_matrix (ClutterColorOp *op)
{
  ClutterColorOpMatrix4x4Private *priv =
    clutter_color_op_matrix_4x4_get_instance_private (CLUTTER_COLOR_OP_MATRIX_4X4 (op));

  return priv->matrix;
}

struct _ClutterColorOpUnpremultiply
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE (ClutterColorOpUnpremultiply, clutter_color_op_unpremultiply,
               CLUTTER_TYPE_COLOR_OP)

static char *
clutter_color_op_unpremultiply_to_string (ClutterColorOp *op)
{
  return g_strdup ("unpremultiply");
}

static void
clutter_color_op_unpremultiply_do_transform (ClutterColorOp *op,
                                             float          *data,
                                             size_t          n_samples)
{
  for (size_t i = 0; i < n_samples; i++)
    {
      if (data[i * 4 + 3] <= 0.0f)
        continue;

      data[i * 4 + 0] /= data[i * 4 + 3];
      data[i * 4 + 1] /= data[i * 4 + 3];
      data[i * 4 + 2] /= data[i * 4 + 3];
    }
}

static void
clutter_color_op_unpremultiply_class_init (ClutterColorOpUnpremultiplyClass *klass)
{
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  op_class->to_string = clutter_color_op_unpremultiply_to_string;
  op_class->do_transform = clutter_color_op_unpremultiply_do_transform;
}

static void
clutter_color_op_unpremultiply_init (ClutterColorOpUnpremultiply *op)
{
}

ClutterColorOp *
clutter_color_op_unpremultiply_new (void)
{
  return g_object_new (CLUTTER_TYPE_COLOR_OP_UNPREMULTIPLY, NULL);
}

struct _ClutterColorOpPremultiply
{
  ClutterColorOp parent;
};

G_DEFINE_TYPE (ClutterColorOpPremultiply, clutter_color_op_premultiply,
               CLUTTER_TYPE_COLOR_OP)

static char *
clutter_color_op_premultiply_to_string (ClutterColorOp *op)
{
  return g_strdup ("premultiply");
}

static void
clutter_color_op_premultiply_do_transform (ClutterColorOp *op,
                                           float          *data,
                                           size_t          n_samples)
{
  for (size_t i = 0; i < n_samples; i++)
    {
      data[i * 4 + 0] *= data[i * 4 + 3];
      data[i * 4 + 1] *= data[i * 4 + 3];
      data[i * 4 + 2] *= data[i * 4 + 3];
    }
}

static void
clutter_color_op_premultiply_class_init (ClutterColorOpPremultiplyClass *klass)
{
  ClutterColorOpClass *op_class = CLUTTER_COLOR_OP_CLASS (klass);

  op_class->to_string = clutter_color_op_premultiply_to_string;
  op_class->do_transform = clutter_color_op_premultiply_do_transform;
}

static void
clutter_color_op_premultiply_init (ClutterColorOpPremultiply *op)
{
}

ClutterColorOp *
clutter_color_op_premultiply_new (void)
{
  return g_object_new (CLUTTER_TYPE_COLOR_OP_PREMULTIPLY, NULL);
}

float
clutter_color_op_gamma_power_get_power (ClutterColorOp *op)
{
  ClutterColorOpGammaPowerPrivate *priv =
    clutter_color_op_gamma_power_get_instance_private (
      CLUTTER_COLOR_OP_GAMMA_POWER (op));

  return priv->power;
}

float
clutter_color_op_multiply_get_value (ClutterColorOp *op)
{
  ClutterColorOpMultiplyPrivate *priv =
    clutter_color_op_multiply_get_instance_private (
      CLUTTER_COLOR_OP_MULTIPLY (op));

  return priv->value;
}

gboolean
clutter_color_op_srgb_piecewise_eotf_get_unit_range_only (ClutterColorOp *op)
{
  ClutterColorOpSrgbPiecewiseEotfPrivate *priv =
    clutter_color_op_srgb_piecewise_eotf_get_instance_private (
      CLUTTER_COLOR_OP_SRGB_PIECEWISE_EOTF (op));

  return priv->unit_range_only;
}

gboolean
clutter_color_op_srgb_piecewise_inv_eotf_get_unit_range_only (ClutterColorOp *op)
{
  ClutterColorOpSrgbPiecewiseInvEotfPrivate *priv =
    clutter_color_op_srgb_piecewise_inv_eotf_get_instance_private (
      CLUTTER_COLOR_OP_SRGB_PIECEWISE_INV_EOTF (op));

  return priv->unit_range_only;
}

const graphene_matrix_t *
clutter_color_op_ycbcr_matrix_get_matrix (ClutterColorOp *op)
{
  ClutterColorOpYcbcrMatrixPrivate *priv =
    clutter_color_op_ycbcr_matrix_get_instance_private (
      CLUTTER_COLOR_OP_YCBCR_MATRIX (op));

  return priv->matrix;
}

static ClutterColorOp *
combine_multiply_with_matrix (float                    multiply_value,
                              const graphene_matrix_t  *matrix,
                              gboolean                  multiply_first)
{
  graphene_matrix_t *scale_matrix;
  graphene_matrix_t *result;

  scale_matrix = graphene_matrix_alloc ();
  graphene_matrix_init_from_float (scale_matrix, (float[16]) {
    multiply_value, 0.0f, 0.0f, 0.0f,
    0.0f, multiply_value, 0.0f, 0.0f,
    0.0f, 0.0f, multiply_value, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });

  result = graphene_matrix_alloc ();
  if (multiply_first)
    graphene_matrix_multiply (scale_matrix, matrix, result);
  else
    graphene_matrix_multiply (matrix, scale_matrix, result);

  graphene_matrix_free (scale_matrix);

  return clutter_color_op_matrix_4x4_new (result);
}

static const graphene_matrix_t *
get_matrix_from_op (ClutterColorOp *op)
{
  if (CLUTTER_IS_COLOR_OP_MATRIX_4X4 (op))
    return clutter_color_op_matrix_4x4_get_matrix (op);

  if (CLUTTER_IS_COLOR_OP_YCBCR_MATRIX (op))
    return clutter_color_op_ycbcr_matrix_get_matrix (op);

  return NULL;
}

/**
 * clutter_color_op_try_combine:
 *
 * Returns: (transfer full) (nullable): A combined op, or %NULL
 */
ClutterColorOp *
clutter_color_op_try_combine (ClutterColorOp *a,
                              ClutterColorOp *b)
{
  const graphene_matrix_t *ma, *mb;

  ma = get_matrix_from_op (a);
  mb = get_matrix_from_op (b);

  if (ma && mb)
    {
      graphene_matrix_t *result;

      result = graphene_matrix_alloc ();
      graphene_matrix_multiply (ma, mb, result);
      return clutter_color_op_matrix_4x4_new (result);
    }

  if (ma && CLUTTER_IS_COLOR_OP_MULTIPLY (b))
    return combine_multiply_with_matrix (clutter_color_op_multiply_get_value (b),
                                         ma, FALSE);

  if (CLUTTER_IS_COLOR_OP_MULTIPLY (a) && mb)
    return combine_multiply_with_matrix (clutter_color_op_multiply_get_value (a),
                                         mb, TRUE);

  if (CLUTTER_IS_COLOR_OP_MULTIPLY (a) &&
      CLUTTER_IS_COLOR_OP_MULTIPLY (b))
    {
      float va, vb;

      va = clutter_color_op_multiply_get_value (a);
      vb = clutter_color_op_multiply_get_value (b);
      return clutter_color_op_multiply_new (va * vb);
    }

  if (CLUTTER_IS_COLOR_OP_GAMMA_POWER (a) &&
      CLUTTER_IS_COLOR_OP_GAMMA_POWER (b))
    {
      float pa, pb;

      pa = clutter_color_op_gamma_power_get_power (a);
      pb = clutter_color_op_gamma_power_get_power (b);
      return clutter_color_op_gamma_power_new (pa * pb);
    }

  return NULL;
}

gboolean
clutter_color_op_can_lower_to_curve_1d (ClutterColorOp *op,
                                        size_t          n_samples)
{
  if (CLUTTER_IS_COLOR_OP_GAMMA_POWER (op))
    return TRUE;

  if (CLUTTER_IS_COLOR_OP_PQ_EOTF (op) ||
      CLUTTER_IS_COLOR_OP_PQ_INV_EOTF (op))
    return n_samples >= 4096;

  if (CLUTTER_IS_COLOR_OP_SRGB_PIECEWISE_EOTF (op))
    return clutter_color_op_srgb_piecewise_eotf_get_unit_range_only (op);

  if (CLUTTER_IS_COLOR_OP_SRGB_PIECEWISE_INV_EOTF (op))
    return clutter_color_op_srgb_piecewise_inv_eotf_get_unit_range_only (op);

  return FALSE;
}

/**
 * clutter_color_op_lower_to_curve_1d:
 *
 * Returns: (transfer full) (nullable): A lowered 1D curve op, or %NULL
 */
ClutterColorOp *
clutter_color_op_lower_to_curve_1d (ClutterColorOp *op,
                                    size_t          n_samples)
{
  if (!clutter_color_op_can_lower_to_curve_1d (op, n_samples))
    return NULL;

  return lower_to_curve_uniform (op, n_samples);
}

gboolean
clutter_color_op_can_lower_to_3d_lut (ClutterColorOp *op)
{
  return CLUTTER_IS_COLOR_OP_MATRIX_4X4 (op) ||
         CLUTTER_IS_COLOR_OP_YCBCR_MATRIX (op);
}

/**
 * clutter_color_op_lower_to_3d_lut:
 *
 * Returns: (transfer full) (nullable): A lowered 3D LUT op, or %NULL
 */
ClutterColorOp *
clutter_color_op_lower_to_3d_lut (ClutterColorOp *op,
                                  uint32_t        size)
{
  if (!clutter_color_op_can_lower_to_3d_lut (op))
    return NULL;

  return lower_to_3d_lut (op, size);
}

gboolean
clutter_color_op_can_lower_to_matrix_4x4 (ClutterColorOp *op)
{
  return CLUTTER_IS_COLOR_OP_YCBCR_MATRIX (op);
}

/**
 * clutter_color_op_lower_to_matrix_4x4:
 *
 * Returns: (transfer full) (nullable): A lowered matrix op, or %NULL
 */
ClutterColorOp *
clutter_color_op_lower_to_matrix_4x4 (ClutterColorOp *op)
{
  const graphene_matrix_t *src;
  graphene_matrix_t *copy;

  if (!clutter_color_op_can_lower_to_matrix_4x4 (op))
    return NULL;

  src = clutter_color_op_ycbcr_matrix_get_matrix (op);
  copy = graphene_matrix_alloc ();
  graphene_matrix_init_from_matrix (copy, src);
  return clutter_color_op_matrix_4x4_new (copy);
}
