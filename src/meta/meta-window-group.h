/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#pragma once

#include "clutter/clutter.h"

#define META_TYPE_WINDOW_GROUP (meta_window_group_get_type())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaWindowGroup,
                      meta_window_group,
                      META, WINDOW_GROUP,
                      ClutterActor)
