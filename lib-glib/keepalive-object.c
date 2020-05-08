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

/** @file keepalive-object.c
 *
 * @brief Provides common base for locking and reference counting.
 */

#include "keepalive-object.h"

#include "logging.h"
#include "xdbus.h"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * OBJECT_LIFETIME
 * ------------------------------------------------------------------------- */

static gboolean  keepalive_object_shutdown_cb          (gpointer aptr);
static void      keepalive_object_shutdown_locked      (keepalive_object_t *self);
static void      keepalive_object_destroy              (keepalive_object_t *self);
bool             keepalive_object_in_shutdown_locked   (keepalive_object_t *self);
void             keepalive_object_ctor                 (keepalive_object_t *self, const char *identity, GDestroyNotify shutdown_locked_cb, GDestroyNotify delete_cb);
void             keepalive_object_dtor                 (keepalive_object_t *self);
void             keepalive_object_lock                 (keepalive_object_t *self);
void             keepalive_object_unlock               (keepalive_object_t *self);
void            *keepalive_object_ref_external_locked  (keepalive_object_t *self);
void            *keepalive_object_ref_internal_locked  (keepalive_object_t *self);
void             keepalive_object_unref_external_locked(keepalive_object_t *self);
void             keepalive_object_unref_internal_locked(keepalive_object_t *self);
void             keepalive_object_unref_internal_cb    (void *aptr);

/* ------------------------------------------------------------------------- *
 * OBJECT_TIMERS
 * ------------------------------------------------------------------------- */

void keepalive_object_timer_start_locked(keepalive_object_t *self, guint *timer_id, guint interval, GSourceFunc notify_cb);
void keepalive_object_timer_stop_locked (keepalive_object_t *self, guint *timer_id);

/* ------------------------------------------------------------------------- *
 * OBJECT_DBUS_IPC
 * ------------------------------------------------------------------------- */

void keepalive_object_ipc_start_locked_va(keepalive_object_t *self, DBusPendingCall **where, DBusPendingCallNotifyFunction notify_cb, DBusConnection *connection, const char *service, const char *object, const char *interface, const char *method, int arg_type, va_list va);
void keepalive_object_ipc_cancel_locked  (keepalive_object_t *self, DBusPendingCall **where);
bool keepalive_object_ipc_finish_locked  (keepalive_object_t *self, DBusPendingCall **where, DBusPendingCall *what);

/* ------------------------------------------------------------------------- *
 * OBJECT_IOWATCHES
 * ------------------------------------------------------------------------- */

void keepalive_object_iowatch_start_locked(keepalive_object_t *self, guint *iowatch_id, int fd, GIOCondition cnd, GIOFunc io_cb);
void keepalive_object_iowatch_stop_locked (keepalive_object_t *self, guint *iowatch_id);

/* ========================================================================= *
 * OBJECT_LIFETIME
 * ========================================================================= */

static gboolean
keepalive_object_shutdown_cb(gpointer aptr)
{
    keepalive_object_t *self = aptr;

    log_function("%s=%p", self->kao_identity, self);
    keepalive_object_lock(self);
    if( self->kao_shutdown_id ) {
        self->kao_shutdown_id = 0;
        self->kao_in_shutdown = true;
        self->kao_shutdown_locked_cb(self);
    }
    keepalive_object_unlock(self);
    return G_SOURCE_REMOVE;
}

static void
keepalive_object_shutdown_locked(keepalive_object_t *self)
{
    if( !self->kao_in_shutdown && !self->kao_shutdown_id ) {
        log_function("%s=%p", self->kao_identity, self);
        keepalive_object_timer_start_locked(self,
                                          &self->kao_shutdown_id,
                                          0,
                                          keepalive_object_shutdown_cb);
    }
}

static void
keepalive_object_destroy(keepalive_object_t *self)
{
    log_function("%s=%p", self->kao_identity, self);
    self->kao_delete_cb(self);
}

bool
keepalive_object_in_shutdown_locked(keepalive_object_t *self)
{
    return self->kao_in_shutdown;
}

void
keepalive_object_ctor(keepalive_object_t *self,
                      const char *identity,
                      GDestroyNotify shutdown_locked_cb,
                      GDestroyNotify delete_cb)
{
    self->kao_identity           = identity;
    self->kao_refcount_external  = 1;
    self->kao_refcount_internal  = 0;
    self->kao_in_shutdown        = false;
    self->kao_shutdown_id        = 0;
    self->kao_shutdown_locked_cb = shutdown_locked_cb;
    self->kao_delete_cb          = delete_cb;

    log_function("%s=%p", self->kao_identity, self);

    if( pthread_mutex_init(&self->kao_mutex, 0) != 0 )
        log_abort("mutex init failed");
}

void
keepalive_object_dtor(keepalive_object_t *self)
{
    log_function("%s=%p", self->kao_identity, self);

    if( pthread_mutex_destroy(&self->kao_mutex) != 0 )
        log_abort("mutex destroy failed");
}

void
keepalive_object_lock(keepalive_object_t *self)
{
    log_function("%s=%p", self->kao_identity, self);
    if( pthread_mutex_lock(&self->kao_mutex) != 0 )
        log_abort("mutex lock failed @ %p", self);
}

void
keepalive_object_unlock(keepalive_object_t *self)
{
    log_function("%s=%p", self->kao_identity, self);

    bool destroy = (self->kao_refcount_external == 0 &&
                    self->kao_refcount_internal == 0);

    if( pthread_mutex_unlock(&self->kao_mutex) != 0 )
        log_abort("mutex unlock failed @ %p", self);

    if( destroy )
        keepalive_object_destroy(self);
}

/** Add external reference
 *
 * Object stays functional while there are external references.
 *
 * When the last external reference is dropped, object is still
 * kept in memory for the purpose of canceling / finalizing all
 * asynchronous activity guarded by internal references.
 */
void *
keepalive_object_ref_external_locked(keepalive_object_t *self)
{
    if( self->kao_refcount_external <= 0 )
        log_abort("adding ref to invalid object @ %p", self);

    self->kao_refcount_external += 1;

    log_function("%s=%p: %d + %d", self->kao_identity, self,
                 self->kao_refcount_external,
                 self->kao_refcount_internal);

    return self;
}

/** Add internal reference
 *
 * Object stays available while there are internal references,
 */
void *
keepalive_object_ref_internal_locked(keepalive_object_t *self)
{
    if( self->kao_refcount_external <= 0 && self->kao_refcount_internal <= 0 )
        log_abort("adding weak ref to invalid object @ %p", self);

    self->kao_refcount_internal += 1;

    log_function("%s=%p: %d + %d", self->kao_identity, self,
                 self->kao_refcount_external,
                 self->kao_refcount_internal);

    return self;
}

/** Remove external reference
 */
void
keepalive_object_unref_external_locked(keepalive_object_t *self)
{
    if( self->kao_refcount_external <= 0 )
        log_abort("removing ref to invalid object @ %p", self);

    /* Once external refs are zero, internal refs can't be
     * added -> shutdown activity must be scheduled prior to
     * decrementing external ref count.
     */
    if( self->kao_refcount_external == 1 )
        keepalive_object_shutdown_locked(self);

    self->kao_refcount_external -= 1;

    log_function("%s=%p: %d + %d", self->kao_identity, self,
                 self->kao_refcount_external,
                 self->kao_refcount_internal);

    /* Note: keepalive_object_unlock() destroys object on zero-zero refcount.
     */
}

/** Remove internal reference
 */
void
keepalive_object_unref_internal_locked(keepalive_object_t *self)
{
    if( self->kao_refcount_internal <= 0 )
        log_abort("removing weak ref to invalid object @ %p", self);

    self->kao_refcount_internal -= 1;

    log_function("%s=%p: %d + %d", self->kao_identity, self,
                 self->kao_refcount_external,
                 self->kao_refcount_internal);

    /* Note: keepalive_object_unlock() destroys object on zero-zero refcount.
     */
}

/** Callback for releasing internal reference
 */
void
keepalive_object_unref_internal_cb(void *aptr)
{
    keepalive_object_t *self = aptr;

    log_function("%s=%p", self->kao_identity, self);
    keepalive_object_lock(self);
    keepalive_object_unref_internal_locked(self);
    keepalive_object_unlock(self);
}

/* ========================================================================= *
 * OBJECT_TIMERS
 * ========================================================================= */

void
keepalive_object_timer_start_locked(keepalive_object_t *self, guint *timer_id,
                                  guint interval, GSourceFunc notify_cb)
{
    log_function("%s=%p", self->kao_identity, self);

    keepalive_object_ref_internal_locked(self);
    keepalive_object_timer_stop_locked(self, timer_id);

    if( self->kao_in_shutdown )
        log_warning("attempt to add timer during object shutdown");
    else if( interval > 0 )
        *timer_id = g_timeout_add_full(G_PRIORITY_DEFAULT, interval,
                                       notify_cb, self,
                                       keepalive_object_unref_internal_cb);
    else
        *timer_id = g_idle_add_full(G_PRIORITY_DEFAULT, notify_cb,
                                    self, keepalive_object_unref_internal_cb);

    if( !*timer_id )
        keepalive_object_unref_internal_locked(self);

    return;
}

void
keepalive_object_timer_stop_locked(keepalive_object_t *self, guint *timer_id)
{
    log_function("%s=%p", self->kao_identity, self);

    /* Timer destroy notify calls locking keepalive_object_unref_internal_cb()
     * -> must unlock during g_source_remove()
     * -> in theory timer slot might get refilled
     * -> must repeat until locked again with cleared slot
     */
    guint id;
    while( (id = *timer_id) ) {
        *timer_id = 0;
        keepalive_object_unlock(self);
        g_source_remove(id);
        keepalive_object_lock(self);
    }
}

/* ========================================================================= *
 * OBJECT_DBUS_IPC
 * ========================================================================= */

void
keepalive_object_ipc_start_locked_va(keepalive_object_t *self,
                                     DBusPendingCall **where,
                                     DBusPendingCallNotifyFunction notify_cb,
                                     DBusConnection *connection,
                                     const char *service,
                                     const char *object,
                                     const char *interface,
                                     const char *method,
                                     int arg_type, va_list va)
{
    log_function("%p", self);

    /* Canceling might drop the last ref we have, so
     * a fresh one needs to be obtained first.
     */
    keepalive_object_ref_internal_locked(self);
    keepalive_object_ipc_cancel_locked(self, where);

    /* Start async call that will unref via pending call
     * destroy notification on completion / cancellation.
     */
    DBusPendingCall *pc = xdbus_method_call_va(connection,
                                               service,
                                               object,
                                               interface,
                                               method,
                                               notify_cb,
                                               self,
                                               keepalive_object_unref_internal_cb,
                                               arg_type, va);

    /* If starting async call failed, unref immediately.
     */
    if( !(*where = pc) )
        keepalive_object_unref_internal_locked(self);
}

void
keepalive_object_ipc_cancel_locked(keepalive_object_t *self,
                                   DBusPendingCall **where)
{
    log_function("%p", self);

    /* Pending call destroy notif calls locking keepalive_object_unref()
     * -> must unlock during dbus_pending_call_unref()
     * -> pending call slot might get refilled
     * -> repeat until locked again with cleared slot
     */

    DBusPendingCall *pc;
    while( (pc = *where) ) {
        *where = 0;
        dbus_pending_call_cancel(pc);
        keepalive_object_unlock(self);
        dbus_pending_call_unref(pc);
        keepalive_object_lock(self);
    }
}

 bool
keepalive_object_ipc_finish_locked(keepalive_object_t *self,
                                   DBusPendingCall **where,
                                   DBusPendingCall *what)
{
    (void)self;

    log_function("%p", self);

    bool finished = false;
    if( *where && *where == what ) {
        /* Assumed: Caller holds 'what' reference, so this is not going
         *          to drive refcount to zero -> unref while holding lock.
         */
        dbus_pending_call_unref(*where),
            *where = 0;
        finished = true;
    }
    return finished;
}

/* ========================================================================= *
 * OBJECT_IOWATCHES
 * ========================================================================= */

/** Helper for creating I/O watch for file descriptor
 */
void
keepalive_object_iowatch_start_locked(keepalive_object_t *self, guint *iowatch_id,
                                      int fd, GIOCondition cnd, GIOFunc io_cb)
{
    guint         wid = 0;
    GIOChannel   *chn = 0;
    guint         pri = G_PRIORITY_DEFAULT;

    keepalive_object_ref_internal_locked(self);
    keepalive_object_iowatch_stop_locked(self, iowatch_id);

    log_function("%p", self);

    if( keepalive_object_in_shutdown_locked(self) ) {
        log_warning("attempt to add iowatch after object shutdown");
    }
    else if( (chn = g_io_channel_unix_new(fd)) ) {
        cnd |= G_IO_ERR | G_IO_HUP | G_IO_NVAL;
        wid = g_io_add_watch_full(chn, pri, cnd, io_cb, self,
                                  keepalive_object_unref_internal_cb);
    }

    if( !(*iowatch_id = wid) )
        keepalive_object_unref_internal_locked(self);

    if( chn )
        g_io_channel_unref(chn);
}

void
keepalive_object_iowatch_stop_locked(keepalive_object_t *self, guint *iowatch_id)
{
    log_function("%p", self);
    guint id;
    /* Removing source might trigger callbacks
     * -> must be done in unlocked state
     * Unlocking allows somebody else to set iowatch slot
     * -> loop until locked and slot is cleared
     */
    while( (id = *iowatch_id) ) {
        *iowatch_id = 0;
        keepalive_object_unlock(self);
        g_source_remove(id);
        keepalive_object_lock(self);
    }
}
