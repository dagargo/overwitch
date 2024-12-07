/*
 *   config.c
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
#include "config.h"
#include "utils.h"

#define CONF_FILE "/preferences.json"

#define CONF_REFRESH_AT_STARTUP "refreshAtStartup"
#define CONF_SHOW_ALL_COLUMNS "showAllColumns"
#define CONF_BLOCKS "blocks"
#define CONF_QUALITY "quality"
#define CONF_TIMEOUT "timeout"
#define CONF_PIPEWIRE_PROPS "pipewireProps"

static gint
save_file_char (const gchar *path, const guint8 *data, ssize_t len)
{
  gint res;
  long bytes;
  FILE *file;

  file = fopen (path, "w");

  if (!file)
    {
      return -errno;
    }

  debug_print (1, "Saving file %s...", path);

  res = 0;
  bytes = fwrite (data, 1, len, file);
  if (bytes == len)
    {
      debug_print (1, "%zu bytes written", bytes);
    }
  else
    {
      error_print ("Error while writing to file %s", path);
      res = -EIO;
    }

  fclose (file);

  return res;
}

gint
ow_save_config (struct ow_config *config)
{
  size_t n;
  gchar *preferences_path;
  JsonBuilder *builder;
  JsonGenerator *gen;
  JsonNode *root;
  gchar *json;

  preferences_path = get_expanded_dir (CONF_DIR);
  if (g_mkdir_with_parents (preferences_path,
			    S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH |
			    S_IXOTH))
    {
      error_print ("Error wile creating dir `%s'", CONF_DIR);
      return -1;
    }

  n = PATH_MAX - strlen (preferences_path) - 1;
  strncat (preferences_path, CONF_FILE, n);
  preferences_path[PATH_MAX - 1] = 0;

  debug_print (1, "Saving preferences to '%s'...", preferences_path);

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, CONF_REFRESH_AT_STARTUP);
  json_builder_add_boolean_value (builder, config->refresh_at_startup);

  json_builder_set_member_name (builder, CONF_SHOW_ALL_COLUMNS);
  json_builder_add_boolean_value (builder, config->show_all_columns);

  json_builder_set_member_name (builder, CONF_BLOCKS);
  json_builder_add_int_value (builder, config->blocks);

  json_builder_set_member_name (builder, CONF_TIMEOUT);
  json_builder_add_int_value (builder, config->timeout);

  json_builder_set_member_name (builder, CONF_QUALITY);
  json_builder_add_int_value (builder, config->quality);

  json_builder_set_member_name (builder, CONF_PIPEWIRE_PROPS);
  json_builder_add_string_value (builder, config->pipewire_props);

  json_builder_end_object (builder);

  gen = json_generator_new ();
  root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
  json_generator_set_pretty (gen, TRUE);
  json = json_generator_to_data (gen, NULL);

  save_file_char (preferences_path, (guint8 *) json, strlen (json));

  g_free (json);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);
  g_free (preferences_path);

  return 0;
}

gint
ow_load_config (struct ow_config *config)
{
  GError *error;
  JsonReader *reader;
  JsonParser *parser = json_parser_new ();
  gchar *preferences_file = get_expanded_dir (CONF_DIR CONF_FILE);

  config->blocks = 24;
  config->quality = 2;
  config->timeout = 10;
  config->refresh_at_startup = TRUE;
  config->show_all_columns = FALSE;
  config->pipewire_props = NULL;

  error = NULL;
  json_parser_load_from_file (parser, preferences_file, &error);
  if (error)
    {
      error_print ("Error wile loading preferences from `%s': %s",
		   CONF_DIR CONF_FILE, error->message);
      g_error_free (error);
      g_object_unref (parser);
      return -1;
    }

  reader = json_reader_new (json_parser_get_root (parser));

  if (json_reader_read_member (reader, CONF_REFRESH_AT_STARTUP))
    {
      config->refresh_at_startup = json_reader_get_boolean_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_SHOW_ALL_COLUMNS))
    {
      config->show_all_columns = json_reader_get_boolean_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_BLOCKS))
    {
      config->blocks = json_reader_get_int_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_TIMEOUT))
    {
      config->timeout = json_reader_get_int_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_QUALITY))
    {
      config->quality = json_reader_get_int_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_PIPEWIRE_PROPS))
    {
      const gchar *v = json_reader_get_string_value (reader);
      if (v && strlen (v))
	{
	  config->pipewire_props = strdup (v);
	}
    }
  json_reader_end_member (reader);

  g_object_unref (reader);
  g_object_unref (parser);

  g_free (preferences_file);

  return 0;
}
