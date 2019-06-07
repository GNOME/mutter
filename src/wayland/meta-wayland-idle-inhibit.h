#ifndef META_WAYLAND_IDLE_INHIBIT_H
#define META_WAYLAND_IDLE_INHIBIT_H

#include <wayland-server.h>
#include <glib.h>
#include "idle-inhibit-unstable-v1-server-protocol.h"

#include "meta-wayland-surface.h"
#include "meta-wayland-types.h"

gboolean
meta_wayland_idle_inhibit_init (MetaWaylandCompositor *compositor);
#endif
