/*
 * Copyright (C) 2017,2018 Red Hat
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

void clutter_input_focus_focus_in  (ClutterInputFocus  *focus,
                                    ClutterInputMethod *method);
void clutter_input_focus_focus_out (ClutterInputFocus  *focus);

void clutter_input_focus_commit (ClutterInputFocus *focus,
                                 const gchar       *text);
void clutter_input_focus_request_surrounding (ClutterInputFocus *focus);
