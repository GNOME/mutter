/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#pragma once

#include "meta/display.h"
#include "meta/meta-window-group.h"

/**
 * MetaWindowGroup:
 *
 * This class is a subclass of ClutterActor with special handling for
 * #MetaCullable when painting children. It uses code similar to
 * meta_cullable_cull_out_children(), but also has additional special
 * cases for the undirected window, and similar.
 */


typedef struct _MetaWindowGroupPrivate MetaWindowGroupPrivate;

ClutterActor *meta_window_group_new (MetaDisplay *display);
