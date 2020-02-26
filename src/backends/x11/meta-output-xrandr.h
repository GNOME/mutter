/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
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

#ifndef META_OUTPUT_XRANDR_H
#define META_OUTPUT_XRANDR_H

#include <X11/extensions/Xrandr.h>

#include "backends/meta-output.h"
#include "backends/x11/meta-gpu-xrandr.h"
#include "backends/x11/meta-monitor-manager-xrandr.h"

#define META_TYPE_OUTPUT_XRANDR (meta_output_xrandr_get_type ())
G_DECLARE_FINAL_TYPE (MetaOutputXrandr, meta_output_xrandr,
                      META, OUTPUT_XRANDR,
                      MetaOutput)

void meta_output_xrandr_apply_mode (MetaOutputXrandr *output_xrandr);

void meta_output_xrandr_change_backlight (MetaOutputXrandr *output_xrandr,
                                          int         value);

GBytes * meta_output_xrandr_read_edid (MetaOutput *output_xrandr);

MetaOutputXrandr * meta_output_xrandr_new (MetaGpuXrandr *gpu_xrandr,
                                           XRROutputInfo *xrandr_output,
                                           RROutput       output_id,
                                           RROutput       primary_output);

#endif /* META_OUTPUT_XRANDR_H */
