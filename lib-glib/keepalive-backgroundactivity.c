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

#include "keepalive-backgroundactivity.h"
#include "keepalive-heartbeat.h"
#include "keepalive-cpukeepalive.h"
#include "keepalive-object.h"

#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

/* ========================================================================= *
 * CONSTANTS
 * ========================================================================= */

/* Logging prefix for this module */
#define PFIX "background activity"

/** Memory tag for marking live background_activity_t objects */
#define BACKGROUND_ACTIVITY_MAJICK_ALIVE 0x54913336

/** Memory tag for marking dead background_activity_t objects */
#define BACKGROUND_ACTIVITY_MAJICK_DEAD  0x00000000

/* ========================================================================= *
 * TYPES
 * ========================================================================= */

/** Enumeration of states background activity object can be in */
typedef enum
{
    /** Neither waiting for heartbeat wakeup nor blocking suspend */
    BACKGROUND_ACTIVITY_STATE_STOPPED = 0,

    /** Waiting for heartbeat wakeup */
    BACKGROUND_ACTIVITY_STATE_WAITING = 1,

    /** Blocking suspend */
    BACKGROUND_ACTIVITY_STATE_RUNNING = 2,

} background_activity_state_t;

/** Wakeup delay using either Global slot or range */
typedef struct
{
    /** Global wakeup slot, or BACKGROUND_ACTIVITY_FREQUENCY_RANGE
     *  in case ranged wakeup is to be used */
    background_activity_frequency_t wd_slot;

    /** Minimum ranged wait period length */
    int                             wd_range_lo;

    /** Maximum ranged wait period length */
    int                             wd_range_hi;
} wakeup_delay_t;

/** State data for background activity object
 */
struct background_activity_t
{
    /* Base object for locking and refcounting */
    keepalive_object_t              bga_object;

    /** Simple memory tag to catch usage of obviously bogus
     *  background_activity_t pointers */
    unsigned                        bga_majick;

    /** Current state: Stopped, Waiting or Running */
    background_activity_state_t     bga_current_state;

    /** Reported state: Stopped, Waiting or Running */
    guint                           bga_report_state_id;
    background_activity_state_t     bga_reported_state;

    /** Requested wakeup slot/range */
    wakeup_delay_t                  bga_wakeup_curr;

    /** Last wakeup slot/range actually used for IPHB IPC
     *
     * Used for detecting Waiting -> Waiting transitions
     * that need to reprogram the wait time */
    wakeup_delay_t                  bga_wakeup_last;

    /** User data pointer passed to notification callbacks */
    void                           *bga_user_data;

    /** Callback for freeing bga_user_data */
    background_activity_free_fn     bga_user_free;

    /** Notify transition to Running state */
    background_activity_event_fn    bga_running_cb;

    /** Notify transition to Waiting state */
    background_activity_event_fn    bga_waiting_cb;

    /** Notify transition to Stopped state */
    background_activity_event_fn    bga_stopped_cb;

    /** For IPHB wakeup IPC with DSME */
    heartbeat_t                    *bga_heartbeat;

    /** For CPU-keepalive IPC with MCE */
    cpukeepalive_t                 *bga_keepalive;

    // Update also: background_activity_ctor() & background_activity_dtor()
};

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * BACKGROUND_ACTIVITY_STATE
 * ------------------------------------------------------------------------- */

static const char *background_activity_state_repr(background_activity_state_t state);

/* ------------------------------------------------------------------------- *
 * WAKEUP_DELAY
 * ------------------------------------------------------------------------- */

static void wakeup_delay_set_slot (wakeup_delay_t *self, background_activity_frequency_t slot);
static void wakeup_delay_set_range(wakeup_delay_t *self, int range_lo, int range_hi);
static bool wakeup_delay_eq_p     (const wakeup_delay_t *self, const wakeup_delay_t *that);

/* ------------------------------------------------------------------------- *
 * OBJECT_LIFETIME
 * ------------------------------------------------------------------------- */

static bool                   background_activity_is_valid             (const background_activity_t *self);
static void                   background_activity_ctor                 (background_activity_t *self);
static void                   background_activity_shutdown_locked_cb   (gpointer aptr);
static void                   background_activity_delete_cb            (gpointer aptr);
static void                   background_activity_dtor                 (background_activity_t *self);
static background_activity_t *background_activity_ref_external_locked  (background_activity_t *self);
static void                   background_activity_unref_external_locked(background_activity_t *self);
static void                   background_activity_lock                 (background_activity_t *self);
static void                   background_activity_unlock               (background_activity_t *self);
static bool                   background_activity_validate_and_lock    (background_activity_t *self);
static void                   background_activity_set_lock             (background_activity_t *self, bool *locked, bool lock);
static bool                   background_activity_in_shutdown_locked   (background_activity_t *self);

/* ------------------------------------------------------------------------- *
 * OBJECT_TIMERS
 * ------------------------------------------------------------------------- */

static void background_activity_timer_start_locked(background_activity_t *self, guint *timer_id, guint interval, GSourceFunc notify_cb);
static void background_activity_timer_stop_locked (background_activity_t *self, guint *timer_id);

/* ------------------------------------------------------------------------- *
 * STATE_TRANSITIONS
 * ------------------------------------------------------------------------- */

static void                        background_activity_stopped_cb      (background_activity_t *self, void *data);
static void                        background_activity_waiting_cb      (background_activity_t *self, void *data);
static void                        background_activity_running_cb      (background_activity_t *self, void *data);
static gboolean                    background_activity_report_state_cb (void *aptr);
static void                        background_activity_set_state_locked(background_activity_t *self, background_activity_state_t state);
static background_activity_state_t background_activity_get_state_locked(const background_activity_t *self);
static bool                        background_activity_in_state_locked (const background_activity_t *self, background_activity_state_t state);
static bool                        background_activity_in_state        (background_activity_t *self, background_activity_state_t state);
static void                        background_activity_set_state       (background_activity_t *self, background_activity_state_t state);

/* ------------------------------------------------------------------------- *
 * HEARTBEAT_WAKEUP
 * ------------------------------------------------------------------------- */

static void background_activity_heartbeat_wakeup_cb(void *aptr);

/* ------------------------------------------------------------------------- *
 * EXTERNAL_API
 * ------------------------------------------------------------------------- */

background_activity_t           *background_activity_new                 (void);
background_activity_t           *background_activity_ref                 (background_activity_t *self);
void                             background_activity_unref               (background_activity_t *self);
background_activity_frequency_t  background_activity_get_wakeup_slot     (background_activity_t *self);
void                             background_activity_set_wakeup_slot     (background_activity_t *self, background_activity_frequency_t slot);
void                             background_activity_get_wakeup_range    (background_activity_t *self, int *range_lo, int *range_hi);
void                             background_activity_set_wakeup_range    (background_activity_t *self, int range_lo, int range_hi);
bool                             background_activity_is_waiting          (background_activity_t *self);
bool                             background_activity_is_running          (background_activity_t *self);
bool                             background_activity_is_stopped          (background_activity_t *self);
void                             background_activity_wait                (background_activity_t *self);
void                             background_activity_run                 (background_activity_t *self);
void                             background_activity_stop                (background_activity_t *self);
const char                      *background_activity_get_id              (const background_activity_t *self);
void                            *background_activity_get_user_data       (background_activity_t *self);
void                            *background_activity_steal_user_data     (background_activity_t *self);
void                             background_activity_set_user_data       (background_activity_t *self, void *user_data, background_activity_free_fn free_cb);
void                             background_activity_set_running_callback(background_activity_t *self, background_activity_event_fn cb);
void                             background_activity_set_waiting_callback(background_activity_t *self, background_activity_event_fn cb);
void                             background_activity_set_stopped_callback(background_activity_t *self, background_activity_event_fn cb);

/* ========================================================================= *
 * BACKGROUND_ACTIVITY_STATE
 * ========================================================================= */

static const char *
background_activity_state_repr(background_activity_state_t state)
{
    const char *res = "UNKNOWN";
    switch( state ) {
    case BACKGROUND_ACTIVITY_STATE_STOPPED: res = "STOPPED"; break;
    case BACKGROUND_ACTIVITY_STATE_WAITING: res = "WAITING"; break;
    case BACKGROUND_ACTIVITY_STATE_RUNNING: res = "RUNNING"; break;
    default: break;
    }
    return res;
}

/* ========================================================================= *
 * WAKEUP_DELAY
 * ========================================================================= */

/** Default initial wakeup delay */
static const wakeup_delay_t wakeup_delay_default =
{
    .wd_slot     = BACKGROUND_ACTIVITY_FREQUENCY_ONE_HOUR,
    .wd_range_lo = BACKGROUND_ACTIVITY_FREQUENCY_ONE_HOUR,
    .wd_range_hi = BACKGROUND_ACTIVITY_FREQUENCY_ONE_HOUR,
};

/** Set wakeup delay to use global wakeup slot
 *
 * @param self  wake up delay object
 * @param slot  global wakeup slot to use
 */
static void
wakeup_delay_set_slot(wakeup_delay_t *self,
                      background_activity_frequency_t slot)
{
    // basically it is just a second count, but it must be

    // a) not smaller than the smallest allowed global slot

    if( slot < BACKGROUND_ACTIVITY_FREQUENCY_THIRTY_SECONDS )
        slot = BACKGROUND_ACTIVITY_FREQUENCY_THIRTY_SECONDS;

    // b) evenly divisible by the smallest allowed global slot

    slot = slot - (slot % BACKGROUND_ACTIVITY_FREQUENCY_THIRTY_SECONDS);

    self->wd_slot     = slot;
    self->wd_range_lo = slot;
    self->wd_range_hi = slot;
}

/** Set wakeup delay to use wakeup range
 *
 * @param self      wake up delay object
 * @param range_lo  minimum seconds to wait
 * @param range_hi  maximum seconds to wait
 */
static void
wakeup_delay_set_range(wakeup_delay_t *self,
                       int range_lo, int range_hi)
{
    /* Currently there is no way to tell what kind of hw watchdog
     * kicking period DSME is using - assume that it is 12 seconds */
    const int heartbeat_period = 12;

    /* Zero wait is not supported */
    if( range_lo < 1 )
        range_lo = 1;

    /* Expand invalid range to heartbeat length */
    if( range_hi <= range_lo )
        range_hi = range_lo + heartbeat_period;

    self->wd_slot     = BACKGROUND_ACTIVITY_FREQUENCY_RANGE;
    self->wd_range_lo = range_lo;
    self->wd_range_hi = range_hi;
}

/** Predicate for: two wake up delay objects are the same
 *
 * @param self      wake up delay object
 */
static bool
wakeup_delay_eq_p(const wakeup_delay_t *self, const wakeup_delay_t *that)
{
    return (self->wd_slot     == that->wd_slot     &&
            self->wd_range_lo == that->wd_range_lo &&
            self->wd_range_hi == that->wd_range_hi);
}

/* ========================================================================= *
 * OBJECT_LIFETIME
 * ========================================================================= */

#if LOGGING_TRACE_FUNCTIONS
static size_t background_activity_ctor_cnt = 0;
static size_t background_activity_dtor_cnt = 0;
static void background_activity_report_stats_cb(void)
{
  fprintf(stderr, "background_activity: ctor=%zd dtor=%zd diff=%zd\n",
          background_activity_ctor_cnt,
          background_activity_dtor_cnt,
          background_activity_ctor_cnt - background_activity_dtor_cnt);
}
#endif

/** Check if background activity pointer is valid
 *
 * @param self  background activity object pointer
 *
 * @return true if pointer is likely to be valid, false otherwise
 */
static bool
background_activity_is_valid(const background_activity_t *self)
{
    /* Null pointers are tolerated */
    if( !self )
        return false;

    /* but obviously invalid pointers are not */
    if( self->bga_majick != BACKGROUND_ACTIVITY_MAJICK_ALIVE )
        log_abort("invalid background activity object: %p", self);

    return true;
}

/** Construct background activity object
 *
 * @param self  pointer to uninitialized background activity object
 */
static void
background_activity_ctor(background_activity_t *self)
{
#if LOGGING_TRACE_FUNCTIONS
    if( ++background_activity_ctor_cnt == 1 )
        atexit(background_activity_report_stats_cb);
#endif

    log_function("%p", self);

    /* Flag object as valid */
    keepalive_object_ctor(&self->bga_object, "bg-activity",
                          background_activity_shutdown_locked_cb,
                          background_activity_delete_cb);
    self->bga_majick = BACKGROUND_ACTIVITY_MAJICK_ALIVE;

    /* In stopped state */
    self->bga_current_state    = BACKGROUND_ACTIVITY_STATE_STOPPED;
    self->bga_report_state_id  = 0;
    self->bga_reported_state   = BACKGROUND_ACTIVITY_STATE_STOPPED;

    /* Sane wakeup delay defaults */
    self->bga_wakeup_curr = wakeup_delay_default;
    self->bga_wakeup_last = wakeup_delay_default;

    /* No user data */
    self->bga_user_data = 0;
    self->bga_user_free = 0;

    /* No notification callbacks */
    self->bga_running_cb = 0;
    self->bga_waiting_cb = 0;
    self->bga_stopped_cb = 0;

    /* Heartbeat object for waking up */
    self->bga_heartbeat  = heartbeat_new();
    heartbeat_set_notify(self->bga_heartbeat,
                         background_activity_heartbeat_wakeup_cb,
                         self, 0);

    /* Keepalive object for staying up */
    self->bga_keepalive  = cpukeepalive_new();

    log_debug(PFIX"(%s): created", background_activity_get_id(self));
}

/** Callback for handling keepalive_object_t shutdown
 *
 * @param self  background activity object pointer
 */
static void
background_activity_shutdown_locked_cb(gpointer aptr)
{
    background_activity_t *self = aptr;

    log_function("%p", self);

    /* Detach heartbeat object */
    heartbeat_set_notify(self->bga_heartbeat, 0, 0, 0);
    heartbeat_unref(self->bga_heartbeat),
        self->bga_heartbeat = 0;

    /* Detach keepalive object */
    cpukeepalive_unref(self->bga_keepalive),
        self->bga_keepalive = 0;

    /* Cancel state notify */
    background_activity_timer_stop_locked(self, &self->bga_report_state_id);
}

/** Callback for handling keepalive_object_t delete
 *
 * @param self  background activity object pointer
 */
static void
background_activity_delete_cb(gpointer aptr)
{
    background_activity_t *self = aptr;
    background_activity_dtor(self);
    free(self);
}

/** Destruct background activity object
 *
 * @param self  background activity object pointer
 */
static void
background_activity_dtor(background_activity_t *self)
{
#if LOGGING_TRACE_FUNCTIONS
    ++background_activity_dtor_cnt;
#endif

    log_function("%p", self);

    /* Flag object as invalid */
    self->bga_majick = BACKGROUND_ACTIVITY_MAJICK_DEAD;
    keepalive_object_dtor(&self->bga_object);

    /* Destroy notification */
    if( self->bga_user_free )
        self->bga_user_free(self->bga_user_data);
    self->bga_user_data = 0;
    self->bga_user_free = 0;

}

/** Add external reference
 *
 * @param self  background activity object pointer
 */
static background_activity_t *
background_activity_ref_external_locked(background_activity_t *self)
{
    return keepalive_object_ref_external_locked(&self->bga_object);
}

/** Remove external reference
 *
 * @param self  background activity object pointer
 */
static void
background_activity_unref_external_locked(background_activity_t *self)
{
    keepalive_object_unref_external_locked(&self->bga_object);
}

/** Lock background activity object
 *
 * Note: This is not recursive lock, incorrect lock/unlock
 *       sequences will lead to deadlocking / aborts.
 *
 * @param self  background activity object pointer
 */
static void
background_activity_lock(background_activity_t *self)
{
    keepalive_object_lock(&self->bga_object);
}

/** Unlock background activity object
 *
 * @param self  background activity object pointer
 */
static void
background_activity_unlock(background_activity_t *self)
{
    keepalive_object_unlock(&self->bga_object);
}

/** Validate and then lock background activity object
 *
 * @param self  background activity object pointer
 *
 * @return true if object is valid and got locked, false otherwise
 */
static bool
background_activity_validate_and_lock(background_activity_t *self)
{
    if( !background_activity_is_valid(self) )
        return false;

    background_activity_lock(self);
    return true;
}

/** Conditionally lock/unlock background activity object
 *
 * This is a helper for simplifying logic in functions that
 * need to alternate between locked and unlocked operations
 * with non-trivial inter-dependencies.
 *
 * @param self    background activity object pointer
 * @param locked  current locked status
 * @param lock    target locked status
 */
static void
background_activity_set_lock(background_activity_t *self,
                               bool *locked, bool lock)
{
    if( *locked != lock ) {
        if( (*locked = lock) )
            background_activity_lock(self);
        else
            background_activity_unlock(self);
    }
}

/** Predicate for: background activity object is getting shut down
 *
 * @param self    background activity object pointer
 *
 * @return true if object is in shutdown, false otherwise
 */
static bool
background_activity_in_shutdown_locked(background_activity_t *self)
{
    return keepalive_object_in_shutdown_locked(&self->bga_object);
}

/* ========================================================================= *
 * OBJECT_TIMERS
 * ========================================================================= */

/** Schedule timer that holds internal ref until completion
 */
static void
background_activity_timer_start_locked(background_activity_t *self,
                                     guint *timer_id, guint interval,
                                     GSourceFunc notify_cb)
{
    keepalive_object_timer_start_locked(&self->bga_object, timer_id,
                                      interval, notify_cb);
}

static void
background_activity_timer_stop_locked(background_activity_t *self,
                                      guint *timer_id)
{
    keepalive_object_timer_stop_locked(&self->bga_object, timer_id);
}

/* ========================================================================= *
 * STATE_TRANSITIONS
 * ========================================================================= */

static void
background_activity_stopped_cb(background_activity_t *self, void *data)
{
    (void)data;
    log_function("%p", self);
}

static void
background_activity_waiting_cb(background_activity_t *self, void *data)
{
    (void)data;
    log_function("%p", self);
}

static void
background_activity_running_cb(background_activity_t *self, void *data)
{
    (void)data;
    log_function("%p", self);
    background_activity_stop(self);
}

static gboolean
background_activity_report_state_cb(void *aptr)
{
    background_activity_t *self  = aptr;

    log_function("%p", self);

    bool locked = false;
    background_activity_set_lock(self, &locked, true);

    /* Assume: neither notify nor stop */
    background_activity_event_fn  func = 0;
    void                         *data = self->bga_user_data;
    bool                          stop = false;

    /* Skip if timer ought to be inactive */
    if( !self->bga_report_state_id )
        goto cleanup;

    self->bga_report_state_id = 0;

    /* Skip if already shutting down */
    if( background_activity_in_shutdown_locked(self) )
        goto cleanup;

    /* Skip if no state changes */
    if( self->bga_reported_state == self->bga_current_state )
        goto cleanup;

    self->bga_reported_state = self->bga_current_state;

    /* Check need to notify */
    switch( (self->bga_reported_state) ) {
    case BACKGROUND_ACTIVITY_STATE_STOPPED:
        func = self->bga_stopped_cb ?: background_activity_stopped_cb;
        break;

    case BACKGROUND_ACTIVITY_STATE_WAITING:
        func = self->bga_waiting_cb ?: background_activity_waiting_cb;
        break;

    case BACKGROUND_ACTIVITY_STATE_RUNNING:
        /* Whatever happens at the callback function, it
         * MUST end up with a call background_activity_stop()
         * or background_activity_wait() or the suspend can
         * be blocked until the process makes an exit */
        func = self->bga_running_cb ?: background_activity_running_cb;
        break;
    }

    /* Check need to stop */
    stop = self->bga_reported_state != BACKGROUND_ACTIVITY_STATE_RUNNING;

cleanup:

    /* To avoid deadlocks, notify in unlocked state */
    if( func ) {
        background_activity_set_lock(self, &locked, false);
        func(self, data);
    }

    /* Stopping keepalive timer must happen after notification */
    if( stop ) {
        background_activity_set_lock(self, &locked, true);
        cpukeepalive_stop(self->bga_keepalive);
    }

    background_activity_set_lock(self, &locked, false);
    return G_SOURCE_REMOVE;
}

/* Set state of background activity object
 *
 * @param self   background activity object pointer
 * @param state  BACKGROUND_ACTIVITY_STATE_STOPPED|WAITING|RUNNING
 */
static void
background_activity_set_state_locked(background_activity_t *self,
                                     background_activity_state_t state)
{
    /* No state changes while shutting down */
    if( background_activity_in_shutdown_locked(self) )
        goto cleanup;

    /* Skip if state does not change; note that changing the length
     * of wait while already waiting is considered a state change */
    if( self->bga_current_state == state ) {
        if( state != BACKGROUND_ACTIVITY_STATE_WAITING )
            goto cleanup;

        if( wakeup_delay_eq_p(&self->bga_wakeup_curr,
                              &self->bga_wakeup_last) )
            goto cleanup;
    }

    log_notice(PFIX"(%s): state: %s -> %s",
               background_activity_get_id(self),
               background_activity_state_repr(self->bga_current_state),
               background_activity_state_repr(state));

    /* leave old state */
    switch( self->bga_current_state ) {
    case BACKGROUND_ACTIVITY_STATE_STOPPED:
        break;

    case BACKGROUND_ACTIVITY_STATE_WAITING:
        /* heartbeat timer can be cancelled before state transition */
        heartbeat_stop(self->bga_heartbeat);
        break;

    case BACKGROUND_ACTIVITY_STATE_RUNNING:
        /* keepalive timer is cancelled after state transition
         * is completed in background_activity_report_state_cb().
         */
        break;
    }

    /* enter new state */
    switch( state ) {
    case BACKGROUND_ACTIVITY_STATE_STOPPED:
        break;

    case BACKGROUND_ACTIVITY_STATE_WAITING:
        heartbeat_set_delay(self->bga_heartbeat,
                            self->bga_wakeup_curr.wd_range_lo,
                            self->bga_wakeup_curr.wd_range_hi);

        self->bga_wakeup_last = self->bga_wakeup_curr;

        heartbeat_start(self->bga_heartbeat);
        break;

    case BACKGROUND_ACTIVITY_STATE_RUNNING:
        cpukeepalive_start(self->bga_keepalive);
        break;
    }

    /* skip notifications if state does not actually change */
    if( self->bga_current_state == state )
        goto cleanup;

    self->bga_current_state = state;

    if( self->bga_report_state_id )
        goto cleanup;

    background_activity_timer_start_locked(self, &self->bga_report_state_id, 0,
                                           background_activity_report_state_cb);

cleanup:
    return;
}

/** Get state of background activity object
 *
 * @param self  background activity object pointer
 *
 * @return BACKGROUND_ACTIVITY_STATE_STOPPED|WAITING|RUNNING
 */
static background_activity_state_t
background_activity_get_state_locked(const background_activity_t *self)
{
    return self->bga_current_state;
}

/** Predicate function for checking state of background activity object
 *
 * @param self   background activity object pointer
 * @param state  state to check
 *
 * @return true if object is valid and in the given state, false otherwise
 */
static bool
background_activity_in_state_locked(const background_activity_t *self,
                                    background_activity_state_t state)
{
    return background_activity_get_state_locked(self) == state;
}

/* ========================================================================= *
 * HEARTBEAT_WAKEUP
 * ========================================================================= */

/** Handle heartbeat wakeup
 *
 * @param aptr background activity object as void pointer
 */
static void
background_activity_heartbeat_wakeup_cb(void *aptr)
{
    background_activity_t *self = aptr;

    if( background_activity_validate_and_lock(self) ) {
        log_notice(PFIX"(%s): iphb wakeup", background_activity_get_id(self));
        if( background_activity_in_state_locked(self, BACKGROUND_ACTIVITY_STATE_WAITING) )
            background_activity_set_state_locked(self, BACKGROUND_ACTIVITY_STATE_RUNNING);
        background_activity_unlock(self);
    }
}

/* ========================================================================= *
 * EXTERNAL_API  --  documented in: keepalive-backgroundactivity.h
 * ========================================================================= */

static bool
background_activity_in_state(background_activity_t *self,
                             background_activity_state_t state)
{
    bool in_state = false;

    if( background_activity_validate_and_lock(self) ) {
        in_state = background_activity_in_state_locked(self, state);
        background_activity_unlock(self);
    }
    return in_state;
}

static void
background_activity_set_state(background_activity_t *self,
                              background_activity_state_t state)
{
    log_function("%p", self);
    if( background_activity_validate_and_lock(self) ) {
        background_activity_set_state_locked(self, state);
        background_activity_unlock(self);
    }
}

background_activity_t *
background_activity_new(void)
{
    background_activity_t *self = calloc(1, sizeof *self);
    log_function("APICALL %p", self);
    if( self )
        background_activity_ctor(self);
    return self;
}

background_activity_t *
background_activity_ref(background_activity_t *self)
{
    log_function("APICALL %p", self);
    background_activity_t *ref = 0;
    if( background_activity_validate_and_lock(self) ) {
        ref = background_activity_ref_external_locked(self);
        background_activity_unlock(self);
    }

    return ref;
}

void
background_activity_unref(background_activity_t *self)
{
    log_function("APICALL %p", self);
    if( background_activity_validate_and_lock(self) ) {
        background_activity_unref_external_locked(self);
        background_activity_unlock(self);
    }
}

background_activity_frequency_t
background_activity_get_wakeup_slot(background_activity_t *self)
{
    log_function("APICALL %p", self);
    background_activity_frequency_t slot = BACKGROUND_ACTIVITY_FREQUENCY_RANGE;
    if( background_activity_validate_and_lock(self) ) {
        slot = self->bga_wakeup_curr.wd_slot;
        background_activity_unlock(self);
    }
    return slot;
}

void
background_activity_set_wakeup_slot(background_activity_t *self,
                                    background_activity_frequency_t slot)
{
    log_function("APICALL %p", self);
    if( background_activity_validate_and_lock(self) ) {
        wakeup_delay_set_slot(&self->bga_wakeup_curr, slot);
        background_activity_unlock(self);
    }
}

void
background_activity_get_wakeup_range(background_activity_t *self,
                                     int *range_lo, int *range_hi)
{
    log_function("APICALL %p", self);
    if( background_activity_validate_and_lock(self) ) {
        *range_lo = self->bga_wakeup_curr.wd_range_lo;
        *range_hi = self->bga_wakeup_curr.wd_range_hi;
        background_activity_unlock(self);
    }
    else {
        *range_lo = BACKGROUND_ACTIVITY_FREQUENCY_RANGE;
        *range_hi = BACKGROUND_ACTIVITY_FREQUENCY_RANGE;
    }
}

void
background_activity_set_wakeup_range(background_activity_t *self,
                                     int range_lo, int range_hi)
{
    log_function("APICALL %p", self);
    if( background_activity_validate_and_lock(self) ) {
        wakeup_delay_set_range(&self->bga_wakeup_curr, range_lo, range_hi);
        background_activity_unlock(self);
    }
}

bool
background_activity_is_waiting(background_activity_t *self)
{
    /* The self pointer is validated by background_activity_in_state() */
    return background_activity_in_state(self,
                                        BACKGROUND_ACTIVITY_STATE_WAITING);
}

bool
background_activity_is_running(background_activity_t *self)
{
    /* The self pointer is validated by background_activity_in_state() */
    return background_activity_in_state(self,
                                        BACKGROUND_ACTIVITY_STATE_RUNNING);
}

bool
background_activity_is_stopped(background_activity_t *self)
{
    /* The self pointer is validated by background_activity_in_state() */
    return background_activity_in_state(self,
                                        BACKGROUND_ACTIVITY_STATE_STOPPED);
}

void
background_activity_wait(background_activity_t *self)
{
    log_function("APICALL %p", self);
    /* The self pointer is validated by background_activity_set_state() */
    background_activity_set_state(self, BACKGROUND_ACTIVITY_STATE_WAITING);
}

void
background_activity_run(background_activity_t *self)
{
    log_function("APICALL %p", self);
    /* The self pointer is validated by background_activity_set_state() */
    background_activity_set_state(self, BACKGROUND_ACTIVITY_STATE_RUNNING);
}

void
background_activity_stop(background_activity_t *self)
{
    log_function("APICALL %p", self);
    /* The self pointer is validated by background_activity_set_state() */
    background_activity_set_state(self, BACKGROUND_ACTIVITY_STATE_STOPPED);
}

const char *
background_activity_get_id(const background_activity_t *self)
{
    const char *id = 0;

    /* The id string is immutable as long as caller is holding
     * a reference, no need to lock. */
    if( background_activity_is_valid(self) )
        id = cpukeepalive_get_id(self->bga_keepalive);

    return id;
}

void *
background_activity_get_user_data(background_activity_t *self)
{
    void *user_data = 0;

    if( background_activity_validate_and_lock(self) ) {
        user_data = self->bga_user_data;
        background_activity_unlock(self);
    }

    return user_data;
}

void *
background_activity_steal_user_data(background_activity_t *self)
{
    void *user_data = 0;
    if( background_activity_validate_and_lock(self) ) {
        user_data = self->bga_user_data;
        self->bga_user_data = 0;
        self->bga_user_free = 0;
        background_activity_unlock(self);
    }
    return user_data;
}

void
background_activity_set_user_data(background_activity_t *self,
                                  void *user_data,
                                  background_activity_free_fn free_cb)
{
    if( background_activity_validate_and_lock(self) ) {

        /* Get old hook data */
        background_activity_free_fn func = self->bga_user_free;
        void *data = self->bga_user_data;

        /* Set new hook data */
        self->bga_user_data = user_data;
        self->bga_user_free = free_cb;

        background_activity_unlock(self);

        /* Execute old hook */
        if( func )
            func(data);
    }
}

void
background_activity_set_running_callback(background_activity_t *self,
                                         background_activity_event_fn cb)
{
    if( background_activity_validate_and_lock(self) ) {
        self->bga_running_cb = cb;
        background_activity_unlock(self);
    }
}

void
background_activity_set_waiting_callback(background_activity_t *self,
                                         background_activity_event_fn cb)
{
    if( background_activity_validate_and_lock(self) ) {
        self->bga_waiting_cb = cb;
        background_activity_unlock(self);
    }
}

void
background_activity_set_stopped_callback(background_activity_t *self,
                                         background_activity_event_fn cb)
{
    if( background_activity_validate_and_lock(self) ) {
        self->bga_stopped_cb = cb;
        background_activity_unlock(self);
    }
}
