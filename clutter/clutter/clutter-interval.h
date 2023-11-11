/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008  Intel Corporation.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_INTERVAL                   (clutter_interval_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterInterval,
                          clutter_interval,
                          CLUTTER,
                          INTERVAL,
                          GInitiallyUnowned)

/**
 * ClutterIntervalClass:
 * @validate: virtual function for validating an interval
 *   using a #GParamSpec
 * @compute_value: virtual function for computing the value
 *   inside an interval using an adimensional factor between 0 and 1
 *
 * The #ClutterIntervalClass contains only private data.
 */
struct _ClutterIntervalClass
{
  /*< private >*/
  GInitiallyUnownedClass parent_class;

  /*< public >*/
  gboolean (* validate)      (ClutterInterval *interval,
                              GParamSpec      *pspec);
  gboolean (* compute_value) (ClutterInterval *interval,
                              gdouble          factor,
                              GValue          *value);
};

CLUTTER_EXPORT
ClutterInterval *clutter_interval_new                (GType            gtype,
                                                      ...);
CLUTTER_EXPORT
ClutterInterval *clutter_interval_new_with_values    (GType            gtype,
                                                      const GValue    *initial,
                                                      const GValue    *final);

CLUTTER_EXPORT
ClutterInterval *clutter_interval_clone              (ClutterInterval *interval);

CLUTTER_EXPORT
GType            clutter_interval_get_value_type     (ClutterInterval *interval);

CLUTTER_EXPORT
void             clutter_interval_set_initial        (ClutterInterval *interval,
                                                      ...);
CLUTTER_EXPORT
void             clutter_interval_set_initial_value  (ClutterInterval *interval,
                                                      const GValue    *value);
CLUTTER_EXPORT
void             clutter_interval_get_initial_value  (ClutterInterval *interval,
                                                      GValue          *value);
CLUTTER_EXPORT
GValue *         clutter_interval_peek_initial_value (ClutterInterval *interval);
CLUTTER_EXPORT
void             clutter_interval_set_final          (ClutterInterval *interval,
                                                      ...);
CLUTTER_EXPORT
void             clutter_interval_set_final_value    (ClutterInterval *interval,
                                                      const GValue    *value);
CLUTTER_EXPORT
void             clutter_interval_get_final_value    (ClutterInterval *interval,
                                                      GValue          *value);
CLUTTER_EXPORT
GValue *         clutter_interval_peek_final_value   (ClutterInterval *interval);

CLUTTER_EXPORT
void             clutter_interval_set_interval       (ClutterInterval *interval,
                                                      ...);
CLUTTER_EXPORT
void             clutter_interval_get_interval       (ClutterInterval *interval,
                                                      ...);

CLUTTER_EXPORT
gboolean         clutter_interval_validate           (ClutterInterval *interval,
                                                      GParamSpec      *pspec);
CLUTTER_EXPORT
gboolean         clutter_interval_compute_value      (ClutterInterval *interval,
                                                      gdouble          factor,
                                                      GValue          *value);

CLUTTER_EXPORT
const GValue *   clutter_interval_compute            (ClutterInterval *interval,
                                                      gdouble          factor);

CLUTTER_EXPORT
gboolean         clutter_interval_is_valid           (ClutterInterval *interval);

G_END_DECLS
