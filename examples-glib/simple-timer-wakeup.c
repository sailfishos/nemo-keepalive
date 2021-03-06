/****************************************************************************************
**
** Copyright (C) 2014 - 2018 Jolla Ltd.
**
** Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
**
** All rights reserved.
**
** This file is part of nemo-keepalive package.
**
** You may use this file under the terms of the GNU Lesser General
** Public License version 2.1 as published by the Free Software Foundation
** and appearing in the file license.lgpl included in the packaging
** of this file.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file license.lgpl included in the packaging
** of this file.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
** Lesser General Public License for more details.
**
****************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <dbus/dbus.h>
#include "../dbus-gmain/dbus-gmain.h"
#include <keepalive-glib/keepalive-timeout.h>

#include <assert.h>

#define failure(FMT, ARGS...) do {\
    fprintf(stderr, "%s: "FMT"\n", __FUNCTION__, ## ARGS);\
    exit(EXIT_FAILURE); \
} while(0)

static DBusConnection *system_bus = 0;
static GMainLoop *mainloop_handle = 0;

static void disconnect_from_systembus(void)
{
    if( system_bus )
        dbus_connection_unref(system_bus), system_bus = 0;
}

static void connect_to_system_bus(void)
{
    DBusError err = DBUS_ERROR_INIT;
    system_bus = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if( !system_bus )
        failure("%s: %s", err.name, err.message);
    dbus_gmain_set_up_connection(system_bus, 0);
    dbus_error_free(&err);
}

static gboolean exit_timer_cb(gpointer aptr)
{
    /* Suspend is blocked before this function is called */

    static int count = 0;

    fprintf(stdout, "TIMER %d\n", ++count);

    if( count < 4 ) {
        /* After returning TRUE the next IPHB wakeup is
         * scheduled and suspending is allowed again */
        return TRUE;
    }

    g_main_loop_quit(mainloop_handle);

    /* After returning FALSE all timer resources are
     * released and suspending is allowed again */
    return FALSE;
}

int main(int argc, char **argv)
{
    (void)argc, (void)argv;

    mainloop_handle = g_main_loop_new(0, 0);

    connect_to_system_bus();

    /* Create timer that can wake the device from suspend */
    guint id = keepalive_timeout_add_seconds(15, exit_timer_cb, 0);
    printf("timer id = %u\n", id);

    /* If needed, the timer can be cancelled similarly to normal
     * glib timeouts, via: g_source_remove(id) */

    /* Run mainloop; suspend is allowed, except when the
     * exit_timer_cb is being executed */
    printf("ENTER MAINLOOP\n");
    g_main_loop_run(mainloop_handle);
    printf("LEAVE MAINLOOP\n");

    disconnect_from_systembus();

    g_main_loop_unref(mainloop_handle);
    return 0;
}
