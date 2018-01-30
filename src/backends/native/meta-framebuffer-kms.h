/*
 * Copyright © 2018 Canonical Ltd.
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
 * Author: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#ifndef META_FRAMEBUFFER_KMS_H
#define META_FRAMEBUFFER_KMS_H

#include <glib-object.h>
#include <gbm.h>

#define META_TYPE_FRAMEBUFFER_KMS (meta_framebuffer_kms_get_type ())
G_DECLARE_FINAL_TYPE (MetaFramebufferKms, meta_framebuffer_kms, META, FRAMEBUFFER_KMS, GObject)

void meta_framebuffer_kms_set_drm_fd (MetaFramebufferKms *fb_kms,
                                      int                 drm_fd);

void meta_framebuffer_kms_set_gbm_surface (MetaFramebufferKms *fb_kms,
                                           struct gbm_surface *gbm_surface);

gboolean meta_framebuffer_kms_acquire_swapped_buffer (MetaFramebufferKms *fb_kms,
                                                      gboolean use_modifiers);
void meta_framebuffer_kms_borrow_dumb_buffer (MetaFramebufferKms *fb_kms,
                                              uint32_t            dumb_fb_id);

uint32_t meta_framebuffer_kms_get_fb_id (const MetaFramebufferKms *fb_kms);

struct gbm_bo *meta_framebuffer_kms_get_bo (const MetaFramebufferKms *fb_kms);

void meta_framebuffer_kms_release_buffer (MetaFramebufferKms *fb_kms);

#endif /* META_FRAMEBUFFER_KMS_H */
