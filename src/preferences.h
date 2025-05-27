/*
 *   preferences.h
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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

#include <glib.h>

struct ow_preferences
{
  gboolean show_all_columns;
  gint64 blocks_ob1;
  gint64 blocks_ob2;
  gint64 timeout;
  gint64 quality;
  gchar *pipewire_props;
};

gint ow_load_preferences (struct ow_preferences *preferences);

gint ow_save_preferences (struct ow_preferences *preferences);
