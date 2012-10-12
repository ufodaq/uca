/* Copyright (C) 2011, 2012 Matthias Vogelgesang <matthias.vogelgesang@kit.edu>
   (Karlsruhe Institute of Technology)

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by the
   Free Software Foundation; either version 2.1 of the License, or (at your
   option) any later version.

   This library is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
   details.

   You should have received a copy of the GNU Lesser General Public License along
   with this library; if not, write to the Free Software Foundation, Inc., 51
   Franklin St, Fifth Floor, Boston, MA 02110, USA */

#include <glib/gprintf.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>

#include "config.h"
#include "ring-buffer.h"
#include "uca-camera.h"
#include "uca-plugin-manager.h"
#include "egg-property-tree-view.h"
#include "egg-histogram-view.h"

typedef enum {
    IDLE,
    RUNNING,
    RECORDING
} State;

typedef struct {
    UcaCamera   *camera;
    GdkPixbuf   *pixbuf;
    GtkWidget   *image;
    GtkWidget   *start_button;
    GtkWidget   *stop_button;
    GtkWidget   *record_button;

    GtkWidget       *histogram_view;
    GtkToggleButton *histogram_button;
    GtkAdjustment   *frame_slider;

    RingBuffer  *buffer;
    guchar      *pixels;
    State        state;

    int          timestamp;
    int          width;
    int          height;
    int          pixel_size;
} ThreadData;

static UcaPluginManager *plugin_manager;


static void
convert_grayscale_to_rgb (ThreadData *data, gpointer buffer)
{
    gdouble min;
    gdouble max;
    gdouble factor;
    guint8 *output;

    egg_histogram_get_visible_range (EGG_HISTOGRAM_VIEW (data->histogram_view), &min, &max);
    factor = 255.0 / (max - min);
    output = data->pixels;

    if (data->pixel_size == 1) {
        guint8 *input = (guint8 *) buffer;

        for (int i = 0, j = 0; i < data->width * data->height; i++) {
            guchar val = (guchar) ((input[i] - min) * factor);
            output[j++] = val;
            output[j++] = val;
            output[j++] = val;
        }
    }
    else if (data->pixel_size == 2) {
        guint16 *input = (guint16 *) buffer;

        for (int i = 0, j = 0; i < data->width * data->height; i++) {
            guchar val = (guint8) ((input[i] - min) * factor);
            output[j++] = val;
            output[j++] = val;
            output[j++] = val;
        }
    }
}

static void
update_pixbuf (ThreadData *data)
{
    gdk_flush ();
    gtk_image_clear (GTK_IMAGE (data->image));
    gtk_image_set_from_pixbuf (GTK_IMAGE (data->image), data->pixbuf);
    gtk_widget_queue_draw_area (data->image, 0, 0, data->width, data->height);

    if (gtk_toggle_button_get_active (data->histogram_button))
        gtk_widget_queue_draw (data->histogram_view);
}

static gpointer
preview_frames (void *args)
{
    ThreadData *data = (ThreadData *) args;
    gint counter = 0;

    while (data->state == RUNNING) {
        gpointer buffer;

        buffer = ring_buffer_get_current_pointer (data->buffer);
        uca_camera_grab (data->camera, &buffer, NULL);
        convert_grayscale_to_rgb (data, buffer);

        gdk_threads_enter ();
        update_pixbuf (data);
        gdk_threads_leave ();

        counter++;
    }
    return NULL;
}

static gpointer
record_frames (gpointer args)
{
    ThreadData *data;
    gpointer buffer;
    guint n_frames = 0;

    data = (ThreadData *) args;
    ring_buffer_reset (data->buffer);

    while (data->state == RECORDING) {
        buffer = ring_buffer_get_current_pointer (data->buffer);
        uca_camera_grab (data->camera, &buffer, NULL);
        ring_buffer_proceed (data->buffer);
        n_frames++;
    }

    n_frames = ring_buffer_get_num_blocks (data->buffer);

    gdk_threads_enter ();
    gtk_adjustment_set_upper (data->frame_slider, n_frames - 1);
    gtk_adjustment_set_value (data->frame_slider, n_frames - 1);
    gdk_threads_leave ();

    return NULL;
}

gboolean
on_delete_event (GtkWidget *widget, GdkEvent *event, gpointer data)
{
    return FALSE;
}

void
on_destroy (GtkWidget *widget, gpointer data)
{
    ThreadData *td = (ThreadData *) data;

    td->state = IDLE;
    g_object_unref (td->camera);
    ring_buffer_free (td->buffer);

    gtk_main_quit ();
}

static void
set_tool_button_state (ThreadData *data)
{
    gtk_widget_set_sensitive (data->start_button,
                              data->state == IDLE);
    gtk_widget_set_sensitive (data->stop_button,
                              data->state == RUNNING || data->state == RECORDING);
    gtk_widget_set_sensitive (data->record_button,
                              data->state == IDLE);
}

static void
on_frame_slider_changed (GtkAdjustment *adjustment, gpointer user_data)
{
    ThreadData *data = (ThreadData *) user_data;

    if (data->state == IDLE) {
        gpointer buffer;
        gint index;
         
        index = (gint) gtk_adjustment_get_value (adjustment);
        buffer = ring_buffer_get_pointer (data->buffer, index);
        convert_grayscale_to_rgb (data, buffer);
        update_pixbuf (data);
    }
}

static void
on_start_button_clicked (GtkWidget *widget, gpointer args)
{
    ThreadData *data = (ThreadData *) args;
    GError *error = NULL;

    data->state = RUNNING;

    set_tool_button_state (data);
    uca_camera_start_recording (data->camera, &error);

    if (error != NULL) {
        g_printerr ("Failed to start recording: %s\n", error->message);
        return;
    }

    if (!g_thread_create (preview_frames, data, FALSE, &error)) {
        g_printerr ("Failed to create thread: %s\n", error->message);
        return;
    }
}

static void
on_stop_button_clicked (GtkWidget *widget, gpointer args)
{
    ThreadData *data = (ThreadData *) args;
    GError *error = NULL;

    data->state = IDLE;

    set_tool_button_state (data);
    uca_camera_stop_recording (data->camera, &error);

    if (error != NULL)
        g_printerr ("Failed to stop: %s\n", error->message);
}

static void
on_record_button_clicked (GtkWidget *widget, gpointer args)
{
    ThreadData *data = (ThreadData *) args;
    GError *error = NULL;

    data->timestamp = (int) time (0);
    data->state = RECORDING;

    set_tool_button_state (data);
    uca_camera_start_recording (data->camera, &error);

    if (!g_thread_create (record_frames, data, FALSE, &error))
        g_printerr ("Failed to create thread: %s\n", error->message);
}

static void
create_main_window (GtkBuilder *builder, const gchar* camera_name)
{
    GtkWidget *window;
    GtkWidget *image;
    GtkWidget *property_tree_view;
    GdkPixbuf *pixbuf;
    GtkBox    *histogram_box;
    GtkContainer *scrolled_property_window;
    GtkAdjustment *max_bin_adjustment;
    static ThreadData td;

    GError *error = NULL;
    UcaCamera *camera = uca_plugin_manager_get_camera (plugin_manager, camera_name, &error);

    if ((camera == NULL) || (error != NULL)) {
        g_error ("%s\n", error->message);
        gtk_main_quit ();
    }

    guint bits_per_sample;
    g_object_get (camera,
                  "roi-width", &td.width,
                  "roi-height", &td.height,
                  "sensor-bitdepth", &bits_per_sample,
                  NULL);

    property_tree_view = egg_property_tree_view_new (G_OBJECT (camera));
    scrolled_property_window = GTK_CONTAINER (gtk_builder_get_object (builder, "scrolledwindow2"));
    gtk_container_add (scrolled_property_window, property_tree_view);

    image = GTK_WIDGET (gtk_builder_get_object (builder, "image"));
    pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, td.width, td.height);
    gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);

    td.pixel_size = bits_per_sample > 8 ? 2 : 1;
    td.image  = image;
    td.pixbuf = pixbuf;
    td.buffer = ring_buffer_new (td.pixel_size * td.width * td.height, 256);
    td.pixels = gdk_pixbuf_get_pixels (pixbuf);
    td.state  = IDLE;
    td.camera = camera;
    td.histogram_view = egg_histogram_view_new ();
    td.histogram_button = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "histogram-checkbutton"));
    td.frame_slider = GTK_ADJUSTMENT (gtk_builder_get_object (builder, "frames-adjustment"));

    histogram_box = GTK_BOX (gtk_builder_get_object (builder, "histogram-box"));
    gtk_box_pack_start (histogram_box, td.histogram_view, TRUE, TRUE, 6);
    egg_histogram_view_set_data (EGG_HISTOGRAM_VIEW (td.histogram_view),
                                 ring_buffer_get_current_pointer (td.buffer),                      
                                 td.width * td.height, bits_per_sample, 256);

    window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
    g_signal_connect (window, "destroy", G_CALLBACK (on_destroy), &td);

    td.start_button = GTK_WIDGET (gtk_builder_get_object (builder, "start-button"));
    td.stop_button = GTK_WIDGET (gtk_builder_get_object (builder, "stop-button"));
    td.record_button = GTK_WIDGET (gtk_builder_get_object (builder, "record-button"));
    set_tool_button_state (&td);

    g_object_bind_property (gtk_builder_get_object (builder, "min-bin-value-adjustment"), "value",
                            td.histogram_view, "minimum-bin-value",
                            G_BINDING_DEFAULT);

    max_bin_adjustment = GTK_ADJUSTMENT (gtk_builder_get_object (builder, "max-bin-value-adjustment"));
    gtk_adjustment_set_value (max_bin_adjustment, pow (2, bits_per_sample) - 1);
    g_object_bind_property (max_bin_adjustment, "value",
                            td.histogram_view, "maximum-bin-value",
                            G_BINDING_DEFAULT);

    g_signal_connect (td.frame_slider, "value-changed", G_CALLBACK (on_frame_slider_changed), &td);
    g_signal_connect (td.start_button, "clicked", G_CALLBACK (on_start_button_clicked), &td);
    g_signal_connect (td.stop_button, "clicked", G_CALLBACK (on_stop_button_clicked), &td);
    g_signal_connect (td.record_button, "clicked", G_CALLBACK (on_record_button_clicked), &td);

    gtk_widget_show_all (window);
}

static void
on_button_proceed_clicked (GtkWidget *widget, gpointer data)
{
    GtkBuilder *builder = GTK_BUILDER (data);
    GtkWidget *choice_window = GTK_WIDGET (gtk_builder_get_object (builder, "choice-window"));
    GtkTreeView *treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview-cameras"));
    GtkListStore *list_store = GTK_LIST_STORE (gtk_builder_get_object (builder, "camera-types"));

    GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
    GList *selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);
    GtkTreeIter iter;

    gtk_widget_destroy (choice_window);
    gboolean valid = gtk_tree_model_get_iter (GTK_TREE_MODEL (list_store), &iter, selected_rows->data);

    if (valid) {
        gchar *data;
        gtk_tree_model_get (GTK_TREE_MODEL (list_store), &iter, 0, &data, -1);
        create_main_window (builder, data);
        g_free (data);
    }

    g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
    g_list_free (selected_rows);
}

static void
on_treeview_keypress (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    if (event->keyval == GDK_KEY_Return)
        gtk_widget_grab_focus (GTK_WIDGET (data));
}

static void
create_choice_window (GtkBuilder *builder)
{
    GList *camera_types = uca_plugin_manager_get_available_cameras (plugin_manager);

    GtkWidget *choice_window = GTK_WIDGET (gtk_builder_get_object (builder, "choice-window"));
    GtkTreeView *treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview-cameras"));
    GtkListStore *list_store = GTK_LIST_STORE (gtk_builder_get_object (builder, "camera-types"));
    GtkButton *proceed_button = GTK_BUTTON (gtk_builder_get_object (builder, "proceed-button"));
    GtkTreeIter iter;

    for (GList *it = g_list_first (camera_types); it != NULL; it = g_list_next (it)) {
        gtk_list_store_append (list_store, &iter);
        gtk_list_store_set (list_store, &iter, 0, g_strdup ((gchar *) it->data), -1);
    }

    gboolean valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store), &iter);

    if (valid) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
        gtk_tree_selection_unselect_all (selection);
        gtk_tree_selection_select_path (selection, gtk_tree_model_get_path (GTK_TREE_MODEL (list_store), &iter));
    }

    g_signal_connect (proceed_button, "clicked", G_CALLBACK (on_button_proceed_clicked), builder);
    g_signal_connect (treeview, "key-press-event", G_CALLBACK (on_treeview_keypress), proceed_button);
    gtk_widget_show_all (GTK_WIDGET (choice_window));

    g_list_foreach (camera_types, (GFunc) g_free, NULL);
    g_list_free (camera_types);
}

int
main (int argc, char *argv[])
{
    GError *error = NULL;

    g_thread_init (NULL);
    gdk_threads_init ();
    gtk_init (&argc, &argv);

    GtkBuilder *builder = gtk_builder_new ();

    if (!gtk_builder_add_from_file (builder, CONTROL_GLADE_PATH, &error)) {
        g_print ("Error: %s\n", error->message);
        return 1;
    }

    plugin_manager = uca_plugin_manager_new ();
    create_choice_window (builder);
    gtk_builder_connect_signals (builder, NULL);

    gdk_threads_enter ();
    gtk_main ();
    gdk_threads_leave ();

    g_object_unref (plugin_manager);
    return 0;
}
