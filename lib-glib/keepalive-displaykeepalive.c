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

#include "keepalive-displaykeepalive.h"
#include "keepalive-object.h"

#include "xdbus.h"
#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <glib.h>
#include <dbus/dbus.h>

#include <mce/dbus-names.h>
#include <mce/mode-names.h>

/** Display keepalive renew time */
#define DISPLAY_KEEPALIVE_RENEW_MS (60 * 1000)

/* Logging prefix for this module */
#define PFIX "displaykeepalive: "

/* ========================================================================= *
 * TYPES
 * ========================================================================= */

/** Enumeration of states a D-Bus service can be in */
typedef enum {
    NAMEOWNER_UNKNOWN,  /** Initial placeholder value */
    NAMEOWNER_STOPPED,  /** Service does not have owner */
    NAMEOWNER_RUNNING,  /** Service has an owner */
} nameowner_t;

/** Enumeration of states prevent mode can be in */
typedef enum {
    PREVENTMODE_UNKNOWN, /** Initial placeholder value */
    PREVENTMODE_ALLOWED, /** Blank prevention is allowed */
    PREVENTMODE_DENIED,  /** Blank prevention is not allowed */
} preventmode_t;

/** Memory tag for marking live displaykeepalive_t objects */
#define DISPLAYKEEPALIVE_MAJICK_ALIVE 0x548ead10

/** Memory tag for marking dead displaykeepalive_t objects */
#define DISPLAYKEEPALIVE_MAJICK_DEAD  0x00000000

/** Display keepalive state object
 */
struct displaykeepalive_t
{
    /* Base object for locking and refcounting */
    keepalive_object_t    dka_object;

    /** Simple memory tag to catch usage of obviously bogus
     *  displaykeepalive_t pointers */
    unsigned         dka_majick;

    /** Flag for: preventing display blanking requested */
    bool             dka_requested;

    /** Flag for: we've already tried to connect to system bus */
    bool             dka_connect_attempted;

    /** System bus connection */
    DBusConnection  *dka_systembus;

    /** Flag for: signal filters installed */
    bool             dka_filter_added;

    /** Current prevent mode */
    preventmode_t   dka_preventmode;

    /** Async D-Bus query for initial dka_preventmode value */
    DBusPendingCall *dka_preventmode_pc;

    /** Current com.nokia.mce name ownership state */
    nameowner_t      dka_mce_service;

    /** Async D-Bus query for initial dka_mce_service value */
    DBusPendingCall *dka_mce_service_pc;

    /** Timer id for active display keepalive session */
    guint            dka_session_renew_id;

    /** Idle callback id for starting/stopping keepalive session */
    guint            dka_rethink_id;

    // NOTE: displaykeepalive_ctor & displaykeepalive_dtor
};

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * OBJECT_LIFETIME
 * ------------------------------------------------------------------------- */

static void                displaykeepalive_ctor                 (displaykeepalive_t *self);
static void                displaykeepalive_shutdown_locked_cb   (gpointer aptr);
static void                displaykeepalive_delete_cb            (gpointer aptr);
static void                displaykeepalive_dtor                 (displaykeepalive_t *self);
static bool                displaykeepalive_is_valid             (const displaykeepalive_t *self);
static displaykeepalive_t *displaykeepalive_ref_external_locked  (displaykeepalive_t *self);
static void                displaykeepalive_unref_external_locked(displaykeepalive_t *self);
static void                displaykeepalive_lock                 (displaykeepalive_t *self);
static void                displaykeepalive_unlock               (displaykeepalive_t *self);
static bool                displaykeepalive_validate_and_lock    (displaykeepalive_t *self);
static bool                displaykeepalive_in_shutdown_locked   (displaykeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * OBJECT_TIMERS
 * ------------------------------------------------------------------------- */

static void displaykeepalive_timer_start_locked(displaykeepalive_t *self, guint *timer_id, guint interval, GSourceFunc notify_cb);
static void displaykeepalive_timer_stop_locked (displaykeepalive_t *self, guint *timer_id);

/* ------------------------------------------------------------------------- *
 * OBJECT_DBUS_IPC
 * ------------------------------------------------------------------------- */

static void displaykeepalive_ipc_start_locked (displaykeepalive_t *self, DBusPendingCall **where, DBusPendingCallNotifyFunction notify_cb, const char *service, const char *object, const char *interface, const char *method, int arg_type, ...);
static void displaykeepalive_ipc_cancel_locked(displaykeepalive_t *self, DBusPendingCall **where);
static bool displaykeepalive_ipc_finish_locked(displaykeepalive_t *self, DBusPendingCall **where, DBusPendingCall *what);

/* ------------------------------------------------------------------------- *
 * KEEPALIVE_SESSION
 * ------------------------------------------------------------------------- */

static void     displaykeepalive_session_ipc_locked  (displaykeepalive_t *self, const char *method);
static gboolean displaykeepalive_session_renew_cb    (gpointer aptr);
static void     displaykeepalive_session_start_locked(displaykeepalive_t *self);
static void     displaykeepalive_session_stop_locked (displaykeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * RETHINK_STATE
 * ------------------------------------------------------------------------- */

static void     displaykeepalive_rethink_now_locked     (displaykeepalive_t *self);
static gboolean displaykeepalive_rethink_cb             (gpointer aptr);
static void     displaykeepalive_rethink_schedule_locked(displaykeepalive_t *self);
static void     displaykeepalive_rethink_cancel_locked  (displaykeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * MCE_SERVICE_TRACKING
 * ------------------------------------------------------------------------- */

static nameowner_t displaykeepalive_mce_owner_get_locked   (const displaykeepalive_t *self);
static void        displaykeepalive_mce_owner_update_locked(displaykeepalive_t *self, nameowner_t state);

/* ------------------------------------------------------------------------- *
 * DBUS_CONNECTION
 * ------------------------------------------------------------------------- */

static void              displaykeepalive_dbus_cancel_mce_owner_query_locked   (displaykeepalive_t *self);
static void              displaykeepalive_dbus_mce_owner_query_reply_cb        (DBusPendingCall *pc, void *aptr);
static void              displaykeepalive_dbus_start_mce_owner_query_locked    (displaykeepalive_t *self);
static void              displaykeepalive_dbus_preventmode_reply_cb            (DBusPendingCall *pc, void *aptr);
static void              displaykeepalive_dbus_start_preventmode_query_locked  (displaykeepalive_t *self);
static void              displaykeepalive_dbus_cancel_preventmode_query_locked (displaykeepalive_t *self);
static void              displaykeepalive_dbus_handle_preventmode_signal_locked(displaykeepalive_t *self, DBusMessage *sig);
static void              displaykeepalive_dbus_handle_nameowner_signal_locked  (displaykeepalive_t *self, DBusMessage *sig);
static DBusHandlerResult displaykeepalive_dbus_message_filter_cb               (DBusConnection *con, DBusMessage *msg, void *aptr);
static void              displaykeepalive_dbus_install_filter_locked           (displaykeepalive_t *self);
static void              displaykeepalive_dbus_remove_filter_locked            (displaykeepalive_t *self);
static void              displaykeepalive_dbus_connect_locked                  (displaykeepalive_t *self);
static void              displaykeepalive_dbus_disconnect_locked               (displaykeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * PREVENT_MODE_TRACKING
 * ------------------------------------------------------------------------- */

static preventmode_t displaykeepalive_preventmode_get_locked   (const displaykeepalive_t *self);
static void          displaykeepalive_preventmode_update_locked(displaykeepalive_t *self, preventmode_t state);

/* ------------------------------------------------------------------------- *
 * EXTERNAL_API
 * ------------------------------------------------------------------------- */

displaykeepalive_t *displaykeepalive_new  (void);
displaykeepalive_t *displaykeepalive_ref  (displaykeepalive_t *self);
void                displaykeepalive_unref(displaykeepalive_t *self);
void                displaykeepalive_start(displaykeepalive_t *self);
void                displaykeepalive_stop (displaykeepalive_t *self);

/* ========================================================================= *
 * OBJECT_LIFETIME
 * ========================================================================= */

#if LOGGING_TRACE_FUNCTIONS
static size_t displaykeepalive_ctor_cnt = 0;
static size_t displaykeepalive_dtor_cnt = 0;
static void displaykeepalive_report_stats_cb(void)
{
  fprintf(stderr, "displaykeepalive: ctor=%zd dtor=%zd diff=%zd\n",
          displaykeepalive_ctor_cnt,
          displaykeepalive_dtor_cnt,
          displaykeepalive_ctor_cnt - displaykeepalive_dtor_cnt);
}
#endif

/** Constructor for displaykeepalive_t objects
 */
static void
displaykeepalive_ctor(displaykeepalive_t *self)
{
#if LOGGING_TRACE_FUNCTIONS
    if( ++displaykeepalive_ctor_cnt == 1 )
        atexit(displaykeepalive_report_stats_cb);
#endif

    log_function("%p", self);

    /* Mark as valid */
    keepalive_object_ctor(&self->dka_object, "displaykeepalive",
                          displaykeepalive_shutdown_locked_cb,
                          displaykeepalive_delete_cb);
    self->dka_majick = DISPLAYKEEPALIVE_MAJICK_ALIVE;

    /* Session neither requested nor running */
    self->dka_requested = false;
    self->dka_session_renew_id = 0;

    /* No system bus connection */
    self->dka_connect_attempted = false;
    self->dka_systembus = 0;
    self->dka_filter_added = false;

    /* Prevent mode is not known */
    self->dka_preventmode = PREVENTMODE_UNKNOWN;
    self->dka_preventmode_pc = 0;

    /* MCE availability is not known */
    self->dka_mce_service = NAMEOWNER_UNKNOWN;
    self->dka_mce_service_pc = 0;

    /* No pending session rethink scheduled */
    self->dka_rethink_id = 0;
}

/** Callback for handling keepalive_object_t shutdown
 *
 * @param self  displaykeepalive object pointer
 */
static void
displaykeepalive_shutdown_locked_cb(gpointer aptr)
{
    displaykeepalive_t *self = aptr;

    log_function("%p", self);

    /* Forced stopping of keepalive session */
    displaykeepalive_rethink_now_locked(self);

    /* Disconnecting also cancels pending async method calls */
    displaykeepalive_dbus_disconnect_locked(self);
}

/** Callback for handling keepalive_object_t delete
 *
 * @param self  displaykeepalive object pointer
 */
static void
displaykeepalive_delete_cb(gpointer aptr)
{
    displaykeepalive_t *self = aptr;

    log_function("%p", self);

    displaykeepalive_dtor(self);
    free(self);
}

/** Destructor for displaykeepalive_t objects
 */
static void
displaykeepalive_dtor(displaykeepalive_t *self)
{
#if LOGGING_TRACE_FUNCTIONS
    ++displaykeepalive_dtor_cnt;
#endif

    log_function("%p", self);

    /* Mark as invalid */
    keepalive_object_dtor(&self->dka_object);
    self->dka_majick = DISPLAYKEEPALIVE_MAJICK_DEAD;
}

/** Predicate for: displaykeepalive_t object is valid
 */
static bool
displaykeepalive_is_valid(const displaykeepalive_t *self)
{
    /* Null pointers are tolerated */
    if( !self )
        return false;

    /* but obviously invalid pointers are not */
    if( self->dka_majick != DISPLAYKEEPALIVE_MAJICK_ALIVE )
        log_abort("invalid keepalive object: %p", self);

    return true;
}

/** Add external reference
 *
 * @param self  displaykeepalive object pointer
 */
static displaykeepalive_t *
displaykeepalive_ref_external_locked(displaykeepalive_t *self)
{
    log_function("%p", self);
    return keepalive_object_ref_external_locked(&self->dka_object);
}

/** Remove external reference
 *
 * @param self  displaykeepalive object pointer
 */
static void
displaykeepalive_unref_external_locked(displaykeepalive_t *self)
{
    log_function("%p", self);
    keepalive_object_unref_external_locked(&self->dka_object);
}

/** Lock displaykeepalive object
 *
 * Note: This is not recursive lock, incorrect lock/unlock
 *       sequences will lead to deadlocking / aborts.
 *
 * @param self  displaykeepalive object pointer
 */
static void
displaykeepalive_lock(displaykeepalive_t *self)
{
    log_function("%p", self);
    keepalive_object_lock(&self->dka_object);
}

/** Unlock displaykeepalive object
 *
 * @param self  displaykeepalive object pointer
 */
static void
displaykeepalive_unlock(displaykeepalive_t *self)
{
    log_function("%p", self);
    keepalive_object_unlock(&self->dka_object);
}

/** Validate and then lock displaykeepalive object
 *
 * @param self  displaykeepalive object pointer
 *
 * @return true if object is valid and got locked, false otherwise
 */
static bool
displaykeepalive_validate_and_lock(displaykeepalive_t *self)
{
    log_function("%p", self);

    if( !displaykeepalive_is_valid(self) )
        return false;

    displaykeepalive_lock(self);
    return true;
}

/** Predicate for: displaykeepalive object is getting shut down
 *
 * @param self    displaykeepalive object pointer
 *
 * @return true if object is in shutdown, false otherwise
 */
static bool
displaykeepalive_in_shutdown_locked(displaykeepalive_t *self)
{
    return keepalive_object_in_shutdown_locked(&self->dka_object);
}

/* ========================================================================= *
 * OBJECT_TIMERS
 * ========================================================================= */

static void
displaykeepalive_timer_start_locked(displaykeepalive_t *self, guint *timer_id,
                                    guint interval, GSourceFunc notify_cb)
{
    log_function("%p", self);
    keepalive_object_timer_start_locked(&self->dka_object, timer_id,
                                        interval, notify_cb);
}

static void
displaykeepalive_timer_stop_locked(displaykeepalive_t *self, guint *timer_id)
{
    log_function("%p", self);
    keepalive_object_timer_stop_locked(&self->dka_object, timer_id);
}

/* ========================================================================= *
 * OBJECT_DBUS_IPC
 * ========================================================================= */

static void
displaykeepalive_ipc_start_locked(displaykeepalive_t *self,
                                  DBusPendingCall **where,
                                  DBusPendingCallNotifyFunction notify_cb,
                                  const char *service,
                                  const char *object,
                                  const char *interface,
                                  const char *method,
                                  int arg_type, ...)
{
    log_function("%p", self);

    va_list va;
    va_start(va, arg_type);
    keepalive_object_ipc_start_locked_va(&self->dka_object, where, notify_cb,
                                         self->dka_systembus, service, object,
                                         interface, method, arg_type, va);
    va_end(va);
}

static void
displaykeepalive_ipc_cancel_locked(displaykeepalive_t *self,
                                   DBusPendingCall **where)
{
    log_function("%p", self);
    keepalive_object_ipc_cancel_locked(&self->dka_object, where);
}

static bool
displaykeepalive_ipc_finish_locked(displaykeepalive_t *self,
                                   DBusPendingCall **where,
                                   DBusPendingCall *what)
{
    (void)self;

    log_function("%p", self);
    return keepalive_object_ipc_finish_locked(&self->dka_object, where, what);
}

/* ========================================================================= *
 * KEEPALIVE_SESSION
 * ========================================================================= */

/** Helper for making MCE D-Bus method calls for which we want no reply
 */
static void
displaykeepalive_session_ipc_locked(displaykeepalive_t *self, const char *method)
{
    log_function("%p", self);

    if( displaykeepalive_mce_owner_get_locked(self) != NAMEOWNER_RUNNING )
        goto cleanup;

    xdbus_simple_call(self->dka_systembus,
                      MCE_SERVICE,
                      MCE_REQUEST_PATH,
                      MCE_REQUEST_IF,
                      method,
                      DBUS_TYPE_INVALID);
cleanup:
    return;
}

/** Timer callback for renewing display keepalive session
 */
static gboolean
displaykeepalive_session_renew_cb(gpointer aptr)
{
    gboolean keep_going = FALSE;
    displaykeepalive_t *self = aptr;

    log_function("%p", self);

    displaykeepalive_lock(self);

    if( displaykeepalive_in_shutdown_locked(self) )
        goto cleanup;

    if( !self->dka_session_renew_id  )
        goto cleanup;

    log_function("%p", self);

    displaykeepalive_session_ipc_locked(self, MCE_PREVENT_BLANK_REQ);
    keep_going = TRUE;

cleanup:
    if( !keep_going )
        self->dka_session_renew_id = 0;

    displaykeepalive_unlock(self);

    return keep_going;
}

/** Start display keepalive session
 */
static void
displaykeepalive_session_start_locked(displaykeepalive_t *self)
{
    if( self->dka_session_renew_id )
        goto cleanup;

    log_function("%p", self);

    displaykeepalive_timer_start_locked(self, &self->dka_session_renew_id,
                                        DISPLAY_KEEPALIVE_RENEW_MS,
                                        displaykeepalive_session_renew_cb);
    displaykeepalive_session_ipc_locked(self, MCE_PREVENT_BLANK_REQ);

cleanup:
    return;
}

/** Stop display keepalive session
 */
static void
displaykeepalive_session_stop_locked(displaykeepalive_t *self)
{
    if( !self->dka_session_renew_id )
        goto cleanup;

    log_function("%p", self);
    displaykeepalive_timer_stop_locked(self, &self->dka_session_renew_id);
    displaykeepalive_session_ipc_locked(self, MCE_CANCEL_PREVENT_BLANK_REQ);

cleanup:
    return;
}

/* ========================================================================= *
 * RETHINK_STATE
 * ========================================================================= */

static void
displaykeepalive_rethink_now_locked(displaykeepalive_t *self)
{
    bool need_renew_loop = false;

    log_function("%p", self);

    displaykeepalive_rethink_cancel_locked(self);

    /* Preventing display blanking is possible when MCE is running,
     * display is on and lockscreen is not active */

    if( displaykeepalive_in_shutdown_locked(self) )
        goto cleanup;

    if( displaykeepalive_mce_owner_get_locked(self) != NAMEOWNER_RUNNING )
        goto cleanup;

    if( displaykeepalive_preventmode_get_locked(self) != PREVENTMODE_ALLOWED )
        goto cleanup;

    need_renew_loop = self->dka_requested;

cleanup:

    if( need_renew_loop )
        displaykeepalive_session_start_locked(self);
    else
        displaykeepalive_session_stop_locked(self);
}

static gboolean
displaykeepalive_rethink_cb(gpointer aptr)
{
    displaykeepalive_t *self = aptr;

    log_function("%p", self);

    displaykeepalive_lock(self);

    // Skip if timer ought not to be active
    if( !self->dka_rethink_id )
        goto cleanup;

    self->dka_rethink_id = 0;

    // Skip if already shutting down
    if( displaykeepalive_in_shutdown_locked(self) )
        goto cleanup;

    displaykeepalive_rethink_now_locked(self);

cleanup:
    displaykeepalive_unlock(self);
    return G_SOURCE_REMOVE;
}

static void
displaykeepalive_rethink_schedule_locked(displaykeepalive_t *self)
{
    log_function("%p", self);

    if( displaykeepalive_in_shutdown_locked(self) )
        goto cleanup;

    if( self->dka_rethink_id )
        goto cleanup;

    displaykeepalive_timer_start_locked(self, &self->dka_rethink_id, 0,
                                        displaykeepalive_rethink_cb);
cleanup:
    return;
}

static void
displaykeepalive_rethink_cancel_locked(displaykeepalive_t *self)
{
    if( self->dka_rethink_id ) {
        log_function("%p", self);
        displaykeepalive_timer_stop_locked(self, &self->dka_rethink_id);
    }
}

/* ========================================================================= *
 * MCE_SERVICE_TRACKING
 * ========================================================================= */

static nameowner_t
displaykeepalive_mce_owner_get_locked(const displaykeepalive_t *self)
{
    return self->dka_mce_service;
}

static void
displaykeepalive_mce_owner_update_locked(displaykeepalive_t *self,
                                         nameowner_t state)
{
    log_function("%p", self);

    displaykeepalive_dbus_cancel_mce_owner_query_locked(self);

    if( self->dka_mce_service != state ) {
        log_notice(PFIX"MCE_SERVICE: %d -> %d", self->dka_mce_service, state);
        self->dka_mce_service = state;

        if( self->dka_mce_service == NAMEOWNER_RUNNING ) {
            displaykeepalive_dbus_start_preventmode_query_locked(self);
        }
        else {
            displaykeepalive_preventmode_update_locked(self, PREVENTMODE_UNKNOWN);
        }

        displaykeepalive_rethink_schedule_locked(self);
    }
}

static void
displaykeepalive_dbus_cancel_mce_owner_query_locked(displaykeepalive_t *self)
{
    log_function("%p", self);
    displaykeepalive_ipc_cancel_locked(self, &self->dka_mce_service_pc);
}

/* ========================================================================= *
 * PREVENT_MODE_TRACKING
 * ========================================================================= */

static preventmode_t
displaykeepalive_preventmode_get_locked(const displaykeepalive_t *self)
{
    return self->dka_preventmode;
}

static void
displaykeepalive_preventmode_update_locked(displaykeepalive_t *self,
                             preventmode_t state)
{
    log_function("%p", self);

    displaykeepalive_dbus_cancel_preventmode_query_locked(self);

    if( self->dka_preventmode != state ) {
        log_notice(PFIX"PREVENT_MODE: %d -> %d",
                   self->dka_preventmode, state);
        self->dka_preventmode = state;

        displaykeepalive_rethink_schedule_locked(self);
    }
}

/* ========================================================================= *
 * DBUS_CONNECTION
 * ========================================================================= */

static void
displaykeepalive_dbus_mce_owner_query_reply_cb(DBusPendingCall *pc, void *aptr)
{
    displaykeepalive_t *self = aptr;
    DBusMessage        *rsp  = 0;
    DBusError           err  = DBUS_ERROR_INIT;

    log_function("%p", self);

    displaykeepalive_lock(self);

    if( !displaykeepalive_ipc_finish_locked(self, &self->dka_mce_service_pc, pc) )
        goto cleanup;

    if( !(rsp = dbus_pending_call_steal_reply(pc)) )
        goto cleanup;

    const char *owner = 0;

    if( dbus_set_error_from_message(&err, rsp) ||
        !dbus_message_get_args(rsp, &err,
                               DBUS_TYPE_STRING, &owner,
                               DBUS_TYPE_INVALID) ) {
        if( g_strcmp0(err.name, DBUS_ERROR_NAME_HAS_NO_OWNER) )
            log_warning(PFIX"GetNameOwner reply: %s: %s", err.name, err.message);
    }

    displaykeepalive_mce_owner_update_locked(self,
                                             (owner && *owner) ?
                                             NAMEOWNER_RUNNING : NAMEOWNER_STOPPED);

cleanup:

    if( rsp )
        dbus_message_unref(rsp);

    dbus_error_free(&err);

    displaykeepalive_unlock(self);
}

static void
displaykeepalive_dbus_start_mce_owner_query_locked(displaykeepalive_t *self)
{
    if( self->dka_mce_service_pc )
        goto cleanup;

    log_function("%p", self);

    const char *arg = MCE_SERVICE;

    displaykeepalive_ipc_start_locked(self, &self->dka_mce_service_pc,
                                      displaykeepalive_dbus_mce_owner_query_reply_cb,
                                      DBUS_SERVICE_DBUS,
                                      DBUS_PATH_DBUS,
                                      DBUS_INTERFACE_DBUS,
                                      "GetNameOwner",
                                      DBUS_TYPE_STRING, &arg,
                                      DBUS_TYPE_INVALID);
cleanup:
    return;
}

static void
displaykeepalive_dbus_preventmode_reply_cb(DBusPendingCall *pc, void *aptr)
{
    displaykeepalive_t *self = aptr;
    DBusMessage        *rsp  = 0;
    DBusError           err  = DBUS_ERROR_INIT;

    log_function("%p", self);

    displaykeepalive_lock(self);

    if( !displaykeepalive_ipc_finish_locked(self, &self->dka_preventmode_pc, pc) )
        goto cleanup;

    if( !(rsp = dbus_pending_call_steal_reply(pc)) )
        goto cleanup;

    // reply to query == change signal
    displaykeepalive_dbus_handle_preventmode_signal_locked(self, rsp);

cleanup:

    if( rsp )
        dbus_message_unref(rsp);

    dbus_error_free(&err);

    displaykeepalive_unlock(self);
}

static void
displaykeepalive_dbus_start_preventmode_query_locked(displaykeepalive_t *self)
{
    if( self->dka_preventmode_pc )
        goto cleanup;

    log_function("%p", self);

    displaykeepalive_ipc_start_locked(self, &self->dka_preventmode_pc,
                                      displaykeepalive_dbus_preventmode_reply_cb,
                                      MCE_SERVICE,
                                      MCE_REQUEST_PATH,
                                      MCE_REQUEST_IF,
                                      MCE_PREVENT_BLANK_ALLOWED_GET,
                                      DBUS_TYPE_INVALID);
cleanup:
    return;
}

static void
displaykeepalive_dbus_cancel_preventmode_query_locked(displaykeepalive_t *self)
{
    log_function("%p", self);
    displaykeepalive_ipc_cancel_locked(self, &self->dka_preventmode_pc);
}

#define DBUS_NAMEOWENERCHANGED_SIG "NameOwnerChanged"

static void
displaykeepalive_dbus_handle_preventmode_signal_locked(displaykeepalive_t *self, DBusMessage *sig)
{
    log_function("%p", self);

    dbus_bool_t   value = FALSE;
    preventmode_t state = PREVENTMODE_UNKNOWN;

    DBusError err = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(sig, &err,
                               DBUS_TYPE_BOOLEAN, &value,
                               DBUS_TYPE_INVALID) ) {
        log_warning(PFIX"can't parse prevent mode signal: %s: %s",
                    err.name, err.message);
        goto cleanup;
    }

    if( value )
        state = PREVENTMODE_ALLOWED;
    else
        state = PREVENTMODE_DENIED;

    displaykeepalive_preventmode_update_locked(self, state);

cleanup:

    dbus_error_free(&err);

    return;
}

static void
displaykeepalive_dbus_handle_nameowner_signal_locked(displaykeepalive_t *self, DBusMessage *sig)
{
    log_function("%p", self);

    const char *name = 0;
    const char *prev = 0;
    const char *curr = 0;

    DBusError err = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(sig, &err,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &prev,
                               DBUS_TYPE_STRING, &curr,
                               DBUS_TYPE_INVALID) ) {
        log_warning(PFIX"can't parse name owner changed signal: %s: %s",
                    err.name, err.message);
        goto cleanup;
    }

    if( !g_strcmp0(name, MCE_SERVICE) ) {
        displaykeepalive_mce_owner_update_locked(self,
                                       *curr ? NAMEOWNER_RUNNING : NAMEOWNER_STOPPED);
    }

cleanup:

    dbus_error_free(&err);

    return;
}

/** D-Bus rule for listening to MCE name ownership changes */
static const char rule_nameowner_mce[] = ""
"type='signal'"
",sender='"DBUS_SERVICE_DBUS"'"
",path='"DBUS_PATH_DBUS"'"
",interface='"DBUS_INTERFACE_DBUS"'"
",member='"DBUS_NAMEOWENERCHANGED_SIG"'"
",arg0='"MCE_SERVICE"'"
;

/** D-Bus rule for listening to prevent mode changes */
static const char rule_preventmode[] = ""
"type='signal'"
",sender='"MCE_SERVICE"'"
",path='"MCE_SIGNAL_PATH"'"
",interface='"MCE_SIGNAL_IF"'"
",member='"MCE_PREVENT_BLANK_ALLOWED_SIG"'"
;

/** D-Bus message filter callback for handling signals
 */
static DBusHandlerResult
displaykeepalive_dbus_message_filter_cb(DBusConnection *con,
                                DBusMessage *msg,
                                void *aptr)
{
    (void)con;

    DBusHandlerResult result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    displaykeepalive_t *self = aptr;

    if( !msg )
        goto cleanup;

    if( dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL )
        goto cleanup;

    const char *interface = dbus_message_get_interface(msg);
    if( !interface )
        goto cleanup;

    const char *member = dbus_message_get_member(msg);
    if( !member )
        goto cleanup;

    log_function("%p %s.%s", self, interface, member);

    if( !strcmp(interface, MCE_SIGNAL_IF) ) {
        if( !strcmp(member, MCE_PREVENT_BLANK_ALLOWED_SIG) ) {
            displaykeepalive_lock(self);
            displaykeepalive_dbus_handle_preventmode_signal_locked(self, msg);
            displaykeepalive_unlock(self);
        }
    }
    else if( !strcmp(interface, DBUS_INTERFACE_DBUS) ) {
        if( !strcmp(member, DBUS_NAMEOWENERCHANGED_SIG) ) {
            displaykeepalive_lock(self);
            displaykeepalive_dbus_handle_nameowner_signal_locked(self, msg);
            displaykeepalive_unlock(self);
        }
    }

cleanup:
    return result;
}

/** Start listening to D-Bus signals
 */
static void
displaykeepalive_dbus_install_filter_locked(displaykeepalive_t *self)
{
    if( self->dka_filter_added )
        goto cleanup;

    log_function("%p", self);

    self->dka_filter_added =
        dbus_connection_add_filter(self->dka_systembus,
                                   displaykeepalive_dbus_message_filter_cb,
                                   self, 0);

    if( !self->dka_filter_added )
        goto cleanup;

    if( xdbus_connection_is_valid(self->dka_systembus) ){
        dbus_bus_add_match(self->dka_systembus, rule_nameowner_mce, 0);
        dbus_bus_add_match(self->dka_systembus, rule_preventmode, 0);
    }

cleanup:
    return;
}

/** Stop listening to D-Bus signals
 */
static void
displaykeepalive_dbus_remove_filter_locked(displaykeepalive_t *self)
{
    if( !self->dka_filter_added )
        goto cleanup;

    log_function("%p", self);

    self->dka_filter_added = false;

    dbus_connection_remove_filter(self->dka_systembus,
                                  displaykeepalive_dbus_message_filter_cb,
                                  self);

    if( xdbus_connection_is_valid(self->dka_systembus) ){
        dbus_bus_remove_match(self->dka_systembus, rule_nameowner_mce, 0);
        dbus_bus_remove_match(self->dka_systembus, rule_preventmode, 0);
    }

cleanup:
    return;
}

/** Connect to D-Bus System Bus
 */
static void
displaykeepalive_dbus_connect_locked(displaykeepalive_t *self)
{
    DBusError err = DBUS_ERROR_INIT;

    /* Attempt system bus connect only once */
    if( self->dka_connect_attempted )
        goto cleanup;

    log_function("%p", self);

    self->dka_connect_attempted = true;

    self->dka_systembus = dbus_bus_get(DBUS_BUS_SYSTEM, &err);

    if( !self->dka_systembus  ) {
        log_warning(PFIX"can't connect to system bus: %s: %s",
                    err.name, err.message);
        goto cleanup;
    }

    /* Assumption: The application itself is handling attaching
     *             the shared systembus connection to mainloop,
     *             either via dbus_connection_setup_with_g_main()
     *             or something equivalent. */

    /* Install signal filters */
    displaykeepalive_dbus_install_filter_locked(self);

    /* Initiate async MCE availability query */
    displaykeepalive_dbus_start_mce_owner_query_locked(self);

cleanup:

    dbus_error_free(&err);

    return;
}

/** Disconnect from D-Bus System Bus
 */
static void
displaykeepalive_dbus_disconnect_locked(displaykeepalive_t *self)
{
    /* If connection was not made, no need to undo stuff */
    if( !self->dka_systembus )
        goto cleanup;

    log_function("%p", self);

    /* Cancel any pending async method calls */
    displaykeepalive_dbus_cancel_mce_owner_query_locked(self);
    displaykeepalive_dbus_cancel_preventmode_query_locked(self);

    /* Remove signal filters */
    displaykeepalive_dbus_remove_filter_locked(self);

    /* Detach from system bus */
    dbus_connection_unref(self->dka_systembus),
        self->dka_systembus = 0;

    /* Note: As we do not clear dka_connect_attempted flag,
     *       re-connecting this object is not possible */

cleanup:

    return;
}

/* ========================================================================= *
 * EXTERNAL_API  --  documented in: keepalive-displaykeepalive.h
 * ========================================================================= */

displaykeepalive_t *
displaykeepalive_new(void)
{
    displaykeepalive_t *self = calloc(1, sizeof *self);

    log_function("APICALL %p", self);

    if( self )
        displaykeepalive_ctor(self);

    return self;
}

displaykeepalive_t *
displaykeepalive_ref(displaykeepalive_t *self)
{
    log_function("APICALL %p", self);

    displaykeepalive_t *ref = 0;

    if( displaykeepalive_validate_and_lock(self) ) {
        ref = displaykeepalive_ref_external_locked(self);
        displaykeepalive_unlock(self);
    }

    return ref;
}

void
displaykeepalive_unref(displaykeepalive_t *self)
{
    log_function("APICALL %p", self);

    if( displaykeepalive_validate_and_lock(self) ) {
        displaykeepalive_unref_external_locked(self);
        displaykeepalive_unlock(self);
    }
}

void
displaykeepalive_start(displaykeepalive_t *self)
{
    log_function("APICALL %p", self);

    if( displaykeepalive_validate_and_lock(self) ) {
        if( !self->dka_requested ) {
            /* Set we-want-to-prevent-blanking flag */
            self->dka_requested = true;

            /* Connect to systembus */
            displaykeepalive_dbus_connect_locked(self);

            /* Check if keepalive session can be started */
            displaykeepalive_rethink_schedule_locked(self);
        }
        displaykeepalive_unlock(self);
    }
}

void
displaykeepalive_stop(displaykeepalive_t *self)
{
    log_function("APICALL %p", self);

    if( displaykeepalive_validate_and_lock(self) ) {
        if( self->dka_requested ) {
            /* Clear we-want-to-prevent-blanking flag */
            self->dka_requested = false;

            /* Check if keepalive session needs to be stopped */
            displaykeepalive_rethink_schedule_locked(self);
        }
        displaykeepalive_unlock(self);
    }
}
