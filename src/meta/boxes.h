/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Simple box operations */

/*
 * Copyright (C) 2005, 2006 Elijah Newren
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

#include "meta/common.h"

/**
 * MetaStrut:
 * @rect: #MtkRectangle the #MetaStrut is on
 * @side: #MetaSide the #MetaStrut is on
 */
typedef struct _MetaStrut MetaStrut;
struct _MetaStrut
{
  MtkRectangle rect;
  MetaSide side;
};

/**
 * MetaEdgeType:
 * @META_EDGE_WINDOW: Whether the edge belongs to a window
 * @META_EDGE_MONITOR: Whether the edge belongs to a monitor
 * @META_EDGE_SCREEN: Whether the edge belongs to a screen
 */
typedef enum
{
  META_EDGE_WINDOW,
  META_EDGE_MONITOR,
  META_EDGE_SCREEN
} MetaEdgeType;

/**
 * MetaEdge:
 * @rect: #MtkRectangle with the bounds of the edge
 * @side_type: Side
 * @edge_type: To what belongs the edge
 */
typedef struct _MetaEdge MetaEdge;
struct _MetaEdge
{
  MtkRectangle rect;      /* width or height should be 1 */
  MetaSide side_type;
  MetaEdgeType  edge_type;
};
