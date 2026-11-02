/*
 *   layout.c
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

#include <glib/gi18n.h>
#include <json-glib/json-glib.h>
#include "layout.h"
#include "utils.h"

#define DEVICES_DIR "/devices.d"
#define DEVICES_FILE "/devices.json"

#define DEV_TAG_PID "pid"
#define DEV_TAG_TRACK_NAME "name"
#define DEV_TAG_TRACK_SIZE "size"
#define DEV_TAG_TRACK_CODE "code"

//These are the limits of the Overbridge Control Panel.
#define MAX_TRACKS_16_BITS 10
#define MAX_TRACKS_24_BITS 6
#define MAX_TRACKS_PER_DIRECTION 8

//Output tracks go from the device to the host and input tracks the other way.
static const gchar *TRACKS_MEMBERS[] = { "output_tracks", "input_tracks" };
static const gchar *AVAILABLE_TRACKS_MEMBERS[] = {
  "available_output_tracks", "available_input_tracks"
};

static const gchar *DEPTHS[] = { "16-bit", "24-bit", NULL };

struct layout_editor
{
  GtkWidget *window;
  GtkWidget *depth_drop_downs[2];
  GPtrArray *check_buttons[2];
  GtkWidget *status_label;
  GtkWidget *save_button;
  //The device definition, which is modified and saved.
  JsonNode *definition;
  gchar *path;
  layout_editor_saved_cb_t saved_cb;
};

static JsonNode *
get_definition_from_node (JsonNode *node, guint16 pid)
{
  JsonObject *object;

  if (!JSON_NODE_HOLDS_OBJECT (node))
    {
      return NULL;
    }

  object = json_node_get_object (node);
  if (json_object_get_int_member_with_default (object, DEV_TAG_PID, -1) !=
      pid)
    {
      return NULL;
    }

  return json_node_copy (node);
}

static JsonNode *
get_definition_from_file (const gchar *file, guint16 pid, gboolean array)
{
  JsonNode *root, *definition = NULL;
  JsonParser *parser = json_parser_new ();

  if (!json_parser_load_from_file (parser, file, NULL))
    {
      g_object_unref (parser);
      return NULL;
    }

  root = json_parser_get_root (parser);
  if (array && JSON_NODE_HOLDS_ARRAY (root))
    {
      JsonArray *devices = json_node_get_array (root);
      for (guint i = 0; i < json_array_get_length (devices) && !definition;
	   i++)
	{
	  definition =
	    get_definition_from_node (json_array_get_element (devices, i),
				      pid);
	}
    }
  else if (!array)
    {
      definition = get_definition_from_node (root, pid);
    }

  g_object_unref (parser);
  return definition;
}

//The search order is the same Overwitch uses. If the definition is not in the
//user devices directory, it will be saved there as it takes precedence.
static JsonNode *
get_definition (guint16 pid, gchar **path)
{
  GDir *gdir;
  const gchar *name;
  gchar *file;
  JsonNode *definition = NULL;
  gchar *dir = get_expanded_dir (CONF_DIR DEVICES_DIR);
  gchar *user_file = get_expanded_dir (CONF_DIR DEVICES_FILE);
  const gchar *array_files[] = { user_file, DATADIR DEVICES_FILE };

  gdir = g_dir_open (dir, 0, NULL);
  if (gdir)
    {
      while (!definition && (name = g_dir_read_name (gdir)) != NULL)
	{
	  if (name[0] == '.' || !g_str_has_suffix (name, ".json"))
	    {
	      continue;
	    }

	  file = g_build_filename (dir, name, NULL);
	  definition = get_definition_from_file (file, pid, FALSE);
	  if (definition)
	    {
	      *path = file;
	    }
	  else
	    {
	      g_free (file);
	    }
	}
      g_dir_close (gdir);
    }

  for (int i = 0; i < G_N_ELEMENTS (array_files) && !definition; i++)
    {
      definition = get_definition_from_file (array_files[i], pid, TRUE);
      if (definition)
	{
	  gchar *basename = g_strdup_printf ("%u.json", pid);
	  *path = g_build_filename (dir, basename, NULL);
	  g_free (basename);
	}
    }

  free (dir);
  free (user_file);

  return definition;
}

static JsonArray *
get_definition_array (struct layout_editor *editor, const gchar *member)
{
  JsonObject *object = json_node_get_object (editor->definition);
  if (!json_object_has_member (object, member))
    {
      return NULL;
    }
  JsonNode *node = json_object_get_member (object, member);
  return JSON_NODE_HOLDS_ARRAY (node) ? json_node_get_array (node) : NULL;
}

static void
update_status (struct layout_editor *editor)
{
  gchar *text;
  gint tracks[2] = { 0, 0 };
  gint limit = MAX_TRACKS_16_BITS;
  gboolean valid;

  for (int i = 0; i < 2; i++)
    {
      GPtrArray *buttons = editor->check_buttons[i];
      for (guint j = 0; j < buttons->len; j++)
	{
	  if (gtk_check_button_get_active
	      (GTK_CHECK_BUTTON (g_ptr_array_index (buttons, j))))
	    {
	      tracks[i]++;
	    }
	}

      if (gtk_drop_down_get_selected
	  (GTK_DROP_DOWN (editor->depth_drop_downs[i])) == 1)
	{
	  limit = MAX_TRACKS_24_BITS;
	}
    }

  if (!tracks[0] || !tracks[1])
    {
      text = g_strdup (_("At least one output and one input are needed"));
      valid = FALSE;
    }
  else if (tracks[0] > MAX_TRACKS_PER_DIRECTION ||
	   tracks[1] > MAX_TRACKS_PER_DIRECTION)
    {
      text = g_strdup_printf (_("At most %d tracks per direction"),
			      MAX_TRACKS_PER_DIRECTION);
      valid = FALSE;
    }
  else
    {
      text = g_strdup_printf (_("%d of %d tracks"), tracks[0] + tracks[1],
			      limit);
      valid = tracks[0] + tracks[1] <= limit;
    }

  gtk_label_set_text (GTK_LABEL (editor->status_label), text);
  gtk_widget_set_sensitive (editor->save_button, valid);
  g_free (text);
}

static void
on_check_toggled (GtkCheckButton *button, gpointer data)
{
  update_status (data);
}

static void
on_depth_selected (GObject *object, GParamSpec *pspec, gpointer data)
{
  update_status (data);
}

static void
free_editor (struct layout_editor *editor)
{
  for (int i = 0; i < 2; i++)
    {
      g_ptr_array_unref (editor->check_buttons[i]);
    }
  json_node_unref (editor->definition);
  g_free (editor->path);
  g_free (editor);
}

static void
close_editor (struct layout_editor *editor)
{
  gtk_window_destroy (GTK_WINDOW (editor->window));
}

static void
on_cancel_clicked (GtkButton *button, gpointer data)
{
  close_editor (data);
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller, guint keyval,
		guint keycode, GdkModifierType state, gpointer data)
{
  if (keyval == GDK_KEY_Escape)
    {
      close_editor (data);
      return TRUE;
    }

  return FALSE;
}

static void
on_window_destroy (GtkWidget *widget, gpointer data)
{
  free_editor (data);
}

static gboolean
save_definition (struct layout_editor *editor)
{
  gchar *dir;
  gboolean ok;
  GError *error = NULL;
  JsonGenerator *generator;
  JsonObject *object = json_node_get_object (editor->definition);

  for (int i = 0; i < 2; i++)
    {
      JsonArray *available =
	get_definition_array (editor, AVAILABLE_TRACKS_MEMBERS[i]);
      JsonArray *tracks = json_array_new ();
      gint size = gtk_drop_down_get_selected
	(GTK_DROP_DOWN (editor->depth_drop_downs[i])) == 1 ? 3 : 2;

      //The order of the available tracks is kept.
      for (guint j = 0; j < editor->check_buttons[i]->len; j++)
	{
	  GtkCheckButton *button =
	    g_ptr_array_index (editor->check_buttons[i], j);
	  if (!gtk_check_button_get_active (button))
	    {
	      continue;
	    }

	  JsonObject *src = json_array_get_object_element (available, j);
	  JsonObject *track = json_object_new ();
	  json_object_set_string_member (track, DEV_TAG_TRACK_NAME,
					 json_object_get_string_member (src,
									DEV_TAG_TRACK_NAME));
	  json_object_set_int_member (track, DEV_TAG_TRACK_SIZE, size);
	  json_object_set_int_member (track, DEV_TAG_TRACK_CODE,
				      json_object_get_int_member (src,
								  DEV_TAG_TRACK_CODE));
	  json_array_add_object_element (tracks, track);
	}

      json_object_set_array_member (object, TRACKS_MEMBERS[i], tracks);
    }

  dir = g_path_get_dirname (editor->path);
  g_mkdir_with_parents (dir, 0755);
  g_free (dir);

  debug_print (1, "Saving the device definition to '%s'...", editor->path);

  generator = json_generator_new ();
  json_generator_set_root (generator, editor->definition);
  json_generator_set_pretty (generator, TRUE);
  ok = json_generator_to_file (generator, editor->path, &error);
  g_object_unref (generator);

  if (!ok)
    {
      error_print ("Error while saving '%s': %s", editor->path,
		   error->message);
      GtkAlertDialog *dialog =
	gtk_alert_dialog_new (_("The track layout could not be saved"));
      gtk_alert_dialog_set_detail (dialog, error->message);
      gtk_alert_dialog_show (dialog, GTK_WINDOW (editor->window));
      g_object_unref (dialog);
      g_error_free (error);
    }

  return ok;
}

static void
on_save_clicked (GtkButton *button, gpointer data)
{
  struct layout_editor *editor = data;
  layout_editor_saved_cb_t saved_cb = editor->saved_cb;

  if (save_definition (editor))
    {
      close_editor (editor);
      saved_cb ();
    }
}

//A track is selected if its code is in the current layout. Definitions
//without codes are matched by name.
static gboolean
is_track_selected (JsonArray *tracks, JsonObject *available)
{
  if (!tracks)
    {
      return FALSE;
    }

  for (guint i = 0; i < json_array_get_length (tracks); i++)
    {
      JsonObject *track = json_array_get_object_element (tracks, i);
      if (json_object_has_member (track, DEV_TAG_TRACK_CODE))
	{
	  if (json_object_get_int_member (track, DEV_TAG_TRACK_CODE) ==
	      json_object_get_int_member (available, DEV_TAG_TRACK_CODE))
	    {
	      return TRUE;
	    }
	}
      else if (!g_strcmp0
	       (json_object_get_string_member_with_default
		(track, DEV_TAG_TRACK_NAME, NULL),
		json_object_get_string_member_with_default (available,
							    DEV_TAG_TRACK_NAME,
							    NULL)))
	{
	  return TRUE;
	}
    }

  return FALSE;
}

static GtkWidget *
create_direction (struct layout_editor *editor, int direction,
		  const gchar *title)
{
  GtkWidget *box, *label;
  JsonArray *available =
    get_definition_array (editor, AVAILABLE_TRACKS_MEMBERS[direction]);
  JsonArray *tracks = get_definition_array (editor, TRACKS_MEMBERS[direction]);
  guint selected_depth = 0;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

  label = gtk_label_new (title);
  gtk_widget_add_css_class (label, "heading");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (box), label);

  if (tracks && json_array_get_length (tracks))
    {
      JsonObject *track = json_array_get_object_element (tracks, 0);
      if (json_object_get_int_member_with_default
	  (track, DEV_TAG_TRACK_SIZE, 2) == 3)
	{
	  selected_depth = 1;
	}
    }

  editor->depth_drop_downs[direction] = gtk_drop_down_new_from_strings
    (DEPTHS);
  gtk_drop_down_set_selected (GTK_DROP_DOWN
			      (editor->depth_drop_downs[direction]),
			      selected_depth);
  g_signal_connect (editor->depth_drop_downs[direction], "notify::selected",
		    G_CALLBACK (on_depth_selected), editor);
  gtk_box_append (GTK_BOX (box), editor->depth_drop_downs[direction]);

  editor->check_buttons[direction] = g_ptr_array_new ();
  for (guint i = 0; i < json_array_get_length (available); i++)
    {
      JsonObject *track = json_array_get_object_element (available, i);
      GtkWidget *button =
	gtk_check_button_new_with_label
	(json_object_get_string_member_with_default (track,
						     DEV_TAG_TRACK_NAME,
						     "?"));
      gtk_check_button_set_active (GTK_CHECK_BUTTON (button),
				   is_track_selected (tracks, track));
      g_signal_connect (button, "toggled", G_CALLBACK (on_check_toggled),
			editor);
      g_ptr_array_add (editor->check_buttons[direction], button);
      gtk_box_append (GTK_BOX (box), button);
    }

  return box;
}

//Definitions written by users might not list the available tracks, so these
//are taken from other definitions of the device, like the distributed one.
static void
add_available_tracks (struct layout_editor *editor, guint16 pid)
{
  gchar *user_file = get_expanded_dir (CONF_DIR DEVICES_FILE);
  const gchar *array_files[] = { user_file, DATADIR DEVICES_FILE };
  JsonObject *object = json_node_get_object (editor->definition);

  for (int i = 0; i < G_N_ELEMENTS (array_files); i++)
    {
      JsonNode *other = get_definition_from_file (array_files[i], pid, TRUE);
      if (!other)
	{
	  continue;
	}

      JsonObject *other_object = json_node_get_object (other);
      gboolean found = TRUE;
      for (int j = 0; j < 2; j++)
	{
	  found &= json_object_has_member (other_object,
					   AVAILABLE_TRACKS_MEMBERS[j]);
	}

      if (found)
	{
	  for (int j = 0; j < 2; j++)
	    {
	      json_object_set_member (object, AVAILABLE_TRACKS_MEMBERS[j],
				      json_node_copy (json_object_get_member
						      (other_object,
						       AVAILABLE_TRACKS_MEMBERS
						       [j])));
	    }
	}

      json_node_unref (other);

      if (found)
	{
	  break;
	}
    }

  free (user_file);
}

//Every available track needs a name and a code.
static gboolean
are_available_tracks_valid (struct layout_editor *editor)
{
  for (int i = 0; i < 2; i++)
    {
      JsonArray *available =
	get_definition_array (editor, AVAILABLE_TRACKS_MEMBERS[i]);
      if (!available || !json_array_get_length (available))
	{
	  return FALSE;
	}

      for (guint j = 0; j < json_array_get_length (available); j++)
	{
	  JsonNode *node = json_array_get_element (available, j);
	  if (!JSON_NODE_HOLDS_OBJECT (node))
	    {
	      return FALSE;
	    }
	  JsonObject *track = json_node_get_object (node);
	  if (!json_object_has_member (track, DEV_TAG_TRACK_NAME) ||
	      !json_object_has_member (track, DEV_TAG_TRACK_CODE))
	    {
	      return FALSE;
	    }
	}
    }

  return TRUE;
}

void
layout_editor_open (GtkWindow *parent, guint16 pid, const gchar *device,
		    layout_editor_saved_cb_t saved_cb)
{
  gchar *title;
  GtkWidget *box, *directions, *buttons, *cancel_button;
  GtkEventController *key_controller;
  struct layout_editor *editor = g_new0 (struct layout_editor, 1);

  editor->saved_cb = saved_cb;
  editor->definition = get_definition (pid, &editor->path);

  if (editor->definition && !are_available_tracks_valid (editor))
    {
      add_available_tracks (editor, pid);
    }

  if (!editor->definition || !are_available_tracks_valid (editor))
    {
      GtkAlertDialog *dialog =
	gtk_alert_dialog_new (_("The track layout can not be edited"));
      gtk_alert_dialog_set_detail (dialog,
				   _
				   ("The device definition does not list the available tracks."));
      gtk_alert_dialog_show (dialog, parent);
      g_object_unref (dialog);
      if (editor->definition)
	{
	  json_node_unref (editor->definition);
	}
      g_free (editor->path);
      g_free (editor);
      return;
    }

  editor->window = gtk_window_new ();
  title = g_strdup_printf (_("%s Track Layout"), device);
  gtk_window_set_title (GTK_WINDOW (editor->window), title);
  g_free (title);
  gtk_window_set_application (GTK_WINDOW (editor->window),
			      gtk_window_get_application (parent));
  gtk_window_set_transient_for (GTK_WINDOW (editor->window), parent);
  gtk_window_set_modal (GTK_WINDOW (editor->window), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (editor->window), FALSE);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top (box, 6);
  gtk_widget_set_margin_bottom (box, 6);
  gtk_widget_set_margin_start (box, 6);
  gtk_widget_set_margin_end (box, 6);

  directions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 24);
  title = g_strdup_printf (_("From %s"), device);
  gtk_box_append (GTK_BOX (directions), create_direction (editor, 0, title));
  g_free (title);
  title = g_strdup_printf (_("To %s"), device);
  gtk_box_append (GTK_BOX (directions), create_direction (editor, 1, title));
  g_free (title);
  gtk_box_append (GTK_BOX (box), directions);

  editor->status_label = gtk_label_new (NULL);
  gtk_widget_set_halign (editor->status_label, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (box), editor->status_label);

  buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_halign (buttons, GTK_ALIGN_END);
  cancel_button = gtk_button_new_with_mnemonic (_("_Cancel"));
  editor->save_button = gtk_button_new_with_mnemonic (_("_Save"));
  gtk_box_append (GTK_BOX (buttons), cancel_button);
  gtk_box_append (GTK_BOX (buttons), editor->save_button);
  gtk_box_append (GTK_BOX (box), buttons);

  gtk_window_set_child (GTK_WINDOW (editor->window), box);

  g_signal_connect (cancel_button, "clicked", G_CALLBACK (on_cancel_clicked),
		    editor);
  g_signal_connect (editor->save_button, "clicked",
		    G_CALLBACK (on_save_clicked), editor);
  g_signal_connect (editor->window, "destroy",
		    G_CALLBACK (on_window_destroy), editor);

  key_controller = gtk_event_controller_key_new ();
  g_signal_connect (key_controller, "key-pressed",
		    G_CALLBACK (on_key_pressed), editor);
  gtk_widget_add_controller (editor->window, key_controller);

  update_status (editor);

  gtk_window_present (GTK_WINDOW (editor->window));
}
