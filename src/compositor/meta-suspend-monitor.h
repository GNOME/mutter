/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright © 2019 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __META_SUSPEND_MONITOR_H__
#define __META_SUSPEND_MONITOR_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS
#define META_TYPE_SUSPEND_MONITOR             (meta_suspend_monitor_get_type ())
#define META_SUSPEND_MONITOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_SUSPEND_MONITOR, MetaSuspendMonitor))
#define META_SUSPEND_MONITOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_SUSPEND_MONITOR, MetaSuspendMonitorClass))
#define META_IS_SUSPEND_MONITOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_SUSPEND_MONITOR))
#define META_IS_SUSPEND_MONITOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_SUSPEND_MONITOR))
#define META_SUSPEND_MONITOR_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), META_TYPE_SUSPEND_MONITOR, MetaSuspendMonitorClass))
typedef struct _MetaSuspendMonitor MetaSuspendMonitor;
typedef struct _MetaSuspendMonitorClass MetaSuspendMonitorClass;
typedef struct _MetaSuspendMonitorPrivate MetaSuspendMonitorPrivate;

struct _MetaSuspendMonitor
{
  GObject parent;

  MetaSuspendMonitorPrivate *priv;
};

struct _MetaSuspendMonitorClass
{
  GObjectClass parent_class;
};

GType meta_suspend_monitor_get_type (void);

MetaSuspendMonitor *meta_suspend_monitor_new (void);
G_END_DECLS
#endif /* __META_SUSPEND_MONITOR_H__ */
