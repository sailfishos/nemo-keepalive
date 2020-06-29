/****************************************************************************************
**
** Copyright (c) 2014 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
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

#include "xdbus.h"
#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <glib.h>

/* Logging prefix for this module */
#define PFIX "dbus: "

/** Predicate for: connection is not null and in connected state
 */
bool
xdbus_connection_is_valid(DBusConnection *con)
{
    return con && dbus_connection_get_is_connected(con);
}

/** Helper for making asynchronous D-Bus method calls; varargs version
 */
DBusPendingCall *
xdbus_method_call_va(DBusConnection *con,
                     const char *service,
                     const char *object,
                     const char *interface,
                     const char *method,
                     DBusPendingCallNotifyFunction notify_cb,
                     void *data,
                     DBusFreeFunction free_cb,
                     int arg_type,
                     va_list va)
{
    DBusPendingCall  *res = 0;
    DBusPendingCall  *pc  = 0;
    DBusMessage      *req = 0;

    if( !xdbus_connection_is_valid(con) )
        goto cleanup;

    req = dbus_message_new_method_call(service, object, interface, method);
    if( !req )
        goto cleanup;

    if( arg_type != DBUS_TYPE_INVALID &&
        !dbus_message_append_args_valist(req, arg_type, va) )
        goto cleanup;

    log_notice(PFIX"calling method: %s.%s", interface, method);

    if( !notify_cb ) {
        dbus_message_set_no_reply(req, TRUE);
        dbus_connection_send(con, req, 0);
        goto cleanup;
    }

    if( !dbus_connection_send_with_reply(con, req, &pc, -1) )
        goto cleanup;

    if( !pc )
        goto cleanup;

    if( !dbus_pending_call_set_notify(pc, notify_cb, data, free_cb) )
        goto cleanup;

    /* Success. Note that also notification holds a reference to
     * pending call until it is either cancelled or finalized.
     */
    res = pc, pc = 0;

cleanup:

    if( pc )
        dbus_pending_call_unref(pc);

    if( req )
        dbus_message_unref(req);

    /* Note: The cleanup functions must be assumed to cause
     *       deadlocks if called from this context -> data
     *       ownership is transferred only on succesful
     *       return -> in case of errors, caller must handle
     *       cleanup.
     */

    return res;
}

/** Helper for making asynchronous D-Bus method calls
 */
DBusPendingCall *
xdbus_method_call(DBusConnection *con,
                  const char *service,
                  const char *object,
                  const char *interface,
                  const char *method,
                  DBusPendingCallNotifyFunction notify_cb,
                  void *data,
                  DBusFreeFunction free_cb,
                  int arg_type,
                  ...)
{
    DBusPendingCall *res = 0;

    va_list va;

    va_start(va, arg_type);
    res = xdbus_method_call_va(con, service, object, interface, method,
                               notify_cb, data, free_cb, arg_type, va);
    va_end(va);

    return res;
}

/** Helper for making async D-Bus method calls without waiting for reply
 */
void
xdbus_simple_call(DBusConnection *con,
                  const char *service,
                  const char *object,
                  const char *interface,
                  const char *method,
                  int arg_type,
                  ...)
{
    DBusPendingCall *res = 0;

    va_list va;

    va_start(va, arg_type);
    res = xdbus_method_call_va(con, service, object, interface, method,
                               0, 0, 0, arg_type, va);
    va_end(va);

    /* Note: this should never happen */
    if( res ) {
        dbus_pending_call_cancel(res);
        dbus_pending_call_unref(res);
    }
}
