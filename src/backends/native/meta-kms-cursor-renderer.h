/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef META_KMS_CURSOR_RENDERER_H
#define META_KMS_CURSOR_RENDERER_H

#include "backends/meta-cursor-renderer.h"
#include "meta/meta-backend.h"

#define META_TYPE_KMS_CURSOR_RENDERER (meta_kms_cursor_renderer_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsCursorRenderer, meta_kms_cursor_renderer,
                      META, KMS_CURSOR_RENDERER,
                      GObject)

MetaKmsCursorRenderer * meta_kms_cursor_renderer_new (MetaBackend *backend);
void meta_kms_cursor_renderer_invalidate_state (MetaKmsCursorRenderer *kms_renderer);
gboolean meta_kms_cursor_renderer_update_cursor (MetaKmsCursorRenderer *kms_cursor_renderer,
                                                 MetaCursorSprite      *sprite);
void meta_kms_cursor_renderer_set_cursor_renderer (MetaKmsCursorRenderer *kms_renderer,
                                                   MetaCursorRenderer    *renderer);

#endif /* META_KMS_CURSOR_RENDERER_H */
