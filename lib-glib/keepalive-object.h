/****************************************************************************************
**
** Copyright (c) 2020 Jolla Ltd.
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

/** @file keepalive-object.h
 *
 * @brief Provides common base for locking and reference counting.
 */

#ifndef KEEPALIVE_OBJECT_H_
#define KEEPALIVE_OBJECT_H_

# include <stdarg.h>
# include <stdbool.h>

# include <glib.h>

# include <dbus/dbus.h>

# ifdef __cplusplus
extern "C" {
# endif

/* ========================================================================= *
 * Types
 * ========================================================================= */

/** Base object used for locking and reference counting
 *
 * NOTE: Pointers to keepalive_object_t are assumed to be also pointers
 *       to containing object i.e. keepalive_object_t must be the 1st
 *       member in containing object structure.
 *
 * Basic assumptions:
 *
 * - functions that end with "_locked" assume that object has already
 *   been validated and locked
 * - functions that do not end with "_locked" will lock the object
 *   before touching internals and unlock again before returning
 * - callbacks need to be called in unlocked state to avoid deadlocks
 *
 * Notable exceptions:
 *
 * - initial stages of object initialization and final states of
 *   object destruction happen in context where locking is not
 *   applicable
 *
 * Rough object life cycle:
 *
 * 1. keepalive_object_ctor()
 * - object is counted as having one external reference
 * - all features available for use
 *
 * 2. keepalive_object_ref_internal_locked()
 * - object will not be deleted until all internal refs are dropped
 * - timers / dbus calls etc must hold an internal ref until
 *   finalized
 *
 * 3. last keepalive_object_unref_external_locked()
 * - keepalive_object_shutdown_locked() shedules shutdown
 * - attempting to add external refs aborts
 *
 * 4. keepalive_object_shutdown_cb()
 * - shutdown notification is made
 * - existing timers are either canceled / waited to complete
 * - new timers can't be added
 *
 * 5. last keepalive_object_unref_internal_locked()
 * - attempt to add/remove refs will abort
 *
 * 6. keepalive_object_unlock()
 * - no more references triggers delete
 *
 * 7. keepalive_object_destroy()
 * - destroy notification is made
 *
 * 8. keepalive_object_dtor()
 * -> further lock/unlock attempts will abort
 *
 * 9. dynamic memory is released
 */
typedef struct keepalive_object_t keepalive_object_t;

struct keepalive_object_t
{
    /** Type name string used for logging */
    const char                     *kao_identity;

    /** External reference count; initially 1 */
    unsigned                        kao_refcount_external;

    /** Internal reference count; initially 0 */
    unsigned                        kao_refcount_internal;

    /** Flag for: shutting down activity */
    bool                            kao_in_shutdown;

    /** Timer id: delayed shutdown */
    guint                           kao_shutdown_id;

    /** Data access lock */
    pthread_mutex_t                 kao_mutex;

    /** On shutdown callback
     *
     * Called when kao_refcount_external drops to zero.
     *
     * Object is in locked state.
     *
     * Internal references can be added.
     * External references can't be added.
     * Timers can't be added.
     */
    GDestroyNotify                  kao_shutdown_locked_cb;

    /** On delete callback
     *
     * Called when also kao_refcount_internal drops to zero.
     *
     * References, locking, timers, etc must not be added / removed.
     */
    GDestroyNotify                  kao_delete_cb;
};

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/** Object constructor
 *
 * @param self                Object pointer
 * @param identity            Human readable type identification string
 * @param shutdown_locked_cb  Shutdown notification callback function
 * @param delete_cb           Destroy notification callback function
 */
void keepalive_object_ctor (keepalive_object_t *self, const char *identity, GDestroyNotify shutdown_locked_cb, GDestroyNotify delete_cb);

/** Object destructor
 *
 * @param self                Object pointer
 */
void keepalive_object_dtor (keepalive_object_t *self);

/** Lock object
 *
 * Attempt to lock already locked object is a logic fault and must
 * be expected to cause a deadlock.
 *
 * Attempt to lock invalid object is logic fault and must be
 * expected to cause crash or abort.
 *
 * @param self                Object pointer
 */
void keepalive_object_lock (keepalive_object_t *self);

/** Unlock object
 *
 * Attempt to unlock invalid or already unlocked object is a logic
 * fault and must expected to crash or abort.
 *
 * @param self                Object pointer
 */
void keepalive_object_unlock (keepalive_object_t *self);

/** Add external reference to object
 *
 * Implies strong reference, which blocks both object
 * shutdown and destroy.
 *
 * Attempt to add external references after external reference
 * count has already dropped to zero is a logic fault and will
 * cause an abort.
 *
 * @param self                Object pointer
 *
 * @returns object pointer
 */
void *keepalive_object_ref_external_locked (keepalive_object_t *self);

/** Add interal reference to object
 *
 * Implies weak reference, which blocks object destruction, but
 * not logical shutdown.
 *
 * Attempt to add internal references after external and external
 * reference counts have already dropped to zero is a logic fault
 * and will cause an abort.
 *
 * @param self                Object pointer
 *
 * @returns object pointer
 */
void *keepalive_object_ref_internal_locked (keepalive_object_t *self);

/** Remove external reference to object
 *
 * Attempt to remove external reference when external reference
 * count is already zero causes an abort.
 *
 * @param self                Object pointer
 */
void keepalive_object_unref_external_locked(keepalive_object_t *self);

/** Remove internal reference to object
 *
 * Attempt to remove internal reference when internal reference
 * count is already zero causes an abort.
 *
 * @param self                Object pointer
 */
void keepalive_object_unref_internal_locked(keepalive_object_t *self);

/** Callback function for removing internal reference to object
 *
 * Meant to be used as destroy notification for glib timeouts,
 * dbus pending calls, etc.
 *
 * Locks object, decrements internal reference count and unlocks
 * the object again.
 *
 * @param self                Object pointer
 */
void keepalive_object_unref_internal_cb (void *aptr);

/** Add timer
 *
 * Add glib timer, bind it to object in such manner that object
 * is not deleted until the timer is either cancelled or finalized.
 *
 * If timer_id already contains non-zero value, it is canceled 1st.
 *
 * @param self                Object pointer
 * @param timer_id            Where timer id is stored
 * @param interval            Timer delay in ms; use zero for idle callback
 * @param notify_cb           Timer trigger callback
 *
 */
void keepalive_object_timer_start_locked (keepalive_object_t *self, guint *timer_id, guint interval, GSourceFunc notify_cb);

/** Remove timer
 *
 * Cancel glib timer and unbind it from object.
 *
 * @param self                Object pointer
 * @param timer_id            Where timer id is stored
 */
void keepalive_object_timer_stop_locked(keepalive_object_t *self, guint *timer_id);

/** Predicate for: object is being shut down
 *
 * @param self                Object pointer
 *
 * @returns true if object is waiting to be deleted, false otherwise
 */
bool keepalive_object_in_shutdown_locked(keepalive_object_t *self);

/** Start D-Bus method call
 *
 * Construct D-Bus method call message. Send it using the specified
 * connection. Bind reply handler to  object in such manner that object
 * is not deleted until the pending call is either cancelled or finalized.
 *
 * If where already contains non-zero value, it is canceled 1st.
 *
 * @param self                Object pointer
 * @param where               Where pending call ref is stored
 * @param notify_cb           Notify callback for finished call
 * @param connection          D-Bus connection to use
 * @param service             D-Bus service name
 * @param object              D-Bus object path
 * @param interface           D-Bus interface name
 * @param method              D-Bus method name
 * @param arg_type            The 1st argument type
 * @param va                  Further arguments as va-list
 */
void keepalive_object_ipc_start_locked_va (keepalive_object_t *self, DBusPendingCall **where, DBusPendingCallNotifyFunction notify_cb, DBusConnection *connection, const char *service, const char *object, const char *interface, const char *method, int arg_type, va_list va);

/** Cancel D-Bus method call
 *
 * Cancel D-Bus pending call and unbind it from object.
 *
 * @param self                Object pointer
 * @param where               Where pending call ref is stored
 */
void keepalive_object_ipc_cancel_locked(keepalive_object_t *self, DBusPendingCall **where);

/** Finish D-Bus method call
 *
 * Unbind D-Bus pending call from object.
 *
 * @param self                Object pointer
 * @param where               Where pending call ref is stored
 * @param what                Pending call we are expecting reply to
 *
 * @returns true if stored pending call matched expected and was unbound from
 *          object, false otherwise
 */
bool keepalive_object_ipc_finish_locked(keepalive_object_t *self, DBusPendingCall **where, DBusPendingCall *what);

/** Add iowatch
 *
 * Add glib io watch, bind it to object in such manner that object
 * is not deleted until the io watch is removed.
 *
 * If watch_id already contains non-zero value, it is canceled 1st.
 *
 * @param self                Object pointer
 * @param watch_id            Where io watch id is stored
 * @param fd                  File descriptor to watch
 * @param cnd                 Conditions that trigger notification
 * @param io_cb               Notification callback
 *
 */
void keepalive_object_iowatch_start_locked(keepalive_object_t *self, guint *iowatch_id, int fd, GIOCondition cnd, GIOFunc io_cb);

/** Remove iowatch
 *
 * Cancel glib io watch and unbind it from object.
 *
 * @param self                Object pointer
 * @param watch_id            Where io watch id is stored
 */
void keepalive_object_iowatch_stop_locked (keepalive_object_t *self, guint *iowatch_id);

# ifdef __cplusplus
};
# endif

#endif /* KEEPALIVE_OBJECT_H_ */
