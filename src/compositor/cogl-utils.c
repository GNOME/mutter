/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for use with Cogl
 *
 * Copyright 2010 Red Hat, Inc.
 * Copyright 2010 Intel Corporation
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "cogl-utils.h"

#define CLUTTER_ENABLE_EXPERIMENTAL_API
#include <clutter/clutter.h>

/* Based on gnome-shell/src/st/st-private.c:_st_create_texture_material.c */

/**
 * meta_create_texture_material:
 * @src_texture: (allow-none): texture to use initially for the layer
 *
 * Creates a material with a single layer. Using a common template
 * allows sharing a shader for different uses in Mutter. To share the same
 * shader with all other materials that are just texture plus opacity
 * would require Cogl fixes.
 * (See http://bugzilla.clutter-project.org/show_bug.cgi?id=2425)
 *
 * Return value: (transfer full): a newly created Cogl material
 */
CoglPipeline *
meta_create_texture_material (CoglHandle src_texture)
{
  static CoglPipeline *texture_material_template = NULL;
  CoglPipeline *material;

  if (G_UNLIKELY (texture_material_template == NULL))
    {
      ClutterBackend *backend = clutter_get_default_backend ();
      CoglContext *context = clutter_backend_get_cogl_context (backend);

      texture_material_template = cogl_pipeline_new (context);
      cogl_pipeline_set_layer_null_texture (texture_material_template,
                                            0, COGL_TEXTURE_TYPE_2D);
    }

  material = cogl_pipeline_copy (texture_material_template);

  if (src_texture != COGL_INVALID_HANDLE)
    cogl_pipeline_set_layer_texture (material, 0, src_texture);

  return material;
}

/**
 * meta_create_crossfade_material:
 * @src_texture_0: (allow-none): the texture to crossfade from
 * @src_texture_1: (allow-none): the texture to crossfade to
 *
 * Creates a material with two layers, using a combine constant to
 * crossfade between them.
 *
 * Return value: (transfer full): a newly created Cogl material
 */
CoglPipeline *
meta_create_crossfade_material (CoglHandle src_texture_0,
                                CoglHandle src_texture_1)
{
  static CoglPipeline *texture_material_template = NULL;
  CoglPipeline *material;

  if (G_UNLIKELY (texture_material_template == NULL))
    {
      ClutterBackend *backend = clutter_get_default_backend ();
      CoglContext *context = clutter_backend_get_cogl_context (backend);

      texture_material_template = cogl_pipeline_new (context);

      cogl_pipeline_set_layer_null_texture (texture_material_template,
                                            0, COGL_TEXTURE_TYPE_2D);
      cogl_pipeline_set_layer_null_texture (texture_material_template,
                                            1, COGL_TEXTURE_TYPE_2D);
      cogl_pipeline_set_layer_combine (texture_material_template,
                                       1, "RGBA = INTERPOLATE (TEXTURE, PREVIOUS, CONSTANT[A])",
                                       NULL);
    }

  material = cogl_pipeline_copy (texture_material_template);

  if (src_texture_0 != COGL_INVALID_HANDLE)
    cogl_pipeline_set_layer_texture (material, 0, src_texture_0);
  if (src_texture_1 != COGL_INVALID_HANDLE)
    cogl_pipeline_set_layer_texture (material, 1, src_texture_1);

  return material;
}
