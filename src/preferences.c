/*
 *   preferences.c
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

#include <sys/stat.h>
#include "preferences.h"
#include "utils.h"

#define PREF_FILE "/preferences.json"

#define PREF_SHOW_ALL_COLUMNS "showAllColumns"
#define PREF_BLOCKS "blocks"
#define PREF_QUALITY "quality"
#define PREF_TIMEOUT "timeout"
#define PREF_PIPEWIRE_PROPS "pipewireProps"

gint
ow_save_preferences (struct ow_preferences *prefs)
{
  size_t n;
  gchar *preferences_path;
  JsonBuilder *builder;
  JsonGenerator *gen;
  JsonNode *root;

  preferences_path = get_expanded_dir (CONF_DIR);
  if (g_mkdir_with_parents (preferences_path,
			    S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH |
			    S_IXOTH))
    {
      error_print ("Error wile creating dir `%s'", CONF_DIR);
      return -1;
    }

  n = PATH_MAX - strlen (preferences_path) - 1;
  strncat (preferences_path, PREF_FILE, n);
  preferences_path[PATH_MAX - 1] = 0;

  debug_print (1, "Saving preferences to '%s'...", preferences_path);

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, PREF_SHOW_ALL_COLUMNS);
  json_builder_add_boolean_value (builder, prefs->show_all_columns);

  json_builder_set_member_name (builder, PREF_BLOCKS);
  json_builder_add_int_value (builder, prefs->blocks);

  json_builder_set_member_name (builder, PREF_TIMEOUT);
  json_builder_add_int_value (builder, prefs->timeout);

  json_builder_set_member_name (builder, PREF_QUALITY);
  json_builder_add_int_value (builder, prefs->quality);

  json_builder_set_member_name (builder, PREF_PIPEWIRE_PROPS);
  json_builder_add_string_value (builder, prefs->pipewire_props);

  json_builder_end_object (builder);

  gen = json_generator_new ();
  root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
  json_generator_set_pretty (gen, TRUE);
  json_generator_to_file (gen, preferences_path, NULL);

  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);
  g_free (preferences_path);

  return 0;
}

gint
ow_load_preferences (struct ow_preferences *prefs)
{
  gint err = 0;
  GError *error;
  JsonReader *reader;
  JsonParser *parser = json_parser_new ();
  gchar *preferences_file = get_expanded_dir (CONF_DIR PREF_FILE);

  prefs->blocks = 24;
  prefs->quality = 2;
  prefs->timeout = 10;
  prefs->show_all_columns = FALSE;
  prefs->pipewire_props = NULL;

  error = NULL;
  json_parser_load_from_file (parser, preferences_file, &error);
  if (error)
    {
      debug_print (0,
		   "Error wile loading preferences from '%s': %s. Using defaults...",
		   CONF_DIR PREF_FILE, error->message);
      g_error_free (error);
      err = -1;
      goto end;
    }

  reader = json_reader_new (json_parser_get_root (parser));

  if (json_reader_read_member (reader, PREF_SHOW_ALL_COLUMNS))
    {
      prefs->show_all_columns = json_reader_get_boolean_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, PREF_BLOCKS))
    {
      prefs->blocks = json_reader_get_int_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, PREF_TIMEOUT))
    {
      prefs->timeout = json_reader_get_int_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, PREF_QUALITY))
    {
      prefs->quality = json_reader_get_int_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, PREF_PIPEWIRE_PROPS))
    {
      const gchar *v = json_reader_get_string_value (reader);
      prefs->pipewire_props = strdup (v ? v : NULL);
    }
  json_reader_end_member (reader);

  g_object_unref (reader);

end:
  g_object_unref (parser);
  g_free (preferences_file);

  return err;
}
