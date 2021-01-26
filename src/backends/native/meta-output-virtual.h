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

#ifndef META_OUTPUT_VIRTUAL_H
#define META_OUTPUT_VIRTUAL_H

#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-output-native.h"

#define META_TYPE_OUTPUT_VIRTUAL (meta_output_virtual_get_type ())
G_DECLARE_FINAL_TYPE (MetaOutputVirtual, meta_output_virtual,
                      META, OUTPUT_VIRTUAL,
                      MetaOutputNative)

MetaOutputVirtual * meta_output_virtual_new (uint64_t                      id,
                                             const MetaVirtualMonitorInfo *info,
                                             MetaCrtcVirtual              *crtc_virtual,
                                             MetaCrtcModeVirtual          *crtc_mode_virtual);

#endif /* META_OUTPUT_VIRTUAL_H */
