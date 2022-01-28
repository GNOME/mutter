/*
 * Copyright (C) 2019-2021 Red Hat Inc.
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
 */

#ifndef META_PRIVATE_ENUMS_H
#define META_PRIVATE_ENUMS_H

typedef enum _MetaX11DisplayPolicy
{
  META_X11_DISPLAY_POLICY_MANDATORY,
  META_X11_DISPLAY_POLICY_ON_DEMAND,
  META_X11_DISPLAY_POLICY_DISABLED,
} MetaX11DisplayPolicy;

typedef enum
{
  META_QUEUE_CALC_SHOWING = 1 << 0,
  META_QUEUE_MOVE_RESIZE = 1 << 1,
} MetaQueueType;

#endif /* META_PRIVATE_ENUMS_H */
