/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat, Inc.
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

#ifndef META_BACKEND_TEST_H
#define META_BACKEND_TEST_H

#include "backends/x11/nested/meta-backend-x11-nested.h"

#define META_TYPE_BACKEND_TEST (meta_backend_test_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaBackendTest, meta_backend_test,
                      META, BACKEND_TEST, MetaBackendX11Nested)

META_EXPORT
void meta_backend_test_set_is_lid_closed (MetaBackendTest *backend_test,
                                          gboolean         is_lid_closed);

META_EXPORT
MetaGpu * meta_backend_test_get_gpu (MetaBackendTest *backend_test);

META_EXPORT_TEST
ClutterInputDevice * meta_backend_test_add_test_device (MetaBackendTest        *backend,
                                                        const char             *name,
                                                        ClutterInputDeviceType  device_type,
                                                        int                     n_buttons);

META_EXPORT_TEST
void meta_backend_test_remove_device (MetaBackendTest    *backend,
                                      ClutterInputDevice *device);


#endif /* META_BACKEND_TEST_H */
