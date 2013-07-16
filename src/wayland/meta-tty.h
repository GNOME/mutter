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

#ifndef META_TTY_H
#define META_TTY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define META_TYPE_TTY              (meta_tty_get_type())
#define META_TTY(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_TTY, MetaTTY))
#define META_TTY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_TTY, MetaTTYClass))
#define META_IS_TTY(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_TTY))
#define META_IS_TTY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_TTY))
#define META_TTY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TTY, MetaTTYClass))

typedef struct _MetaTTY      MetaTTY;
typedef struct _MetaTTYClass MetaTTYClass;

GType             meta_tty_get_type                (void) G_GNUC_CONST;

MetaTTY          *meta_tty_new                     (void);

gboolean          meta_tty_activate_vt             (MetaTTY  *self,
						    int       number,
						    GError  **error);

void              meta_tty_reset                   (MetaTTY  *self,
						    gboolean  warn_if_fail);

G_END_DECLS

#endif /* META_TTY_H */
