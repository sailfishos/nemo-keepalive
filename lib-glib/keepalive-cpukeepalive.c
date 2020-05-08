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

#include "keepalive-cpukeepalive.h"
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

/** Assumed renew period used while D-Bus query has not been made yet */
#define CPU_KEEPALIVE_RENEW_MS (60 * 1000)

/** Logging prefix for this module */
#define PFIX "cpukeepalive: "

/* ========================================================================= *
 * GENERIC HELPERS
 * ========================================================================= */

static inline bool
eq(const char *a, const char *b)
{
    return (a && b) ? !strcmp(a, b) : (a == b);
}

/* ========================================================================= *
 * TYPES
 * ========================================================================= */

/** Enumeration of states a D-Bus service can be in */
typedef enum {
    NAMEOWNER_UNKNOWN,
    NAMEOWNER_STOPPED,
    NAMEOWNER_RUNNING,
} nameowner_t;

/** Memory tag for marking live cpukeepalive_t objects */
#define CPUKEEPALIVE_MAJICK_ALIVE 0x548ec404

/** Memory tag for marking dead cpukeepalive_t objects */
#define CPUKEEPALIVE_MAJICK_DEAD  0x00000000

/** CPU-keepalive state object
 */
struct cpukeepalive_t
{
    /* Base object for locking and refcounting */
    keepalive_object_t    cka_object;

    /** Simple memory tag to catch usage of obviously bogus
     *  cpukeepalive_t pointers */
    unsigned         cka_majick;

    /** Unique identifier string */
    char            *cka_id;

    /** Flag for: preventing device suspend requested */
    bool             cka_requested;

    /** Flag for: we've already tried to connect to system bus */
    bool             cka_connect_attempted;

    /** System bus connection */
    DBusConnection  *cka_systembus;

    /** Flag for: signal filters installed */
    bool             cka_filter_added;

    /** Current com.nokia.mce name ownership state */
    nameowner_t      cka_mce_service;

    /** Async D-Bus query for initial cka_mce_service value */
    DBusPendingCall *cka_mce_service_pc;

    /** Timer id for active CPU-keepalive session */
    guint            cka_session_renew_id;

    /** Renew delay for active CPU-keepalive session */
    guint            cka_renew_period_ms;

    /** Async D-Bus query for initial cka_mce_service value */
    DBusPendingCall *cka_renew_period_pc;

    /** Timer id for delayed D-Bus connect */
    guint            cka_delayed_connect_id;

    /** Timer id for delayed session rething */
    guint            cka_delayed_rethink_id;

    // NOTE: cpukeepalive_ctor & cpukeepalive_dtor
};

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * SESSION_ID
 * ------------------------------------------------------------------------- */

static char       *cpukeepalive_generate_id  (void);
static const char *cpukeepalive_get_id_locked(const cpukeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * OBJECT_LIFETIME
 * ------------------------------------------------------------------------- */

static void            cpukeepalive_ctor                 (cpukeepalive_t *self);
static void            cpukeepalive_shutdown_locked_cb   (gpointer aptr);
static void            cpukeepalive_delete_cb            (gpointer aptr);
static void            cpukeepalive_dtor                 (cpukeepalive_t *self);
static bool            cpukeepalive_is_valid             (const cpukeepalive_t *self);
static cpukeepalive_t *cpukeepalive_ref_external_locked  (cpukeepalive_t *self);
static void            cpukeepalive_unref_external_locked(cpukeepalive_t *self);
static void            cpukeepalive_lock                 (cpukeepalive_t *self);
static void            cpukeepalive_unlock               (cpukeepalive_t *self);
static bool            cpukeepalive_validate_and_lock    (cpukeepalive_t *self);
static bool            cpukeepalive_in_shutdown_locked   (cpukeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * OBJECT_TIMERS
 * ------------------------------------------------------------------------- */

static void cpukeepalive_timer_start_locked(cpukeepalive_t *self, guint *timer_id, guint interval, GSourceFunc notify_cb);
static void cpukeepalive_timer_stop_locked (cpukeepalive_t *self, guint *timer_id);

/* ------------------------------------------------------------------------- *
 * OBJECT_DBUS_IPC
 * ------------------------------------------------------------------------- */

static void cpukeepalive_ipc_start_locked (cpukeepalive_t *self, DBusPendingCall **where, DBusPendingCallNotifyFunction notify_cb, const char *service, const char *object, const char *interface, const char *method, int arg_type, ...);
static void cpukeepalive_ipc_cancel_locked(cpukeepalive_t *self, DBusPendingCall **where);
static bool cpukeepalive_ipc_finish_locked(cpukeepalive_t *self, DBusPendingCall **where, DBusPendingCall *what);

/* ------------------------------------------------------------------------- *
 * RENEW_PERIOD
 * ------------------------------------------------------------------------- */

static guint cpukeepalive_renew_period_get_locked         (const cpukeepalive_t *self);
static void  cpukeepalive_renew_period_set_locked         (cpukeepalive_t *self, int delay_ms);
static void  cpukeepalive_renew_period_query_reply_cb     (DBusPendingCall *pc, void *aptr);
static void  cpukeepalive_renew_period_start_query_locked (cpukeepalive_t *self);
static void  cpukeepalive_renew_period_cancel_query_locked(cpukeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * KEEPALIVE_SESSION
 * ------------------------------------------------------------------------- */

static void     cpukeepalive_session_ipc_locked    (cpukeepalive_t *self, const char *method);
static gboolean cpukeepalive_session_renew_cb      (gpointer aptr);
static void     cpukeepalive_session_start_locked  (cpukeepalive_t *self);
static void     cpukeepalive_session_restart_locked(cpukeepalive_t *self);
static void     cpukeepalive_session_stop_locked   (cpukeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * STATE_EVALUATION
 * ------------------------------------------------------------------------- */

static void     cpukeepalive_rethink_now_locked     (cpukeepalive_t *self);
static gboolean cpukeepalive_rethink_cb             (gpointer aptr);
static void     cpukeepalive_rethink_schedule_locked(cpukeepalive_t *self);
static void     cpukeepalive_rethink_cancel_locked  (cpukeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * MCE_TRACKING
 * ------------------------------------------------------------------------- */

static nameowner_t cpukeepalive_mce_owner_get_locked         (const cpukeepalive_t *self);
static void        cpukeepalive_mce_owner_set_locked         (cpukeepalive_t *self, nameowner_t state);
static void        cpukeepalive_mce_owner_query_reply_cb     (DBusPendingCall *pc, void *aptr);
static void        cpukeepalive_mce_owner_start_query_locked (cpukeepalive_t *self);
static void        cpukeepalive_mce_owner_cancel_query_locked(cpukeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * DBUS_GLUE
 * ------------------------------------------------------------------------- */

static void              cpukeepalive_dbus_nameowner_signal_cb  (cpukeepalive_t *self, DBusMessage *sig);
static DBusHandlerResult cpukeepalive_dbus_filter_cb            (DBusConnection *con, DBusMessage *msg, void *aptr);
static void              cpukeepalive_dbus_filter_install_locked(cpukeepalive_t *self);
static void              cpukeepalive_dbus_filter_remove_locked (cpukeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * DBUS_CONNECTION
 * ------------------------------------------------------------------------- */

static void     cpukeepalive_connect_now_locked   (cpukeepalive_t *self);
static void     cpukeepalive_disconnect_now_locked(cpukeepalive_t *self);
static gboolean cpukeepalive_connect_cb           (gpointer aptr);
static void     cpukeepalive_connect_later_locked (cpukeepalive_t *self);

/* ------------------------------------------------------------------------- *
 * EXTERNAL_API
 * ------------------------------------------------------------------------- */

cpukeepalive_t *cpukeepalive_new   (void);
cpukeepalive_t *cpukeepalive_ref   (cpukeepalive_t *self);
void            cpukeepalive_unref (cpukeepalive_t *self);
void            cpukeepalive_start (cpukeepalive_t *self);
void            cpukeepalive_stop  (cpukeepalive_t *self);
const char     *cpukeepalive_get_id(const cpukeepalive_t *self);

/* ========================================================================= *
 * HAXOR
 * ========================================================================= */

#if LOGGING_TRACE_FUNCTIONS
static size_t cpukeepalive_ctor_cnt = 0;
static size_t cpukeepalive_dtor_cnt = 0;
static void cpukeepalive_report_stats_cb(void);
static void cpukeepalive_report_stats_cb(void)
{
    fprintf(stderr, "cpukeepalive: ctor=%zd dtor=%zd diff=%zd\n",
            cpukeepalive_ctor_cnt,
            cpukeepalive_dtor_cnt,
            cpukeepalive_ctor_cnt - cpukeepalive_dtor_cnt);
}
#endif

/* ========================================================================= *
 * SESSION_ID
 * ========================================================================= */

/** Generate keepalive id for ipc with mce
 *
 * Needs to be unique within process.
 */
static char *cpukeepalive_generate_id(void)
{
    static _Atomic unsigned count = 0;

    log_enter_function();

    unsigned id = ++count;
    char buf[64];
    snprintf(buf, sizeof buf, "glib_cpu_keepalive_%u", id);
    return strdup(buf);
}

/** Get session ID string
 */
static const char *
cpukeepalive_get_id_locked(const cpukeepalive_t *self)
{
    return self->cka_id;
}

/* ========================================================================= *
 * OBJECT_LIFETIME
 * ========================================================================= */

/** Constructor for cpukeepalive_t objects
 */
static void
cpukeepalive_ctor(cpukeepalive_t *self)
{
#if LOGGING_TRACE_FUNCTIONS
    if( ++cpukeepalive_ctor_cnt == 1 )
        atexit(cpukeepalive_report_stats_cb);
#endif

    log_function("%p", self);

    /* Mark as valid */
    keepalive_object_ctor(&self->cka_object, "cpukeepalive",
                          cpukeepalive_shutdown_locked_cb,
                          cpukeepalive_delete_cb);
    self->cka_majick = CPUKEEPALIVE_MAJICK_ALIVE;

    /* Assign unique (within process) id for use with MCE D-Bus IPC */
    self->cka_id = cpukeepalive_generate_id();

    /* Session neither requested nor running */
    self->cka_requested = false;
    self->cka_session_renew_id = 0;

    /* No system bus connection */
    self->cka_connect_attempted = false;
    self->cka_systembus = 0;
    self->cka_filter_added = false;

    /* MCE availability is not known */
    self->cka_mce_service = NAMEOWNER_UNKNOWN;
    self->cka_mce_service_pc = 0;

    /* Renew period is unknown */
    self->cka_renew_period_ms = 0;
    self->cka_renew_period_pc = 0;

    /* D-Bus connect is not scheduled */
    self->cka_delayed_connect_id = 0;

    /* Session rethink is not scheduled */
    self->cka_delayed_rethink_id = 0;

    /* Note: Any initialization that might cause callbacks
     *       to trigger in other threads must happen while
     *       holding data lock.
     */
    cpukeepalive_lock(self);

    /* Connect to systembus */
    cpukeepalive_connect_later_locked(self);

    cpukeepalive_unlock(self);
}

/** Callback for handling keepalive_object_t shutdown
 *
 * @param self  cpukeepalive object pointer
 */
static void
cpukeepalive_shutdown_locked_cb(gpointer aptr)
{
    cpukeepalive_t *self = aptr;

    /* Cancel any pending async method calls */
    cpukeepalive_mce_owner_cancel_query_locked(self);
    cpukeepalive_renew_period_cancel_query_locked(self);

    /* Stop session and renew loop if necessary */
    cpukeepalive_rethink_cancel_locked(self);
    cpukeepalive_rethink_now_locked(self);

    /* Disconnect from system bus */
    cpukeepalive_disconnect_now_locked(self);

    log_function("%p", self);

}

/** Callback for handling keepalive_object_t delete
 *
 * @param self  cpukeepalive object pointer
 */
static void
cpukeepalive_delete_cb(gpointer aptr)
{
    cpukeepalive_t *self = aptr;
    cpukeepalive_dtor(self);
    free(self);
}

/** Destructor for cpukeepalive_t objects
 */
static void
cpukeepalive_dtor(cpukeepalive_t *self)
{
#if LOGGING_TRACE_FUNCTIONS
    ++cpukeepalive_dtor_cnt;
#endif

    log_function("%p", self);

    /* Free id string */
    free(self->cka_id),
        self->cka_id = 0;

    /* Mark as invalid */
    keepalive_object_dtor(&self->cka_object);
    self->cka_majick = CPUKEEPALIVE_MAJICK_DEAD;
}

/** Predicate for: cpukeepalive_t object is valid
 */
static bool
cpukeepalive_is_valid(const cpukeepalive_t *self)
{
    /* Null pointers are tolerated */
    if( !self )
        return false;

    /* but obviously invalid pointers are not */
    if( self->cka_majick != CPUKEEPALIVE_MAJICK_ALIVE )
        log_abort("invalid keepalive object: %p", self);

    return true;
}

/** Add external reference
 *
 * @param self  cpukeepalive object pointer
 */
static cpukeepalive_t *
cpukeepalive_ref_external_locked(cpukeepalive_t *self)
{
    return keepalive_object_ref_external_locked(&self->cka_object);
}

/** Remove external reference
 *
 * @param self  cpukeepalive object pointer
 */
static void
cpukeepalive_unref_external_locked(cpukeepalive_t *self)
{
    keepalive_object_unref_external_locked(&self->cka_object);
}

/** Lock cpukeepalive object
 *
 * Note: This is not recursive lock, incorrect lock/unlock
 *       sequences will lead to deadlocking / aborts.
 *
 * @param self  cpukeepalive object pointer
 */
static void
cpukeepalive_lock(cpukeepalive_t *self)
{
    keepalive_object_lock(&self->cka_object);
}

/** Unlock cpukeepalive object
 *
 * @param self  cpukeepalive object pointer
 */
static void
cpukeepalive_unlock(cpukeepalive_t *self)
{
    keepalive_object_unlock(&self->cka_object);
}

/** Validate and then lock cpukeepalive object
 *
 * @param self  cpukeepalive object pointer
 *
 * @return true if object is valid and got locked, false otherwise
 */
static bool
cpukeepalive_validate_and_lock(cpukeepalive_t *self)
{
    if( !cpukeepalive_is_valid(self) )
        return false;

    cpukeepalive_lock(self);
    return true;
}

/** Predicate for: cpukeepalive object is getting shut down
 *
 * @param self    cpukeepalive object pointer
 *
 * @return true if object is in shutdown, false otherwise
 */
static bool
cpukeepalive_in_shutdown_locked(cpukeepalive_t *self)
{
    return keepalive_object_in_shutdown_locked(&self->cka_object);
}

/* ========================================================================= *
 * OBJECT_TIMERS
 * ========================================================================= */

static void
cpukeepalive_timer_start_locked(cpukeepalive_t *self, guint *timer_id,
                           guint interval, GSourceFunc notify_cb)
{
    keepalive_object_timer_start_locked(&self->cka_object, timer_id,
                                        interval, notify_cb);
}

static void
cpukeepalive_timer_stop_locked(cpukeepalive_t *self, guint *timer_id)
{
    keepalive_object_timer_stop_locked(&self->cka_object, timer_id);
}

/* ========================================================================= *
 * OBJECT_DBUS_IPC
 * ========================================================================= */

static void
cpukeepalive_ipc_start_locked(cpukeepalive_t *self,
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
    keepalive_object_ipc_start_locked_va(&self->cka_object, where, notify_cb,
                                         self->cka_systembus, service, object,
                                         interface, method, arg_type, va);
    va_end(va);
}

static void
cpukeepalive_ipc_cancel_locked(cpukeepalive_t *self,
                               DBusPendingCall **where)
{
    log_function("%p", self);
    keepalive_object_ipc_cancel_locked(&self->cka_object, where);
}

static bool
cpukeepalive_ipc_finish_locked(cpukeepalive_t *self,
                               DBusPendingCall **where,
                               DBusPendingCall *what)
{
    (void)self;

    log_function("%p", self);
    return keepalive_object_ipc_finish_locked(&self->cka_object, where, what);
}

/* ========================================================================= *
 * RENEW_PERIOD
 * ========================================================================= */

static guint
cpukeepalive_renew_period_get_locked(const cpukeepalive_t *self)
{
    return self->cka_renew_period_ms ?: CPU_KEEPALIVE_RENEW_MS;
}

static void
cpukeepalive_renew_period_set_locked(cpukeepalive_t *self, int delay_ms)
{
    log_function("%p", self);

    guint delay_old = cpukeepalive_renew_period_get_locked(self);

    if( delay_ms <= 0 )
        self->cka_renew_period_ms = CPU_KEEPALIVE_RENEW_MS;
    else
        self->cka_renew_period_ms = delay_ms;

    guint delay_new = cpukeepalive_renew_period_get_locked(self);

    log_notice(PFIX"renew period: %d", delay_new);

    if( delay_old != delay_new )
        cpukeepalive_session_restart_locked(self);
}

static void
cpukeepalive_renew_period_query_reply_cb(DBusPendingCall *pc, void *aptr)
{
    cpukeepalive_t *self = aptr;
    DBusMessage    *rsp  = 0;
    DBusError       err  = DBUS_ERROR_INIT;

    log_function("%p", self);

    cpukeepalive_lock(self);

    if( !cpukeepalive_ipc_finish_locked(self, &self->cka_renew_period_pc, pc) )
        goto cleanup;

    /* Note: We do not want to repeat this even if query or parsing
     *       the reply fails -> set regardless ofsuccess/failure.
     *       If value is still zero at that time, built-in default
     *       is used.
     */
    dbus_int32_t val = 0;
    if( (rsp = dbus_pending_call_steal_reply(pc)) ) {
        if( dbus_set_error_from_message(&err, rsp) ||
            !dbus_message_get_args(rsp, &err,
                                   DBUS_TYPE_INT32, &val,
                                   DBUS_TYPE_INVALID) ) {
            log_warning(PFIX"renew period reply: %s: %s",
                        err.name, err.message);
        }
    }
    cpukeepalive_renew_period_set_locked(self, val * 1000);

cleanup:
    dbus_error_free(&err);
    if( rsp )
        dbus_message_unref(rsp);
    cpukeepalive_unlock(self);
}

static void
cpukeepalive_renew_period_start_query_locked(cpukeepalive_t *self)
{
    /* Shutting down? */
    if( cpukeepalive_in_shutdown_locked(self) )
        goto cleanup;

    /* Already known? */
    if( self->cka_renew_period_ms )
        goto cleanup;

    /* Already in progress? */
    if( self->cka_renew_period_pc )
        goto cleanup;

    log_function("%p", self);

    const char *arg = cpukeepalive_get_id_locked(self);
    cpukeepalive_ipc_start_locked(self,
                                  &self->cka_renew_period_pc,
                                  cpukeepalive_renew_period_query_reply_cb,
                                  MCE_SERVICE,
                                  MCE_REQUEST_PATH,
                                  MCE_REQUEST_IF,
                                  MCE_CPU_KEEPALIVE_PERIOD_REQ,
                                  DBUS_TYPE_STRING, &arg,
                                  DBUS_TYPE_INVALID);

cleanup:
    return;
}

static void
cpukeepalive_renew_period_cancel_query_locked(cpukeepalive_t *self)
{
    if( self->cka_renew_period_pc ) {
        log_function("%p", self);
        cpukeepalive_ipc_cancel_locked(self, &self->cka_renew_period_pc);
    }
}

/* ========================================================================= *
 * KEEPALIVE_SESSION
 * ========================================================================= */

/** Helper for making MCE D-Bus method calls for which we want no reply
 */
static void
cpukeepalive_session_ipc_locked(cpukeepalive_t *self, const char *method)
{
    log_function("%p %s(%s)", self, method, cpukeepalive_get_id_locked(self));

    if( xdbus_connection_is_valid(self->cka_systembus) ) {
        const char *arg = cpukeepalive_get_id_locked(self);
        xdbus_simple_call(self->cka_systembus,
                          MCE_SERVICE,
                          MCE_REQUEST_PATH,
                          MCE_REQUEST_IF,
                          method,
                          DBUS_TYPE_STRING, &arg,
                          DBUS_TYPE_INVALID);

        /* We need to make sure these method call messages are
         * actually sent as soon as possible.
         *
         * As flushing output can lead to incoming messages getting
         * dispatched, we must unlock while doing it to avoid
         * deadlocks. Which in turn means that the cached connection
         * reference can't be relied to stay valid.
         */
        DBusConnection *con = dbus_connection_ref(self->cka_systembus);
        cpukeepalive_unlock(self);
        dbus_connection_flush(con);
        cpukeepalive_lock(self);
        dbus_connection_unref(con);
    }
}

/** Timer callback for renewing CPU-keepalive session
 */
static gboolean
cpukeepalive_session_renew_cb(gpointer aptr)
{
    gboolean        result = G_SOURCE_REMOVE;
    cpukeepalive_t *self   = aptr;

    log_function("%p", self);

    cpukeepalive_lock(self);

    if( self->cka_session_renew_id ) {
        cpukeepalive_session_ipc_locked(self, MCE_CPU_KEEPALIVE_START_REQ);
        result = G_SOURCE_CONTINUE;
    }

    cpukeepalive_unlock(self);

    return result;
}

/** Start CPU-keepalive session
 */
static void
cpukeepalive_session_start_locked(cpukeepalive_t *self)
{
    /* skip if already running */
    if( self->cka_session_renew_id )
        goto cleanup;

    log_function("%p", self);

    cpukeepalive_session_ipc_locked(self, MCE_CPU_KEEPALIVE_START_REQ);

    cpukeepalive_timer_start_locked(self, &self->cka_session_renew_id,
                                    cpukeepalive_renew_period_get_locked(self),
                                    cpukeepalive_session_renew_cb);
cleanup:
    return;
}

/** Restart CPU-keepalive session after renew delay change
 */
static void
cpukeepalive_session_restart_locked(cpukeepalive_t *self)
{
    /* skip if not already running */
    if( !self->cka_session_renew_id )
        goto cleanup;

    log_function("%p", self);

    cpukeepalive_session_ipc_locked(self, MCE_CPU_KEEPALIVE_START_REQ);

    cpukeepalive_timer_start_locked(self, &self->cka_session_renew_id,
                                    cpukeepalive_renew_period_get_locked(self),
                                    cpukeepalive_session_renew_cb);
cleanup:
    return;
}

/** Stop CPU-keepalive session
 */
static void
cpukeepalive_session_stop_locked(cpukeepalive_t *self)
{
    /* skip if not running */
    if( !self->cka_session_renew_id )
        goto cleanup;

    log_function("%p", self);

    cpukeepalive_timer_stop_locked(self, &self->cka_session_renew_id);

    cpukeepalive_session_ipc_locked(self, MCE_CPU_KEEPALIVE_STOP_REQ);

cleanup:
    return;
}

/* ========================================================================= *
 * STATE_EVALUATION
 * ========================================================================= */

static void
cpukeepalive_rethink_now_locked(cpukeepalive_t *self)
{
    log_function("%p", self);

    /* This function potentially cancels timers and flushes
     * dbus connection -> to avoid multithread issues, must not be called
     * directly from API functions.
     */

    cpukeepalive_rethink_cancel_locked(self);

    /* Default to stopping renew loop */
    bool need_renew_loop = false;

    /* Shutting down? */
    if( cpukeepalive_in_shutdown_locked(self) )
        goto cleanup;

    /* MCE is running? */
    if( cpukeepalive_mce_owner_get_locked(self) != NAMEOWNER_RUNNING )
        goto cleanup;

    /* Act based on requested state */
    need_renew_loop = self->cka_requested;

cleanup:

    if( need_renew_loop )
        cpukeepalive_session_start_locked(self);
    else
        cpukeepalive_session_stop_locked(self);
}

static gboolean
cpukeepalive_rethink_cb(gpointer aptr)
{
    cpukeepalive_t *self = aptr;

    log_function("%p", self);
    cpukeepalive_lock(self);

    if( self->cka_delayed_rethink_id ) {
        self->cka_delayed_rethink_id = 0;
        cpukeepalive_rethink_now_locked(self);
    }

    cpukeepalive_unlock(self);
    return G_SOURCE_REMOVE;
}

static void
cpukeepalive_rethink_schedule_locked(cpukeepalive_t *self)
{
    if( !self->cka_delayed_rethink_id ) {
        log_function("%p", self);
        cpukeepalive_timer_start_locked(self, &self->cka_delayed_rethink_id,
                                        0, cpukeepalive_rethink_cb);
    }
}

static void
cpukeepalive_rethink_cancel_locked(cpukeepalive_t *self)
{
    if( self->cka_delayed_rethink_id ) {
        log_function("%p", self);
        cpukeepalive_timer_stop_locked(self, &self->cka_delayed_rethink_id);
    }
}

/* ========================================================================= *
 * MCE_TRACKING
 * ========================================================================= */

static nameowner_t
cpukeepalive_mce_owner_get_locked(const cpukeepalive_t *self)
{
    return self->cka_mce_service;
}

static void
cpukeepalive_mce_owner_set_locked(cpukeepalive_t *self, nameowner_t state)
{
    log_function("%p", self);

    if( self->cka_mce_service != state ) {
        log_notice(PFIX"MCE_SERVICE: %d -> %d",
                   self->cka_mce_service, state);
        self->cka_mce_service = state;

        if( self->cka_mce_service == NAMEOWNER_RUNNING )
            cpukeepalive_renew_period_start_query_locked(self);

        cpukeepalive_rethink_schedule_locked(self);
    }
}

static void
cpukeepalive_mce_owner_query_reply_cb(DBusPendingCall *pc, void *aptr)
{
    cpukeepalive_t *self = aptr;
    DBusMessage    *rsp  = 0;
    DBusError       err  = DBUS_ERROR_INIT;

    log_function("%p", self);

    cpukeepalive_lock(self);

    if( !cpukeepalive_ipc_finish_locked(self, &self->cka_mce_service_pc, pc) )
        goto cleanup;

    if( !(rsp = dbus_pending_call_steal_reply(pc)) )
        goto cleanup;

    const char *owner = 0;
    if( dbus_set_error_from_message(&err, rsp) ||
        !dbus_message_get_args(rsp, &err,
                               DBUS_TYPE_STRING, &owner,
                               DBUS_TYPE_INVALID) ) {
        if( strcmp(err.name, DBUS_ERROR_NAME_HAS_NO_OWNER) )
            log_warning(PFIX"GetNameOwner reply: %s: %s", err.name, err.message);
    }
    cpukeepalive_mce_owner_set_locked(self, (owner && *owner) ?
                                      NAMEOWNER_RUNNING : NAMEOWNER_STOPPED);

cleanup:

    if( rsp )
        dbus_message_unref(rsp);

    dbus_error_free(&err);

    cpukeepalive_unlock(self);
}

static void
cpukeepalive_mce_owner_start_query_locked(cpukeepalive_t *self)
{
    /* Shutting down? */
    if( cpukeepalive_in_shutdown_locked(self) )
        goto cleanup;

    /* Already in progress? */
    if( self->cka_mce_service_pc )
        goto cleanup;

    log_function("%p", self);

    const char *arg = MCE_SERVICE;
    cpukeepalive_ipc_start_locked(self,
                                  &self->cka_mce_service_pc,
                                  cpukeepalive_mce_owner_query_reply_cb,
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
cpukeepalive_mce_owner_cancel_query_locked(cpukeepalive_t *self)
{
    if( self->cka_mce_service_pc ) {
        log_function("%p", self);
        cpukeepalive_ipc_cancel_locked(self, &self->cka_mce_service_pc);
    }
}

/* ========================================================================= *
 * DBUS_GLUE
 * ========================================================================= */

#define DBUS_NAMEOWENERCHANGED_SIG "NameOwnerChanged"

static void
cpukeepalive_dbus_nameowner_signal_cb(cpukeepalive_t *self, DBusMessage *sig)
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

    if( eq(name, MCE_SERVICE) ) {
        cpukeepalive_lock(self);
        cpukeepalive_mce_owner_set_locked(self,
                                          *curr ? NAMEOWNER_RUNNING : NAMEOWNER_STOPPED);
        cpukeepalive_unlock(self);
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

/** D-Bus message filter callback for handling signals
 */
static DBusHandlerResult
cpukeepalive_dbus_filter_cb(DBusConnection *con, DBusMessage *msg, void *aptr)
{
    (void)con;

    DBusHandlerResult  result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    cpukeepalive_t    *self   = aptr;

    log_function("%p", self);

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

    if( !strcmp(interface, DBUS_INTERFACE_DBUS) ) {
        if( !strcmp(member, DBUS_NAMEOWENERCHANGED_SIG) )
            cpukeepalive_dbus_nameowner_signal_cb(self, msg);
    }

cleanup:
    return result;
}

/** Start listening to D-Bus signals
 */
static void
cpukeepalive_dbus_filter_install_locked(cpukeepalive_t *self)
{
    if( self->cka_filter_added )
        goto cleanup;

    log_function("%p", self);

    self->cka_filter_added =
        dbus_connection_add_filter(self->cka_systembus,
                                   cpukeepalive_dbus_filter_cb,
                                   self, 0);

    if( !self->cka_filter_added )
        goto cleanup;

    if( xdbus_connection_is_valid(self->cka_systembus) )
        dbus_bus_add_match(self->cka_systembus, rule_nameowner_mce, 0);

cleanup:
    return;
}

/** Stop listening to D-Bus signals
 */
static void
cpukeepalive_dbus_filter_remove_locked(cpukeepalive_t *self)
{
    if( !self->cka_filter_added )
        goto cleanup;

    self->cka_filter_added = false;

    log_function("%p", self);

    if( self->cka_systembus )
        dbus_connection_remove_filter(self->cka_systembus,
                                      cpukeepalive_dbus_filter_cb,
                                      self);

    if( xdbus_connection_is_valid(self->cka_systembus) )
        dbus_bus_remove_match(self->cka_systembus, rule_nameowner_mce, 0);

cleanup:
    return;
}

/* ========================================================================= *
 * DBUS_CONNECTION
 * ========================================================================= */

/** Connect to D-Bus System Bus
 */
static void
cpukeepalive_connect_now_locked(cpukeepalive_t *self)
{
    DBusError err = DBUS_ERROR_INIT;

    log_function("%p", self);

    /* Handling now -> stop timer */
    cpukeepalive_timer_stop_locked(self, &self->cka_delayed_connect_id);

    /* Skip if we are already shutting down */
    if( cpukeepalive_in_shutdown_locked(self) )
        goto cleanup;

    /* Attempt system bus connect only once */
    if( self->cka_connect_attempted )
        goto cleanup;

    self->cka_connect_attempted = true;

    self->cka_systembus = dbus_bus_get(DBUS_BUS_SYSTEM, &err);

    if( !self->cka_systembus  ) {
        log_warning(PFIX"can't connect to system bus: %s: %s",
                    err.name, err.message);
        goto cleanup;
    }

    /* Assumption: The application itself is handling attaching
     *             the shared systembus connection to mainloop,
     *             either via dbus_connection_setup_with_g_main()
     *             or something equivalent. */

    /* Install signal filters */
    cpukeepalive_dbus_filter_install_locked(self);

    /* Initiate async MCE availability query */
    cpukeepalive_mce_owner_start_query_locked(self);

cleanup:

    dbus_error_free(&err);

    return;
}

/** Disconnect from D-Bus System Bus
 */
static void
cpukeepalive_disconnect_now_locked(cpukeepalive_t *self)
{
    log_function("%p", self);

    /* Do not leave connect timer behind */
    cpukeepalive_timer_stop_locked(self, &self->cka_delayed_connect_id);

    /* Remove signal filters */
    cpukeepalive_dbus_filter_remove_locked(self);

    /* Detach from system bus */
    if( self->cka_systembus ) {
        dbus_connection_unref(self->cka_systembus),
            self->cka_systembus = 0;
    }

    /* Note: As we do not clear cka_connect_attempted flag,
     *       re-connecting this object is not possible */
}

/** Callback for delayed D-Bus connect
 */
static gboolean
cpukeepalive_connect_cb(gpointer aptr)
{
    cpukeepalive_t *self = aptr;

    log_function("%p", self);
    cpukeepalive_lock(self);

    if( self->cka_delayed_connect_id ) {
        self->cka_delayed_connect_id = 0;
        cpukeepalive_connect_now_locked(self);
    }

    cpukeepalive_unlock(self);
    return G_SOURCE_REMOVE;
}

/** Schedule delayed D-Bus connect
 */
static void
cpukeepalive_connect_later_locked(cpukeepalive_t *self)
{
    if( !self->cka_delayed_connect_id ) {
        log_function("%p", self);
        cpukeepalive_timer_start_locked(self, &self->cka_delayed_connect_id,
                                        0, cpukeepalive_connect_cb);
    }
}

/* ========================================================================= *
 * EXTERNAL_API --  documented in: keepalive-cpukeepalive.h
 * ========================================================================= */

cpukeepalive_t *
cpukeepalive_new(void)
{
    /* Note: New instance -> no locking required */

    cpukeepalive_t *self = calloc(1, sizeof *self);

    log_function("APICALL %p", self);

    if( self )
        cpukeepalive_ctor(self);

    return self;
}

cpukeepalive_t *
cpukeepalive_ref(cpukeepalive_t *self)
{
    log_function("APICALL %p", self);

    cpukeepalive_t *ref = 0;

    if( cpukeepalive_validate_and_lock(self) ) {
        ref = cpukeepalive_ref_external_locked(self);
        cpukeepalive_unlock(self);
    }

    return ref;
}

void
cpukeepalive_unref(cpukeepalive_t *self)
{
    log_function("APICALL %p", self);

    if( cpukeepalive_validate_and_lock(self) ) {
        cpukeepalive_unref_external_locked(self);
        cpukeepalive_unlock(self);
    }
}

void
cpukeepalive_start(cpukeepalive_t *self)
{
    log_function("APICALL %p", self);

    if( cpukeepalive_validate_and_lock(self) ) {
        if( !self->cka_requested ) {
            self->cka_requested = true;
            cpukeepalive_rethink_schedule_locked(self);
        }
        cpukeepalive_unlock(self);
    }
}

void
cpukeepalive_stop(cpukeepalive_t *self)
{
    log_function("APICALL %p", self);

    if( cpukeepalive_validate_and_lock(self) ) {
        if( self->cka_requested ) {
            self->cka_requested = false;
            cpukeepalive_rethink_schedule_locked(self);
        }
        cpukeepalive_unlock(self);
    }
}

const char *
cpukeepalive_get_id(const cpukeepalive_t *self)
{
    const char *id = 0;

    if( cpukeepalive_is_valid(self) ) {
        /* Note: Id string is immutable, so no locking / duplication
         *       needed as long as caller does hold a reference as
         *       is expected.
         */
        id = cpukeepalive_get_id_locked(self);
    }

    return id;
}
