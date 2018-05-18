#ifndef META_WAYLAND_IDLE_H
#define META_WAYLAND_IDLE_H

#include <wayland-server.h>
#include <glib.h>
#include "idle-inhibit-unstable-v1-server-protocol.h"

#include "meta-wayland-surface.h"
#include "meta-wayland-types.h"

struct _MetaWaylandIdleInhibitor
{
  MetaWaylandSurface	   *surface;
  GDBusProxy 		   *session_proxy;
  guint			   cookie;
  gulong                   inhibit_idle_handler;
  gulong                   restore_idle_handler;
};

typedef struct _MetaWaylandIdleInhibitor MetaWaylandIdleInhibitor;

gboolean
meta_wayland_idle_inhibit_init (MetaWaylandCompositor *compositor);
#endif
