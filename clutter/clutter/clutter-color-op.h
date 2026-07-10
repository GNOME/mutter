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

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_COLOR_OP (clutter_color_op_get_type ())
CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterColorOp,
                          clutter_color_op,
                          CLUTTER, COLOR_OP,
                          GObject)

struct _ClutterColorOpClass
{
  GObjectClass parent_class;

  char * (* to_string) (ClutterColorOp *op);

  void (* do_transform) (ClutterColorOp *op,
                         float          *data,
                         size_t          n_samples);

  float (* do_transform_one) (ClutterColorOp *op,
                              float           input);

  gboolean (* get_transforms_alpha) (ClutterColorOp *op);
  gboolean (* get_clamps_input) (ClutterColorOp *op);
  gboolean (* get_clamps_output) (ClutterColorOp *op);
};

char * clutter_color_op_to_string (ClutterColorOp *op);

CLUTTER_EXPORT
void clutter_color_op_do_transform (ClutterColorOp *op,
                                    float          *data,
                                    size_t          n_samples);

CLUTTER_EXPORT
float clutter_color_op_do_transform_one (ClutterColorOp *op,
                                         float           input);

gboolean clutter_color_op_get_transforms_alpha (ClutterColorOp *op);
gboolean clutter_color_op_get_clamps_input (ClutterColorOp *op);
gboolean clutter_color_op_get_clamps_output (ClutterColorOp *op);

#define CLUTTER_TYPE_COLOR_OP_CLAMP_UNIT (clutter_color_op_clamp_unit_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpClampUnit, clutter_color_op_clamp_unit,
                      CLUTTER, COLOR_OP_CLAMP_UNIT,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_clamp_unit_new (void);

#define CLUTTER_TYPE_COLOR_OP_SRGB_PIECEWISE_EOTF (clutter_color_op_srgb_piecewise_eotf_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpSrgbPiecewiseEotf,
                      clutter_color_op_srgb_piecewise_eotf,
                      CLUTTER, COLOR_OP_SRGB_PIECEWISE_EOTF,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_srgb_piecewise_eotf_new (gboolean unit_range_only);

#define CLUTTER_TYPE_COLOR_OP_SRGB_PIECEWISE_INV_EOTF (clutter_color_op_srgb_piecewise_inv_eotf_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpSrgbPiecewiseInvEotf,
                      clutter_color_op_srgb_piecewise_inv_eotf,
                      CLUTTER, COLOR_OP_SRGB_PIECEWISE_INV_EOTF,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_srgb_piecewise_inv_eotf_new (gboolean unit_range_only);

#define CLUTTER_TYPE_COLOR_OP_PQ_EOTF (clutter_color_op_pq_eotf_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpPqEotf,
                      clutter_color_op_pq_eotf,
                      CLUTTER, COLOR_OP_PQ_EOTF,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_pq_eotf_new (void);

#define CLUTTER_TYPE_COLOR_OP_PQ_INV_EOTF (clutter_color_op_pq_inv_eotf_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpPqInvEotf,
                      clutter_color_op_pq_inv_eotf,
                      CLUTTER, COLOR_OP_PQ_INV_EOTF,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_pq_inv_eotf_new (void);

#define CLUTTER_TYPE_COLOR_OP_3D_LUT (clutter_color_op_3d_lut_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOp3DLut,
                      clutter_color_op_3d_lut,
                      CLUTTER, COLOR_OP_3D_LUT,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_3d_lut_new (uint32_t size,
                                              float   *data);

#define CLUTTER_TYPE_COLOR_OP_GAMMA_POWER (clutter_color_op_gamma_power_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpGammaPower,
                      clutter_color_op_gamma_power,
                      CLUTTER, COLOR_OP_GAMMA_POWER,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_gamma_power_new (float power);

#define CLUTTER_TYPE_COLOR_OP_CURVE_1D (clutter_color_op_curve_1d_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpCurve1D,
                      clutter_color_op_curve_1d,
                      CLUTTER, COLOR_OP_CURVE_1D,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_curve_1d_new (size_t  size,
                                                float  *r,
                                                float  *g,
                                                float  *b,
                                                float  *a);

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_curve_1d_new_rgb (size_t  size,
                                                    float  *v);

typedef enum _ClutterYcbcrCoefficients
{
  CLUTTER_YCBCR_COEFFICIENTS_IDENTITY_LIMITED,
  CLUTTER_YCBCR_COEFFICIENTS_BT601_FULL,
  CLUTTER_YCBCR_COEFFICIENTS_BT601_LIMITED,
  CLUTTER_YCBCR_COEFFICIENTS_BT709_FULL,
  CLUTTER_YCBCR_COEFFICIENTS_BT709_LIMITED,
  CLUTTER_YCBCR_COEFFICIENTS_BT2020_FULL,
  CLUTTER_YCBCR_COEFFICIENTS_BT2020_LIMITED,
} ClutterYcbcrCoefficients;

#define CLUTTER_TYPE_COLOR_OP_YCBCR_MATRIX (clutter_color_op_ycbcr_matrix_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpYcbcrMatrix,
                      clutter_color_op_ycbcr_matrix,
                      CLUTTER, COLOR_OP_YCBCR_MATRIX,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_ycbcr_matrix_new (ClutterYcbcrCoefficients  coeffs,
                                                    graphene_matrix_t        *matrix);

CLUTTER_EXPORT
ClutterYcbcrCoefficients clutter_color_op_ycbcr_matrix_get_coeffs (ClutterColorOp *op);

#define CLUTTER_TYPE_COLOR_OP_MATRIX_4X4 (clutter_color_op_matrix_4x4_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpMatrix4x4,
                      clutter_color_op_matrix_4x4,
                      CLUTTER, COLOR_OP_MATRIX_4X4,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_matrix_4x4_new (graphene_matrix_t *matrix);

#define CLUTTER_TYPE_COLOR_OP_MULTIPLY (clutter_color_op_multiply_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpMultiply,
                      clutter_color_op_multiply,
                      CLUTTER, COLOR_OP_MULTIPLY,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_multiply_new (float value);

#define CLUTTER_TYPE_COLOR_OP_UNPREMULTIPLY (clutter_color_op_unpremultiply_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpUnpremultiply,
                      clutter_color_op_unpremultiply,
                      CLUTTER, COLOR_OP_UNPREMULTIPLY,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_unpremultiply_new (void);

#define CLUTTER_TYPE_COLOR_OP_PREMULTIPLY (clutter_color_op_premultiply_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorOpPremultiply,
                      clutter_color_op_premultiply,
                      CLUTTER, COLOR_OP_PREMULTIPLY,
                      ClutterColorOp)

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_premultiply_new (void);

CLUTTER_EXPORT
const graphene_matrix_t * clutter_color_op_matrix_4x4_get_matrix (ClutterColorOp *op);

CLUTTER_EXPORT
float clutter_color_op_gamma_power_get_power (ClutterColorOp *op);

CLUTTER_EXPORT
float clutter_color_op_multiply_get_value (ClutterColorOp *op);

CLUTTER_EXPORT
gboolean clutter_color_op_srgb_piecewise_eotf_get_unit_range_only (ClutterColorOp *op);

CLUTTER_EXPORT
gboolean clutter_color_op_srgb_piecewise_inv_eotf_get_unit_range_only (ClutterColorOp *op);

CLUTTER_EXPORT
const graphene_matrix_t * clutter_color_op_ycbcr_matrix_get_matrix (ClutterColorOp *op);

CLUTTER_EXPORT
void clutter_color_op_curve_1d_get_data (ClutterColorOp  *op,
                                         size_t          *out_size,
                                         const float    **out_r,
                                         const float    **out_g,
                                         const float    **out_b,
                                         const float    **out_a);

CLUTTER_EXPORT
void clutter_color_op_3d_lut_get_data (ClutterColorOp  *op,
                                       uint32_t        *out_size,
                                       const float    **out_data);

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_try_combine (ClutterColorOp *a,
                                               ClutterColorOp *b);

CLUTTER_EXPORT
gboolean clutter_color_op_can_lower_to_curve_1d (ClutterColorOp *op,
                                                 size_t          n_samples);

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_lower_to_curve_1d (ClutterColorOp *op,
                                                     size_t          n_samples);

CLUTTER_EXPORT
gboolean clutter_color_op_can_lower_to_3d_lut (ClutterColorOp *op);

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_lower_to_3d_lut (ClutterColorOp *op,
                                                   uint32_t        size);

CLUTTER_EXPORT
gboolean clutter_color_op_can_lower_to_matrix_4x4 (ClutterColorOp *op);

CLUTTER_EXPORT
ClutterColorOp * clutter_color_op_lower_to_matrix_4x4 (ClutterColorOp *op);

G_END_DECLS
