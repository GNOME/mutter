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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_WESTON_LAUNCH_H
#define META_WESTON_LAUNCH_H

#include <glib-object.h>

/* Keep this in sync with weston-launch */

enum weston_launcher_opcode {
	WESTON_LAUNCHER_OPEN,
	WESTON_LAUNCHER_DRM_SET_MASTER
};

struct weston_launcher_message {
	int opcode;
};

struct weston_launcher_open {
	struct weston_launcher_message header;
	int flags;
	char path[0];
};

struct weston_launcher_set_master {
	struct weston_launcher_message header;
	int set_master;
};

gboolean meta_weston_launch_set_master (GSocket   *weston_launch,
					int        drm_fd,
					gboolean   master,
					GError   **error);
int      meta_weston_launch_open_input_device (GSocket     *weston_launch,
					       const char  *name,
					       int          flags,
					       GError     **error);

#endif
