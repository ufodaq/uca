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

#include <glib-object.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include "uca-camera.h"

static UcaCamera *camera = NULL;

static void sigint_handler(int signal)
{
    printf("Closing down libuca\n");
    uca_camera_stop_recording(camera, NULL);
    g_object_unref(camera);
    exit(signal);
}

static void print_usage(void)
{
    gchar **types;
    
    g_print("Usage: grab (");
    types = uca_camera_get_types();

    for (guint i = 0; types[i] != NULL; i++) {
        if (types[i+1] == NULL)
            g_print("%s)", types[i]);
        else
            g_print("%s | ", types[i]);
    }

    g_print("\n");
    g_strfreev(types);
}

int main(int argc, char *argv[])
{
    GError *error = NULL;
    (void) signal(SIGINT, sigint_handler);

    guint sensor_width, sensor_height;
    guint roi_width, roi_height, roi_x, roi_y, roi_width_multiplier, roi_height_multiplier;
    guint bits;
    gchar *name;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    g_type_init();
    camera = uca_camera_new(argv[1], &error);

    if (camera == NULL) {
        g_print("Error during initialization: %s\n", error->message);
        return 1;
    }

    g_object_get(G_OBJECT(camera),
            "sensor-width", &sensor_width,
            "sensor-height", &sensor_height,
            "name", &name,
            NULL);

    g_object_set(G_OBJECT(camera),
            "exposure-time", 0.1,
            "roi-x0", 0,
            "roi-y0", 0,
            "roi-width", 1000,
            "roi-height", sensor_height,
            NULL);

    g_object_get(G_OBJECT(camera),
            "roi-width", &roi_width,
            "roi-height", &roi_height,
            "roi-width-multiplier", &roi_width_multiplier,
            "roi-height-multiplier", &roi_height_multiplier,
            "roi-x0", &roi_x,
            "roi-y0", &roi_y,
            "sensor-bitdepth", &bits,
            NULL);

    g_print("Camera: %s\n", name);
    g_free(name);

    g_print("Sensor: %ix%i px\n", sensor_width, sensor_height);
    g_print("ROI: %ix%i @ (%i, %i), steps: %i, %i\n", 
            roi_width, roi_height, roi_x, roi_y, roi_width_multiplier, roi_height_multiplier);

    const int pixel_size = bits == 8 ? 1 : 2;
    gpointer buffer = g_malloc0(roi_width * roi_height * pixel_size);
    gchar filename[FILENAME_MAX];
    GTimer *timer = g_timer_new();

    for (int i = 0; i < 1; i++) {
        gint counter = 0;
        g_print("Start recording\n");
        uca_camera_start_recording(camera, &error);
        g_assert_no_error(error);

        while (counter < 2) {
            g_print(" grab frame ... ");
            g_timer_start(timer);
            uca_camera_grab(camera, &buffer, &error);

            if (error != NULL) {
                g_print("\nError: %s\n", error->message);
                goto cleanup;
            }

            g_timer_stop(timer);
            g_print("done (took %3.5fs)\n", g_timer_elapsed(timer, NULL));

            snprintf(filename, FILENAME_MAX, "frame-%08i.raw", counter++);
            FILE *fp = fopen(filename, "wb");
            fwrite(buffer, roi_width * roi_height, pixel_size, fp);
            fclose(fp);
        }

        g_print("Stop recording\n");
        uca_camera_stop_recording(camera, &error);
        g_assert_no_error(error);
    }

    g_timer_destroy(timer);

cleanup:
    uca_camera_stop_recording(camera, NULL);
    g_object_unref(camera);
    g_free(buffer);

    return error != NULL ? 1 : 0;
}
