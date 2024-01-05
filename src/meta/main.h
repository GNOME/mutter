/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter main */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#include <glib.h>

#include "meta/common.h"
#include "meta/meta-context.h"

META_EXPORT
void            meta_restart                (const char  *message,
                                             MetaContext *context);

META_EXPORT
gboolean        meta_is_restart             (void);

/**
 * MetaExitCode:
 * @META_EXIT_SUCCESS: Success
 * @META_EXIT_ERROR: Error
 */
typedef enum
{
  META_EXIT_SUCCESS,
  META_EXIT_ERROR
} MetaExitCode;

/* exit immediately */
META_EXPORT
void meta_exit (MetaExitCode code) G_GNUC_NORETURN;
