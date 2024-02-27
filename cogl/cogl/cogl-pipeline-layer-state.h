/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-pipeline.h"
#include "cogl/cogl-color.h"
#include "cogl/cogl-texture.h"

G_BEGIN_DECLS

/**
 * CoglPipelineFilter:
 * @COGL_PIPELINE_FILTER_NEAREST: Measuring in manhatten distance from the,
 *   current pixel center, use the nearest texture texel
 * @COGL_PIPELINE_FILTER_LINEAR: Use the weighted average of the 4 texels
 *   nearest the current pixel center
 * @COGL_PIPELINE_FILTER_NEAREST_MIPMAP_NEAREST: Select the mimap level whose
 *   texel size most closely matches the current pixel, and use the
 *   %COGL_PIPELINE_FILTER_NEAREST criterion
 * @COGL_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST: Select the mimap level whose
 *   texel size most closely matches the current pixel, and use the
 *   %COGL_PIPELINE_FILTER_LINEAR criterion
 * @COGL_PIPELINE_FILTER_NEAREST_MIPMAP_LINEAR: Select the two mimap levels
 *   whose texel size most closely matches the current pixel, use
 *   the %COGL_PIPELINE_FILTER_NEAREST criterion on each one and take
 *   their weighted average
 * @COGL_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR: Select the two mimap levels
 *   whose texel size most closely matches the current pixel, use
 *   the %COGL_PIPELINE_FILTER_LINEAR criterion on each one and take
 *   their weighted average
 *
 * Texture filtering is used whenever the current pixel maps either to more
 * than one texture element (texel) or less than one. These filter enums
 * correspond to different strategies used to come up with a pixel color, by
 * possibly referring to multiple neighbouring texels and taking a weighted
 * average or simply using the nearest texel.
 */
typedef enum
{
  COGL_PIPELINE_FILTER_NEAREST = 0x2600,
  COGL_PIPELINE_FILTER_LINEAR = 0x2601,
  COGL_PIPELINE_FILTER_NEAREST_MIPMAP_NEAREST = 0x2700,
  COGL_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST = 0x2701,
  COGL_PIPELINE_FILTER_NEAREST_MIPMAP_LINEAR = 0x2702,
  COGL_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR = 0x2703
} CoglPipelineFilter;
/* NB: these values come from the equivalents in gl.h */

/**
 * CoglPipelineWrapMode:
 * @COGL_PIPELINE_WRAP_MODE_REPEAT: The texture will be repeated. This
 *   is useful for example to draw a tiled background.
 * @COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE: The coordinates outside the
 *   range 0→1 will sample copies of the edge pixels of the
 *   texture. This is useful to avoid artifacts if only one copy of
 *   the texture is being rendered.
 * @COGL_PIPELINE_WRAP_MODE_AUTOMATIC: Cogl will try to automatically
 *   decide which of the above two to use. For cogl_rectangle(), it
 *   will use repeat mode if any of the texture coordinates are
 *   outside the range 0→1, otherwise it will use clamp to edge. For
 *   cogl_polygon() it will always use repeat mode. For
 *   cogl_vertex_buffer_draw() it will use repeat mode except for
 *   layers that have point sprite coordinate generation enabled. This
 *   is the default value.
 *
 * The wrap mode specifies what happens when texture coordinates
 * outside the range 0→1 are used. Note that if the filter mode is
 * anything but %COGL_PIPELINE_FILTER_NEAREST then texels outside the
 * range 0→1 might be used even when the coordinate is exactly 0 or 1
 * because OpenGL will try to sample neighbouring pixels. For example
 * if you are trying to render the full texture then you may get
 * artifacts around the edges when the pixels from the other side are
 * merged in if the wrap mode is set to repeat.
 */
/* GL_ALWAYS is just used here as a value that is known not to clash
 * with any valid GL wrap modes
 *
 * XXX: keep the values in sync with the CoglPipelineWrapModeInternal
 * enum so no conversion is actually needed.
 */
typedef enum
{
  COGL_PIPELINE_WRAP_MODE_REPEAT = 0x2901,
  COGL_PIPELINE_WRAP_MODE_MIRRORED_REPEAT = 0x8370,
  COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE = 0x812F,
  COGL_PIPELINE_WRAP_MODE_AUTOMATIC = 0x0207 /* GL_ALWAYS */
} CoglPipelineWrapMode;
/* NB: these values come from the equivalents in gl.h */

/**
 * cogl_pipeline_set_layer:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the index of the layer
 * @texture: a #CoglTexture for the layer object
 *
 * In addition to the standard OpenGL lighting model a Cogl pipeline may have
 * one or more layers comprised of textures that can be blended together in
 * order, with a number of different texture combine modes. This function
 * defines a new texture layer.
 *
 * The index values of multiple layers do not have to be consecutive; it is
 * only their relative order that is important.
 *
 * The @texture parameter can also be %NULL in which case the pipeline
 * will use a default white texture. The type of the default texture
 * will be the same as whatever texture was last used for the pipeline
 * or %COGL_TEXTURE_TYPE_2D if none has been specified yet. To
 * explicitly specify the type of default texture required, use
 * cogl_pipeline_set_layer_null_texture() instead.
 *
 * In the future, we may define other types of pipeline layers, such
 * as purely GLSL based layers.
 */
COGL_EXPORT void
cogl_pipeline_set_layer_texture (CoglPipeline *pipeline,
                                 int           layer_index,
                                 CoglTexture  *texture);

/**
 * cogl_pipeline_set_layer_null_texture:
 * @pipeline: A #CoglPipeline
 * @layer_index: The layer number to modify
 *
 * Sets the texture for this layer to be the default texture for the
 * given type. The default texture is a 1x1 pixel white texture.
 *
 * This function is mostly useful if you want to create a base
 * pipeline that you want to create multiple copies from using
 * cogl_pipeline_copy(). In that case this function can be used to
 * specify the texture type so that any pipeline copies can share the
 * internal texture type state for efficiency.
 */
COGL_EXPORT void
cogl_pipeline_set_layer_null_texture (CoglPipeline *pipeline,
                                      int layer_index);

/**
 * cogl_pipeline_get_layer_texture:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the index of the layer
 *
 * Return value: (transfer none): the texture that was set for the
 *   given layer of the pipeline or %NULL if no texture was set.
 */
COGL_EXPORT CoglTexture *
cogl_pipeline_get_layer_texture (CoglPipeline *pipeline,
                                 int layer_index);

/**
 * cogl_pipeline_remove_layer:
 * @pipeline: A #CoglPipeline object
 * @layer_index: Specifies the layer you want to remove
 *
 * This function removes a layer from your pipeline
 */
COGL_EXPORT void
cogl_pipeline_remove_layer (CoglPipeline *pipeline,
			    int           layer_index);

/**
 * cogl_pipeline_set_layer_combine:
 * @pipeline: A #CoglPipeline object
 * @layer_index: Specifies the layer you want define a combine function for
 * @blend_string: A Cogl blend string describing the desired
 *  texture combine function.
 * @error: A #GError that may report parse errors or lack of GPU/driver
 *   support. May be %NULL, in which case a warning will be printed out if an
 *   error is encountered.
 *
 *
 * These are all the functions available for texture combining:
 * 
 * - `REPLACE(arg0) = arg0`
 * - `MODULATE(arg0, arg1) = arg0 x arg1`
 * - `ADD(arg0, arg1) = arg0 + arg1`
 * - `ADD_SIGNED(arg0, arg1) = arg0 + arg1 - 0.5`
 * - `INTERPOLATE(arg0, arg1, arg2) = arg0 x arg2 + arg1 x (1 - arg2)`
 * - `SUBTRACT(arg0, arg1) = arg0 - arg1`
 * - 
 * ```
 *  DOT3_RGB(arg0, arg1) = 4 x ((arg0[R] - 0.5)) * (arg1[R] - 0.5) +
 *                              (arg0[G] - 0.5)) * (arg1[G] - 0.5) +
 *                              (arg0[B] - 0.5)) * (arg1[B] - 0.5))
 * ```
 * -
 * ```
 *  DOT3_RGBA(arg0, arg1) = 4 x ((arg0[R] - 0.5)) * (arg1[R] - 0.5) +
 *                               (arg0[G] - 0.5)) * (arg1[G] - 0.5) +
 *                               (arg0[B] - 0.5)) * (arg1[B] - 0.5))
 * ```
 *
 * The valid source names for texture combining are:
 * 
 * - `TEXTURE`: Use the color from the current texture layer
 * - `TEXTURE_0, TEXTURE_1, etc`: Use the color from the specified texture layer
 * - `CONSTANT`: Use the color from the constant given with
 *     [method@Cogl.Pipeline.set_layer_combine_constant]
 * - `PRIMARY`: Use the color of the pipeline as set with
 *     [method@Cogl.Pipeline.set_color]
 * - `PREVIOUS`: Either use the texture color from the previous layer, or
 *     if this is layer 0, use the color of the pipeline as set with
 *     [method@Cogl.Pipeline.set_color]
 * 
 * Layer Combine Examples:
 * 
 * This is effectively what the default blending is:
 * 
 * ```
 * RGBA = MODULATE (PREVIOUS, TEXTURE)
 * ```
 * 
 * This could be used to cross-fade between two images, using
 * the alpha component of a constant as the interpolator. The constant
 * color is given by calling [method@Cogl.Pipeline.set_layer_combine_constant].
 * 
 * ```
 * RGBA = INTERPOLATE (PREVIOUS, TEXTURE, CONSTANT[A])
 * ```
 *
 * You can't give a multiplication factor for arguments as you can
 * with blending.
 *
 * Return value: %TRUE if the blend string was successfully parsed, and the
 *   described texture combining is supported by the underlying driver and
 *   or hardware. On failure, %FALSE is returned and @error is set
 */
COGL_EXPORT gboolean
cogl_pipeline_set_layer_combine (CoglPipeline *pipeline,
				 int           layer_index,
				 const char   *blend_string,
                                 GError      **error);

/**
 * cogl_pipeline_set_layer_combine_constant:
 * @pipeline: A #CoglPipeline object
 * @layer_index: Specifies the layer you want to specify a constant used
 *               for texture combining
 * @constant: The constant color you want
 *
 * When you are using the 'CONSTANT' color source in a layer combine
 * description then you can use this function to define its value.
 */
COGL_EXPORT void
cogl_pipeline_set_layer_combine_constant (CoglPipeline    *pipeline,
                                          int              layer_index,
                                          const CoglColor *constant);

/**
 * cogl_pipeline_set_layer_matrix:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the index for the layer inside @pipeline
 * @matrix: the transformation matrix for the layer
 *
 * This function lets you set a matrix that can be used to e.g. translate
 * and rotate a single layer of a pipeline used to fill your geometry.
 */
COGL_EXPORT void
cogl_pipeline_set_layer_matrix (CoglPipeline     *pipeline,
				int               layer_index,
				const graphene_matrix_t *matrix);

/**
 * cogl_pipeline_get_n_layers:
 * @pipeline: A #CoglPipeline object
 *
 * Retrieves the number of layers defined for the given @pipeline
 *
 * Return value: the number of layers
 */
COGL_EXPORT int
cogl_pipeline_get_n_layers (CoglPipeline *pipeline);

/**
 * cogl_pipeline_get_layer_filters:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the layer number to change.
 * @min_filter: (out): Return location for the filter used when scaling
 *   a texture down.
 * @mag_filter: (out): Return location for the filter used when magnifying
 *   a texture.
 *
 * Returns the decimation and interpolation filters used when a texture is
 * drawn at other scales than 100%.
 */
COGL_EXPORT void
cogl_pipeline_get_layer_filters (CoglPipeline       *pipeline,
                                 int                 layer_index,
                                 CoglPipelineFilter *min_filter,
                                 CoglPipelineFilter *mag_filter);

/**
 * cogl_pipeline_set_layer_filters:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the layer number to change.
 * @min_filter: the filter used when scaling a texture down.
 * @mag_filter: the filter used when magnifying a texture.
 *
 * Changes the decimation and interpolation filters used when a texture is
 * drawn at other scales than 100%.
 *
 * It is an error to pass anything other than
 * %COGL_PIPELINE_FILTER_NEAREST or %COGL_PIPELINE_FILTER_LINEAR as
 * magnification filters since magnification doesn't ever need to
 * reference values stored in the mipmap chain.
 */
COGL_EXPORT void
cogl_pipeline_set_layer_filters (CoglPipeline      *pipeline,
                                 int                layer_index,
                                 CoglPipelineFilter min_filter,
                                 CoglPipelineFilter mag_filter);

/**
 * cogl_pipeline_set_layer_point_sprite_coords_enabled:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the layer number to change.
 * @enable: whether to enable point sprite coord generation.
 * @error: A return location for a #GError, or NULL to ignore errors.
 *
 * When rendering points, if @enable is %TRUE then the texture
 * coordinates for this layer will be replaced with coordinates that
 * vary from 0.0 to 1.0 across the primitive. The top left of the
 * point will have the coordinates 0.0,0.0 and the bottom right will
 * have 1.0,1.0. If @enable is %FALSE then the coordinates will be
 * fixed for the entire point.
 *
 * Return value: %TRUE if the function succeeds, %FALSE otherwise.
 */
COGL_EXPORT gboolean
cogl_pipeline_set_layer_point_sprite_coords_enabled (CoglPipeline *pipeline,
                                                     int           layer_index,
                                                     gboolean      enable,
                                                     GError      **error);

/**
 * cogl_pipeline_get_layer_point_sprite_coords_enabled:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the layer number to check.
 *
 * Gets whether point sprite coordinate generation is enabled for this
 * texture layer.
 *
 * Return value: whether the texture coordinates will be replaced with
 * point sprite coordinates.
 */
COGL_EXPORT gboolean
cogl_pipeline_get_layer_point_sprite_coords_enabled (CoglPipeline *pipeline,
                                                     int           layer_index);

/**
 * cogl_pipeline_get_layer_wrap_mode_s:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the layer number to change.
 *
 * Returns the wrap mode for the 's' coordinate of texture lookups on this
 * layer.
 *
 * Return value: the wrap mode for the 's' coordinate of texture lookups on
 * this layer.
 */
COGL_EXPORT CoglPipelineWrapMode
cogl_pipeline_get_layer_wrap_mode_s (CoglPipeline *pipeline,
                                     int           layer_index);

/**
 * cogl_pipeline_set_layer_wrap_mode_s:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the layer number to change.
 * @mode: the new wrap mode
 *
 * Sets the wrap mode for the 's' coordinate of texture lookups on this layer.
 */
COGL_EXPORT void
cogl_pipeline_set_layer_wrap_mode_s (CoglPipeline        *pipeline,
                                     int                  layer_index,
                                     CoglPipelineWrapMode mode);

/**
 * cogl_pipeline_get_layer_wrap_mode_t:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the layer number to change.
 *
 * Returns the wrap mode for the 't' coordinate of texture lookups on this
 * layer.
 *
 * Return value: the wrap mode for the 't' coordinate of texture lookups on
 * this layer.
 */
COGL_EXPORT CoglPipelineWrapMode
cogl_pipeline_get_layer_wrap_mode_t (CoglPipeline *pipeline,
                                     int           layer_index);


/**
 * cogl_pipeline_set_layer_wrap_mode_t:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the layer number to change.
 * @mode: the new wrap mode
 *
 * Sets the wrap mode for the 't' coordinate of texture lookups on this layer.
 */
COGL_EXPORT void
cogl_pipeline_set_layer_wrap_mode_t (CoglPipeline        *pipeline,
                                     int                  layer_index,
                                     CoglPipelineWrapMode mode);

/**
 * cogl_pipeline_set_layer_wrap_mode:
 * @pipeline: A #CoglPipeline object
 * @layer_index: the layer number to change.
 * @mode: the new wrap mode
 *
 * Sets the wrap mode for all three coordinates of texture lookups on
 * this layer. This is equivalent to calling
 * cogl_pipeline_set_layer_wrap_mode_s() and
 * cogl_pipeline_set_layer_wrap_mode_t() separately.
 */
COGL_EXPORT void
cogl_pipeline_set_layer_wrap_mode (CoglPipeline        *pipeline,
                                   int                  layer_index,
                                   CoglPipelineWrapMode mode);

/**
 * cogl_pipeline_add_layer_snippet:
 * @pipeline: A #CoglPipeline
 * @layer: The layer to hook the snippet to
 * @snippet: A #CoglSnippet
 *
 * Adds a shader snippet that will hook on to the given layer of the
 * pipeline. The exact part of the pipeline that the snippet wraps
 * around depends on the hook that is given to
 * cogl_snippet_new(). Note that some hooks can't be used with a layer
 * and need to be added with cogl_pipeline_add_snippet() instead.
 */
COGL_EXPORT void
cogl_pipeline_add_layer_snippet (CoglPipeline *pipeline,
                                 int layer,
                                 CoglSnippet *snippet);

COGL_EXPORT void
cogl_pipeline_set_layer_max_mipmap_level (CoglPipeline *pipeline,
                                          int           layer,
                                          int           max_level);

G_END_DECLS
