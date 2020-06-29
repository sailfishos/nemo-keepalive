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

#include "keepalive-heartbeat.h"
#include "keepalive-object.h"

#include "logging.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <iphbd/libiphb.h>

#include <glib.h>

/* ========================================================================= *
 * CONSTANTS
 * ========================================================================= */

/** Memory tag for marking live heartbeat_t objects */
#define HB_MAJICK_ALIVE 0x5492a037

/** Memory tag for marking dead heartbeat_t objects */
#define HB_MAJICK_DEAD  0x00000000

/** Delay between IPHB connect attempts */
#define HB_CONNECT_TIMEOUT_MS (5 * 1000)

/* Logging prefix for this module */
#define PFIX "heartbeat: "

/* ========================================================================= *
 * TYPES
 * ========================================================================= */

struct heartbeat_t
{
    /* Base object for locking and refcounting */
    keepalive_object_t    hb_object;

    /** Simple memory tag to catch obviously bogus heartbeat_t pointers */
    unsigned             hb_majick;

    /** Current minimum wakeup wait length */
    int                  hb_delay_lo;

    /** Current maximum wakeup wait length */
    int                  hb_delay_hi;

    /** Flag for: wakeup has been requested */
    bool                 hb_started;

    /** Flag for: wakeup has been programmed */
    bool                 hb_waiting;

    /** IPHB connection handle */
    iphb_t               hb_iphb_handle;

    /** I/O watch id for hb_iphb_handle file descriptor */
    guint                hb_wakeup_watch_id;

    /** Timer id for: retrying connection attempts */
    guint                hb_connect_retry_id;

    /** User data to be passed for hb_user_notify */
    void                *hb_user_data;

    /** Free callback to be used for releasing hb_user_data */
    heartbeat_free_fn    hb_user_free;

    /** Wakeup notification callback set via heartbeat_set_notify() */
    heartbeat_wakeup_fn  hb_user_notify;
};

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * OBJECT_LIFETIME
 * ------------------------------------------------------------------------- */

static void         heartbeat_ctor                 (heartbeat_t *self);
static void         heartbeat_shutdown_locked_cb   (gpointer aptr);
static void         heartbeat_delete_cb            (gpointer aptr);
static void         heartbeat_dtor                 (heartbeat_t *self);
static bool         heartbeat_is_valid             (const heartbeat_t *self);
static heartbeat_t *heartbeat_ref_external_locked  (heartbeat_t *self);
static void         heartbeat_unref_external_locked(heartbeat_t *self);
static void         heartbeat_lock                 (heartbeat_t *self);
static void         heartbeat_unlock               (heartbeat_t *self);
static bool         heartbeat_validate_and_lock    (heartbeat_t *self);
static bool         heartbeat_in_shutdown_locked   (heartbeat_t *self);

/* ------------------------------------------------------------------------- *
 * OBJECT_IOWATCHES
 * ------------------------------------------------------------------------- */

static void heartbeat_iowatch_start_locked(heartbeat_t *self, guint *iowatch_id, int fd, GIOCondition cnd, GIOFunc io_cb);
static void heartbeat_iowatch_stop_locked (heartbeat_t *self, guint *iowatch_id);

/* ------------------------------------------------------------------------- *
 * OBJECT_TIMERS
 * ------------------------------------------------------------------------- */

static void heartbeat_timer_start_locked(heartbeat_t *self, guint *timer_id, guint interval, GSourceFunc notify_cb);
static void heartbeat_timer_stop_locked (heartbeat_t *self, guint *timer_id);

/* ------------------------------------------------------------------------- *
 * IPHB_WAKEUP
 * ------------------------------------------------------------------------- */

static gboolean heartbeat_iphb_wakeup_cb             (GIOChannel *chn, GIOCondition cnd, gpointer data);
static void     heartbeat_iphb_wakeup_schedule_locked(heartbeat_t *self);

/* ------------------------------------------------------------------------- *
 * IPHB_CONNECTION
 * ------------------------------------------------------------------------- */

static gboolean heartbeat_iphb_connect_retry_cb          (gpointer aptr);
static bool     heartbeat_iphb_connection_try_open_locked(heartbeat_t *self);
static void     heartbeat_iphb_connection_open_locked    (heartbeat_t *self);
static void     heartbeat_iphb_connection_close_locked   (heartbeat_t *self);

/* ------------------------------------------------------------------------- *
 * STATE_MANAGEMENT
 * ------------------------------------------------------------------------- */

static void heartbeat_stop_locked      (heartbeat_t *self);
static void heartbeat_start_locked     (heartbeat_t *self);
static void heartbeat_set_delay_locked (heartbeat_t *self, int delay_lo, int delay_hi);
static void heartbeat_set_notify_locked(heartbeat_t *self, heartbeat_wakeup_fn notify_cb, void *user_data, heartbeat_free_fn user_free_cb);

/* ------------------------------------------------------------------------- *
 * EXTERNAL_API
 * ------------------------------------------------------------------------- */

heartbeat_t *heartbeat_new       (void);
heartbeat_t *heartbeat_ref       (heartbeat_t *self);
void         heartbeat_unref     (heartbeat_t *self);
void         heartbeat_set_notify(heartbeat_t *self, heartbeat_wakeup_fn notify_cb, void *user_data, heartbeat_free_fn user_free_cb);
void         heartbeat_set_delay (heartbeat_t *self, int delay_lo, int delay_hi);
void         heartbeat_start     (heartbeat_t *self);
void         heartbeat_stop      (heartbeat_t *self);

/* ========================================================================= *
 * OBJECT_LIFETIME
 * ========================================================================= */

#if LOGGING_TRACE_FUNCTIONS
static size_t heartbeat_ctor_cnt = 0;
static size_t heartbeat_dtor_cnt = 0;
static void heartbeat_report_stats_cb(void)
{
    fprintf(stderr, "heartbeat: ctor=%zd dtor=%zd diff=%zd\n",
            heartbeat_ctor_cnt,
            heartbeat_dtor_cnt,
            heartbeat_ctor_cnt - heartbeat_dtor_cnt);
}
#endif

/** Construct heartbeat object
 *
 * @param self  pointer to uninitialized heartbeat object
 */
static void
heartbeat_ctor(heartbeat_t *self)
{
#if LOGGING_TRACE_FUNCTIONS
    if( ++heartbeat_ctor_cnt == 1 )
        atexit(heartbeat_report_stats_cb);
#endif

    log_function("%p", self);

    /* Mark object as valid */
    keepalive_object_ctor(&self->hb_object, "heartbeat",
                          heartbeat_shutdown_locked_cb,
                          heartbeat_delete_cb);
    self->hb_majick   = HB_MAJICK_ALIVE;

    /* Sane default wait period */
    self->hb_delay_lo = 60 * 60;
    self->hb_delay_hi = 60 * 60;

    /* Clear state data */
    self->hb_started  = false;
    self->hb_waiting  = false;

    /* No IPHB connection */
    self->hb_iphb_handle      = 0;
    self->hb_wakeup_watch_id  = 0;
    self->hb_connect_retry_id = 0;

    /* No user data */
    self->hb_user_data   = 0;
    self->hb_user_free   = 0;

    /* No notification callback */
    self->hb_user_notify = 0;
}

/** Callback for handling keepalive_object_t shutdown
 *
 * @param self  heartbeat object pointer
 */
static void
heartbeat_shutdown_locked_cb(gpointer aptr)
{
    heartbeat_t *self = aptr;

    log_function("%p", self);

    /* Break IPHB connection */
    heartbeat_iphb_connection_close_locked(self);
}

/** Callback for handling keepalive_object_t delete
 *
 * @param self  heartbeat object pointer
 */
static void
heartbeat_delete_cb(gpointer aptr)
{
    heartbeat_t *self = aptr;
    heartbeat_dtor(self);
    free(self);
}

/** Destruct heartbeat object
 *
 * @param self  heartbeat object
 */
static void
heartbeat_dtor(heartbeat_t *self)
{
#if LOGGING_TRACE_FUNCTIONS
    ++heartbeat_dtor_cnt;
#endif

    log_function("%p", self);

    /* Mark object as invalid */
    self->hb_majick = HB_MAJICK_DEAD;
    keepalive_object_dtor(&self->hb_object);

    /* Destroy notification to user */
    heartbeat_free_fn func = self->hb_user_free;
    if( func ) {
        void *data = self->hb_user_data;
        self->hb_user_free = 0;
        self->hb_user_data = 0;
        func(data);
    }
}

/** Predicate for: heartbeat object is valid
 *
 * @param self  heartbeat object
 */
static bool
heartbeat_is_valid(const heartbeat_t *self)
{
    /* Null pointers are tolerated */
    if( !self )
        return false;

    /* but obviously invalid pointers are not */
    if( self->hb_majick != HB_MAJICK_ALIVE )
        log_abort("invalid heartbeat object %p", self);

    return true;
}

/** Add external reference
 *
 * @param self  heartbeat object pointer
 */
static heartbeat_t *
heartbeat_ref_external_locked(heartbeat_t *self)
{
    return keepalive_object_ref_external_locked(&self->hb_object);
}

/** Remove external reference
 *
 * @param self  heartbeat object pointer
 */
static void
heartbeat_unref_external_locked(heartbeat_t *self)
{
    keepalive_object_unref_external_locked(&self->hb_object);
}

/** Lock heartbeat object
 *
 * Note: This is not recursive lock, incorrect lock/unlock
 *       sequences will lead to deadlocking / aborts.
 *
 * @param self  heartbeat object pointer
 */
static void
heartbeat_lock(heartbeat_t *self)
{
    keepalive_object_lock(&self->hb_object);
}

/** Unlock heartbeat object
 *
 * @param self  heartbeat object pointer
 */
static void
heartbeat_unlock(heartbeat_t *self)
{
    keepalive_object_unlock(&self->hb_object);
}

/** Validate and then lock heartbeat object
 *
 * @param self  heartbeat object pointer
 *
 * @return true if object is valid and got locked, false otherwise
 */
static bool
heartbeat_validate_and_lock(heartbeat_t *self)
{
    if( !heartbeat_is_valid(self) )
        return false;

    heartbeat_lock(self);
    return true;
}

/** Predicate for: heartbeat object is getting shut down
 *
 * @param self    heartbeat object pointer
 *
 * @return true if object is in shutdown, false otherwise
 */
static bool
heartbeat_in_shutdown_locked(heartbeat_t *self)
{
    return keepalive_object_in_shutdown_locked(&self->hb_object);
}

/* ========================================================================= *
 * OBJECT_IOWATCHES
 * ========================================================================= */

/** Helper for creating I/O watch for file descriptor
 */
static void
heartbeat_iowatch_start_locked(heartbeat_t *self, guint *iowatch_id,
                             int fd, GIOCondition cnd, GIOFunc io_cb)
{
    log_function("%p", self);
    keepalive_object_iowatch_start_locked(&self->hb_object, iowatch_id,
                                          fd, cnd, io_cb);
}

static void
heartbeat_iowatch_stop_locked(heartbeat_t *self, guint *iowatch_id)
{
    log_function("%p", self);
    keepalive_object_iowatch_stop_locked(&self->hb_object, iowatch_id);
}

/* ========================================================================= *
 * OBJECT_TIMERS
 * ========================================================================= */

static void
heartbeat_timer_start_locked(heartbeat_t *self, guint *timer_id,
                           guint interval, GSourceFunc notify_cb)
{
    keepalive_object_timer_start_locked(&self->hb_object, timer_id,
                                      interval, notify_cb);
}

static void
heartbeat_timer_stop_locked(heartbeat_t *self, guint *timer_id)
{
    keepalive_object_timer_stop_locked(&self->hb_object, timer_id);
}

/* ========================================================================= *
 * IPHB_WAKEUP
 * ========================================================================= */

/** Calback for handling IPHB wakeups
 *
 * @param chn  io channel
 * @param cnd  io condition
 * @param data heartbeat object as void pointer
 *
 * @return TRUE to keep io watch alive, or FALSE to disable it
 */
static gboolean
heartbeat_iphb_wakeup_cb(GIOChannel *chn, GIOCondition cnd, gpointer data)
{
    gboolean keep_going = FALSE;

    heartbeat_t *self = data;

    log_function("%p", self);

    heartbeat_lock(self);

    if( !self->hb_wakeup_watch_id ) {
        /* Watch id was cleared but callback function still got executed
         * -> assume some sort of glib remove vs dispatch glitch. */
        log_warning(PFIX"stray wakeup - no watch id");
        goto failure_bailout;
    }

    int fd = g_io_channel_unix_get_fd(chn);
    if( fd < 0 )
        goto failure_reconnect;

    if( cnd & ~G_IO_IN )
        goto failure_reconnect;

    char buf[256];

    /* Stopping/reprogramming IPHB flushes pending input
     * from the socket. If that happens after decision
     * to call this input callback is already made, simple
     * read could block and that can't be allowed. */
    int rc = recv(fd, buf, sizeof buf, MSG_DONTWAIT);

    if( rc == 0 ) {
        log_error(PFIX"unexpected eof");
        goto failure_reconnect;
    }

    if( rc == -1 ) {
        if( errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK )
            goto success;

        log_error(PFIX"read error: %m");
        goto failure_reconnect;
    }

    if( !self->hb_waiting ) {
        log_debug(PFIX"stray wakeup - not waiting");
        goto success;
    }

    /* clear state data */
    self->hb_started  = false;
    self->hb_waiting  = false;

    /* notify
     *
     * To avoid deadlocking due to activity during notify
     * -> we have to unlock
     * -> notify data might change while unlocked
     * -> this is not thread safe, but ...
     *
     * Assuming heartbeat_set_notify() is used only during
     * object setup -> there is no issue.
     */
    heartbeat_wakeup_fn func = self->hb_user_notify;
    if( func ) {
        void *data = self->hb_user_data;
        heartbeat_unlock(self);
        func(data);
        heartbeat_lock(self);
    }

success:
    keep_going = TRUE;

failure_reconnect:

    if( !keep_going && self->hb_wakeup_watch_id ) {
        /* I/O error / similar -> try to re-establish
         * iphb connection */
        self->hb_wakeup_watch_id = 0;

        bool was_started = self->hb_started;
        heartbeat_iphb_connection_close_locked(self);

        self->hb_started = was_started;
        heartbeat_iphb_connection_open_locked(self);
    }

failure_bailout:
    heartbeat_unlock(self);

    return keep_going;
}

/** Request IPHB wakeup at currently active wakeup range/slot
 *
 * @param self  heartbeat object
 */
static void
heartbeat_iphb_wakeup_schedule_locked(heartbeat_t *self)
{
    log_function("%p", self);

    // not while shutting down
    if( heartbeat_in_shutdown_locked(self) )
        goto cleanup;

    // must be started
    if( !self->hb_started )
        goto cleanup;

    // but not in waiting state yet
    if( self->hb_waiting )
        goto cleanup;

    // must be connected
    heartbeat_iphb_connection_open_locked(self);
    if( !self->hb_iphb_handle )
        goto cleanup;

    int lo = self->hb_delay_lo;
    int hi = self->hb_delay_hi;
    log_notice(PFIX"iphb_wait2(%d, %d)", lo, hi);
    iphb_wait2(self->hb_iphb_handle, lo, hi, 0, 1);
    self->hb_waiting = true;

cleanup:
    return;
}

/* ========================================================================= *
 * IPHB_CONNECTION
 * ========================================================================= */

/** Callback for connect reattempt timer
 *
 * @param aptr  heartbeat object as void pointer
 *
 * @return TRUE to keep timer repeating, or FALSE to stop it
 */
static gboolean
heartbeat_iphb_connect_retry_cb(gpointer aptr)
{
    gboolean retry = false;

    heartbeat_t *self = aptr;

    log_function("%p", self);

    heartbeat_lock(self);

    // shutting down?
    if( heartbeat_in_shutdown_locked(self) )
        goto cleanup;

    // already canceled?
    if( !self->hb_wakeup_watch_id )
        goto cleanup;

    if( !heartbeat_iphb_connection_try_open_locked(self) )
        retry = true;
    else
        heartbeat_iphb_wakeup_schedule_locked(self);

cleanup:
    if( !retry )
        self->hb_wakeup_watch_id = 0;

    heartbeat_unlock(self);

    return retry ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

/** Try to establish IPHB socket connection now
 *
 * @param aptr  heartbeat object as void pointer
 */
static bool
heartbeat_iphb_connection_try_open_locked(heartbeat_t *self)
{
    iphb_t handle = 0;

    if( self->hb_iphb_handle )
        goto cleanup;

    // shutting down?
    if( heartbeat_in_shutdown_locked(self) )
        goto cleanup;

    log_function("%p", self);

    if( !(handle = iphb_open(0)) ) {
        log_warning(PFIX"iphb_open: %m");
        goto cleanup;
    }

    int fd;

    if( (fd = iphb_get_fd(handle)) == -1 ) {
        log_warning(PFIX"iphb_get_fd: %m");
        goto cleanup;
    }

    /* set up io watch */
    heartbeat_iowatch_start_locked(self, &self->hb_wakeup_watch_id,
                                 fd, G_IO_IN, heartbeat_iphb_wakeup_cb);

    if( !self->hb_wakeup_watch_id )
        goto cleanup;

    /* heartbeat_t owns the handle */
    self->hb_iphb_handle = handle, handle = 0;

cleanup:

    if( handle ) iphb_close(handle);

    return self->hb_iphb_handle != 0;
}

/** Start connecting to IPHB socket
 *
 * @param aptr  heartbeat object as void pointer
 */
static void
heartbeat_iphb_connection_open_locked(heartbeat_t *self)
{
    // Skip if shutting down
    if( heartbeat_in_shutdown_locked(self) )
        goto cleanup;

    // Skip if retry timer is already active
    if( self->hb_connect_retry_id )
        goto cleanup;

    log_function("%p", self);

    if( !heartbeat_iphb_connection_try_open_locked(self) ) {
        // Could not connect now - start retry timer
        heartbeat_timer_start_locked(self,
                                     &self->hb_connect_retry_id,
                                     HB_CONNECT_TIMEOUT_MS,
                                     heartbeat_iphb_connect_retry_cb);
    }

cleanup:
    return;
}

/** Close IPHB socket connection
 *
 * @param aptr  heartbeat object as void pointer
 */
static void
heartbeat_iphb_connection_close_locked(heartbeat_t *self)
{
    /* Stop retry timer */
    heartbeat_timer_stop_locked(self,&self->hb_connect_retry_id);

    /* Remove io watch */
    heartbeat_iowatch_stop_locked(self, &self->hb_wakeup_watch_id);

    /* Stop IPHB timer */
    heartbeat_stop_locked(self);

    /* Close handle */
    if( self->hb_iphb_handle ) {
        log_function("%p", self);
        iphb_close(self->hb_iphb_handle),
            self->hb_iphb_handle = 0;
    }
}

/* ========================================================================= *
 * STATE_MANAGEMENT
 * ========================================================================= */

static void
heartbeat_stop_locked(heartbeat_t *self)
{
    log_function("%p", self);

    if( self->hb_waiting && self->hb_iphb_handle )
        iphb_wait2(self->hb_iphb_handle, 0, 0, 0, 0);

    self->hb_waiting = false;
    self->hb_started = false;
}

static void
heartbeat_start_locked(heartbeat_t *self)
{
    log_function("%p", self);

    self->hb_started = true;
    heartbeat_iphb_wakeup_schedule_locked(self);
}

static void
heartbeat_set_delay_locked(heartbeat_t *self, int delay_lo, int delay_hi)
{
    log_function("%p", self);

    if( delay_lo < 1 )
        delay_lo = 1;

    if( delay_hi < delay_lo )
        delay_hi = delay_lo;

    self->hb_delay_lo = delay_lo;
    self->hb_delay_hi = delay_hi;
}

static void
heartbeat_set_notify_locked(heartbeat_t *self,
                            heartbeat_wakeup_fn notify_cb,
                            void *user_data,
                            heartbeat_free_fn user_free_cb)
{
    log_function("%p", self);

    self->hb_user_data   = user_data;
    self->hb_user_free   = user_free_cb;
    self->hb_user_notify = notify_cb;
}

/* ========================================================================= *
 * EXTERNAL_API --  documented in: keepalive-hearbeat.h
 * ========================================================================= */

heartbeat_t *
heartbeat_new(void)
{
    heartbeat_t *self = calloc(1, sizeof *self);
    log_function("APICALL %p", self);
    if( self )
        heartbeat_ctor(self);
    return self;
}

heartbeat_t *
heartbeat_ref(heartbeat_t *self)
{
    log_function("APICALL %p", self);
    heartbeat_t *ref = 0;
    if( heartbeat_validate_and_lock(self) ) {
        ref = heartbeat_ref_external_locked(self);
        heartbeat_unlock(self);
    }
    return ref;
}

void
heartbeat_unref(heartbeat_t *self)
{
    log_function("APICALL %p", self);
    if( heartbeat_validate_and_lock(self) ) {
        heartbeat_unref_external_locked(self);
        heartbeat_unlock(self);
    }
}

void
heartbeat_set_notify(heartbeat_t *self,
                     heartbeat_wakeup_fn notify_cb,
                     void *user_data,
                     heartbeat_free_fn user_free_cb)
{
    log_function("APICALL %p", self);
    if( heartbeat_validate_and_lock(self) ) {
        heartbeat_set_notify_locked(self, notify_cb, user_data, user_free_cb);
        heartbeat_unlock(self);
    }
}

void
heartbeat_set_delay(heartbeat_t *self, int delay_lo, int delay_hi)
{
    log_function("APICALL %p", self);
    if( heartbeat_validate_and_lock(self) ) {
        heartbeat_set_delay_locked(self, delay_lo, delay_hi);
        heartbeat_unlock(self);
    }
}

void
heartbeat_start(heartbeat_t *self)
{
    log_function("APICALL %p", self);
    if( heartbeat_validate_and_lock(self) ) {
        heartbeat_start_locked(self);
        heartbeat_unlock(self);
    }
}

void
heartbeat_stop(heartbeat_t *self)
{
    log_function("APICALL %p", self);
    if( heartbeat_validate_and_lock(self) ) {
        heartbeat_stop_locked(self);
        heartbeat_unlock(self);
    }
}
