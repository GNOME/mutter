/*
 * Copyright (C) 2019 Red Hat Inc.
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

#ifndef META_CONTEXT_PRIVATE_H
#define META_CONTEXT_PRIVATE_H

#include "meta/meta-backend.h"
#include "meta/meta-context.h"
#include "wayland/meta-wayland-types.h"

struct _MetaContextClass
{
  GObjectClass parent_class;

  gboolean (* configure) (MetaContext   *context,
                          int           *argc,
                          char        ***argv,
                          GError       **error);

  MetaCompositorType (* get_compositor_type) (MetaContext *context);

  gboolean (* setup) (MetaContext  *context,
                      GError      **error);

  MetaBackend * (* create_backend) (MetaContext  *context,
                                    GError      **error);

  void (* notify_ready) (MetaContext *context);
};

const char * meta_context_get_name (MetaContext *context);

MetaWaylandCompositor * meta_context_get_wayland_compositor (MetaContext *context);

#endif /* META_CONTEXT_PRIVATE_H */
