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

/** @file keepalive-cpukeepalive.h
 *
 * @brief Provides wrapper API for MCE CPU-keepalive D-Bus interface.
 */

#ifndef KEEPALIVE_GLIB_CPUKEEPALIVE_H_
# define KEEPALIVE_GLIB_CPUKEEPALIVE_H_

# ifdef __cplusplus
extern "C" {
# elif 0
} /* fool JED indentation ... */
# endif

# pragma GCC visibility push(default)

/** Opaque CPU-keepalive structure
 *
 * Allocate via cpukeepalive_new() and
 * release via cpukeepalive_unref().
 */
typedef struct cpukeepalive_t cpukeepalive_t;

/** Create CPU-keepalive object
 *
 * Initially has reference count of 1.
 *
 * Use cpukeepalive_ref() to increment reference count and
 * cpukeepalive_unref() to decrement reference count.
 *
 * Will be automatically released after reference count drops to zero.
 *
 * @return pointer to CPU-keepalive object, or NULL
 */
cpukeepalive_t *cpukeepalive_new(void);

/** Increment reference count of CPU-keepalive object
 *
 * Passing NULL object is explicitly allowed and does nothing.
 *
 * @param self  CPU-keepalive object
 *
 * @return pointer to CPU-keepalive object, or NULL in case of errors
 */
cpukeepalive_t *cpukeepalive_ref(cpukeepalive_t *self);

/** Decrement reference count of CPU-keepalive object
 *
 * Passing NULL object is explicitly allowed and does nothing.
 *
 * @param self  CPU-keepalive object
 *
 * The object will be released if reference count reaches zero.
 */
void cpukeepalive_unref(cpukeepalive_t *self);

/** Disable normal device suspend policy
 *
 * The CPU-keepalive object makes the necessary D-Bus IPC that keeps
 * the device from suspending while/when the following conditions are met:
 * 1) MCE is running
 *
 * @param self  CPU-keepalive object
 */
void cpukeepalive_start(cpukeepalive_t *self);

/** Enable normal device suspend policy
 *
 * @param self  CPU-keepalive object
 */
void cpukeepalive_stop(cpukeepalive_t *self);

/** Get keepalive id string
 *
 * Normally the id string is used to identify CPU-keepalive object
 * when making D-Bus IPC with MCE, but can be also used if application
 * code needs to have some unique within the process key string to
 * associate with the CPU-keepalive object.
 *
 * @param self  CPU-keepalive object
 *
 * @return id string
 */
const char *cpukeepalive_get_id(const cpukeepalive_t *self);

# pragma GCC visibility pop

# ifdef __cplusplus
};
# endif

#endif // KEEPALIVE_GLIB_CPUKEEPALIVE_H_
