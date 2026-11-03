/*
 *   layout.h
 *   Copyright (C) 2026 Zach Denton <z@chdenton.com>
 *
 *   This file is part of Overwitch.
 *
 *   Overwitch is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Overwitch is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Overwitch. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gtk/gtk.h>

typedef void (*layout_editor_saved_cb_t) (void);

//Opens the track layout editor of an Overbridge 1 device. The layout is saved
//in its device definition and the callback is called afterwards.
void layout_editor_open (GtkWindow * parent, guint16 pid,
			 const gchar * device, layout_editor_saved_cb_t saved_cb);
