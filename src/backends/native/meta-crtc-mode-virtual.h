/*
 * Copyright (C) 2021 Red Hat
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

#ifndef META_CRTC_MODE_VIRTUAL_H
#define META_CRTC_MODE_VIRTUAL_H

#include "backends/meta-backend-types.h"
#include "backends/meta-crtc-mode.h"

#define META_TYPE_CRTC_MODE_VIRTUAL (meta_crtc_mode_virtual_get_type ())
G_DECLARE_FINAL_TYPE (MetaCrtcModeVirtual, meta_crtc_mode_virtual,
                      META, CRTC_MODE_VIRTUAL,
                      MetaCrtcMode)

MetaCrtcModeVirtual * meta_crtc_mode_virtual_new (uint64_t                      id,
                                                  const MetaVirtualMonitorInfo *info);

#endif /* META_CRTC_MODE_VIRTUAL_H */
