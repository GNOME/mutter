/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; c-basic-offset: 2; -*- */

/* 
 * Copyright 2012, 2013 Red Hat Inc.
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
 * Authors: Jaster St. Pierre <jstpierr@redhat.com>
 *          Giovanni Campagna <gcampagn@redhat.com>
 */

#ifndef BARRIER_PRIVATE_H
#define BARRIER_PRIVATE_H

typedef struct _MetaBarrierManager MetaBarrierManager;

MetaBarrierManager *meta_barrier_manager_get (void);

void meta_barrier_manager_constrain_cursor (MetaBarrierManager *manager,
					    guint32             time,
					    float               current_x,
					    float               current_y,
					    float              *new_x,
					    float              *new_y);

#endif
