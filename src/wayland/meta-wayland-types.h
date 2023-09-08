/*
 * Copyright (C) 2013 Red Hat, Inc.
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

typedef struct _MetaWaylandCompositor MetaWaylandCompositor;

typedef struct _MetaWaylandSeat MetaWaylandSeat;
typedef struct _MetaWaylandInputDevice MetaWaylandInputDevice;
typedef struct _MetaWaylandPointer MetaWaylandPointer;
typedef struct _MetaWaylandPopupGrab MetaWaylandPopupGrab;
typedef struct _MetaWaylandPopup MetaWaylandPopup;
typedef struct _MetaWaylandPopupSurface MetaWaylandPopupSurface;
typedef struct _MetaWaylandKeyboard MetaWaylandKeyboard;
typedef struct _MetaWaylandTouch MetaWaylandTouch;
typedef struct _MetaWaylandDragDestFuncs MetaWaylandDragDestFuncs;
typedef struct _MetaWaylandDataOffer MetaWaylandDataOffer;
typedef struct _MetaWaylandDataDevice MetaWaylandDataDevice;
typedef struct _MetaWaylandDataDevicePrimary MetaWaylandDataDevicePrimary;

typedef struct _MetaWaylandTabletManager MetaWaylandTabletManager;
typedef struct _MetaWaylandTabletSeat MetaWaylandTabletSeat;
typedef struct _MetaWaylandTabletTool MetaWaylandTabletTool;
typedef struct _MetaWaylandTablet MetaWaylandTablet;
typedef struct _MetaWaylandTabletPad MetaWaylandTabletPad;
typedef struct _MetaWaylandTabletPadGroup MetaWaylandTabletPadGroup;
typedef struct _MetaWaylandTabletPadStrip MetaWaylandTabletPadStrip;
typedef struct _MetaWaylandTabletPadRing MetaWaylandTabletPadRing;

typedef struct _MetaWaylandBuffer MetaWaylandBuffer;
typedef struct _MetaWaylandRegion MetaWaylandRegion;

typedef struct _MetaWaylandSurface MetaWaylandSurface;
typedef struct _MetaWaylandSurfaceState MetaWaylandSurfaceState;

typedef struct _MetaWaylandTransaction MetaWaylandTransaction;
typedef struct _MetaWaylandTransactionEntry MetaWaylandTransactionEntry;

typedef struct _MetaWaylandOutput MetaWaylandOutput;

typedef struct _MetaWaylandWindowConfiguration MetaWaylandWindowConfiguration;

typedef struct _MetaWaylandPointerClient MetaWaylandPointerClient;

typedef struct _MetaWaylandActivation MetaWaylandActivation;

typedef struct _MetaWaylandDmaBufManager MetaWaylandDmaBufManager;

typedef struct _MetaWaylandSyncobjTimeline MetaWaylandSyncobjTimeline;

typedef struct _MetaWaylandXdgPositioner MetaWaylandXdgPositioner;

typedef struct _MetaXWaylandManager MetaXWaylandManager;

typedef struct _MetaWaylandXdgForeign MetaWaylandXdgForeign;

typedef struct _MetaWaylandFilterManager MetaWaylandFilterManager;

typedef struct _MetaWaylandClient MetaWaylandClient;
