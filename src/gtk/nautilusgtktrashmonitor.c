/* GTK - The GIMP Toolkit
 * nautilusgtktrashmonitor.h: Monitor the trash:/// folder to see if there is trash or not
 * Copyright (C) 2011 Suse
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Federico Mena Quintero <federico@gnome.org>
 */

#include "config.h"
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "nautilusgtktrashmonitor.h"

struct _NautilusGtkTrashMonitor
{
  GObject parent;

  GFileMonitor *file_monitor;
  gulong file_monitor_changed_id;
  
  guint has_trash : 1;
};

struct _NautilusGtkTrashMonitorClass
{
  GObjectClass parent_class;

  void (* trash_state_changed) (NautilusGtkTrashMonitor *monitor);
};

enum {
  TRASH_STATE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (NautilusGtkTrashMonitor, _nautilus_gtk_trash_monitor, G_TYPE_OBJECT)

static NautilusGtkTrashMonitor *the_trash_monitor;

#define ICON_NAME_TRASH_EMPTY "user-trash-symbolic"
#define ICON_NAME_TRASH_FULL  "user-trash-full-symbolic"

static void
nautilus_gtk_trash_monitor_dispose (GObject *object)
{
  NautilusGtkTrashMonitor *monitor;

  monitor = NAUTILUS_GTK_TRASH_MONITOR (object);

  if (monitor->file_monitor)
    {
      g_signal_handler_disconnect (monitor->file_monitor, monitor->file_monitor_changed_id);
      monitor->file_monitor_changed_id = 0;

      g_clear_object (&monitor->file_monitor);
    }

  G_OBJECT_CLASS (_nautilus_gtk_trash_monitor_parent_class)->dispose (object);
}

static void
_nautilus_gtk_trash_monitor_class_init (NautilusGtkTrashMonitorClass *class)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) class;

  gobject_class->dispose = nautilus_gtk_trash_monitor_dispose;

  signals[TRASH_STATE_CHANGED] =
    g_signal_new ("trash-state-changed",
		  G_OBJECT_CLASS_TYPE (gobject_class),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (NautilusGtkTrashMonitorClass, trash_state_changed),
		  NULL, NULL,
		  NULL,
		  G_TYPE_NONE, 0);
}

/* Updates the internal has_trash flag and emits the "trash-state-changed" signal */
static void
update_has_trash_and_notify (NautilusGtkTrashMonitor *monitor,
			     gboolean has_trash)
{
  if (monitor->has_trash == !!has_trash)
    return;

  monitor->has_trash = !!has_trash;

  g_signal_emit (monitor, signals[TRASH_STATE_CHANGED], 0); 
}

/* Use G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT since we only want to know whether the
 * trash is empty or not, not access its children. This is available for the
 * trash backend since it uses a cache. In this way we prevent flooding the
 * trash backend with enumeration requests when trashing > 1000 files
 */
static void
trash_query_info_cb (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
  NautilusGtkTrashMonitor *monitor = NAUTILUS_GTK_TRASH_MONITOR (user_data);
  GFileInfo *info;
  guint32 item_count;
  gboolean has_trash = FALSE;

  info = g_file_query_info_finish (G_FILE (source), result, NULL);

  if (info != NULL)
    {
      item_count = g_file_info_get_attribute_uint32 (info,
                                                     G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT);
      has_trash = item_count > 0;

      g_object_unref (info);
    }

  update_has_trash_and_notify (monitor, has_trash);

  g_object_unref (monitor); /* was reffed in recompute_trash_state() */
}

/* Asynchronously recomputes whether there is trash or not */
static void
recompute_trash_state (NautilusGtkTrashMonitor *monitor)
{
  GFile *file;

  file = g_file_new_for_uri ("trash:///");
  g_file_query_info_async (file,
                           G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT, NULL,
                           trash_query_info_cb, g_object_ref (monitor));

  g_object_unref (file);
}

/* Callback used when the "trash:///" file monitor changes; we just recompute the trash state
 * whenever something happens.
 */
static void
file_monitor_changed_cb (GFileMonitor	   *file_monitor,
			 GFile		   *child,
			 GFile		   *other_file,
			 GFileMonitorEvent  event_type,
			 NautilusGtkTrashMonitor   *monitor)
{
  recompute_trash_state (monitor);
}

static void
_nautilus_gtk_trash_monitor_init (NautilusGtkTrashMonitor *monitor)
{
  GFile *file;

  file = g_file_new_for_uri ("trash:///");

  monitor->file_monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, NULL);

  g_object_unref (file);

  if (monitor->file_monitor)
    monitor->file_monitor_changed_id = g_signal_connect (monitor->file_monitor, "changed",
							 G_CALLBACK (file_monitor_changed_cb), monitor);

  recompute_trash_state (monitor);
}

/*
 * _nautilus_gtk_trash_monitor_get:
 *
 * Returns: (transfer full): a new reference to the singleton
 * #NautilusGtkTrashMonitor object.  Be sure to call g_object_unref() on it when you are
 * done with the trash monitor.
 */
NautilusGtkTrashMonitor *
_nautilus_gtk_trash_monitor_get (void)
{
  if (the_trash_monitor != NULL)
    {
      g_object_ref (the_trash_monitor);
    }
  else
    {
      the_trash_monitor = g_object_new (NAUTILUS_TYPE_GTK_TRASH_MONITOR, NULL);
      g_object_add_weak_pointer (G_OBJECT (the_trash_monitor), (gpointer *) &the_trash_monitor);
    }

  return the_trash_monitor;
}

/*
 * _nautilus_gtk_trash_monitor_get_icon:
 * @monitor: a #NautilusGtkTrashMonitor
 *
 * Returns: (transfer full): the #GIcon that should be used to represent
 * the state of the trash folder on screen, based on whether there is trash or
 * not.
 */
GIcon *
_nautilus_gtk_trash_monitor_get_icon (NautilusGtkTrashMonitor *monitor)
{
  const char *icon_name;

  g_return_val_if_fail (NAUTILUS_GTK_IS_TRASH_MONITOR (monitor), NULL);

  if (monitor->has_trash)
    icon_name = ICON_NAME_TRASH_FULL;
  else
    icon_name = ICON_NAME_TRASH_EMPTY;

  return g_themed_icon_new (icon_name);
}

/*
 * _nautilus_gtk_trash_monitor_get_has_trash:
 * @monitor: a #NautilusGtkTrashMonitor
 *
 * Returns: #TRUE if there is trash in the trash:/// folder, or #FALSE otherwise.
 */
gboolean
_nautilus_gtk_trash_monitor_get_has_trash (NautilusGtkTrashMonitor *monitor)
{
  g_return_val_if_fail (NAUTILUS_GTK_IS_TRASH_MONITOR (monitor), FALSE);

  return monitor->has_trash;
}