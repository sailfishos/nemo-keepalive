/****************************************************************************************
**
** Copyright (C) 2014 Jolla Ltd.
** Contact: Simo Piiroinen <simo.piiroinen@jollamobile.com>
** All rights reserved.
**
** This file is part of nemo keepalive package.
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

/** @mainpage
 *
 * @section introduction Introduction
 *
 * Normally when there are no signs of user activity, display will
 * be powered off and soon after that the device is suspended.
 *
 * This is troublesome for applications that want to:
 *
 * - Show something on screen without really expecting user interaction
 *   (any application that does video playback).
 *
 * - Continue processing even if display does get blanked (say
 *   something like fractal generator).
 *
 * - Wake up at some specific time (normal timers are frozen when
 *   device is suspended).
 *
 * While there are low level solutions to these problems, the
 * availability might vary from one device / kernel version to
 * another.
 *
 * What libkeepalive is meant to provide is: Make available a stable
 * API that will work on all Mer based devices even if the underlying
 * implementation details should change in the future.
 *
 * @section preventblanking Preventing Display Blanking
 *
 * Use functionality listed in keepalive-displaykeepalive.h
 *
 * Example: @ref keep-display-on.c "keep-display-on.c"
 *
 * @section preventsuspending Prevent Device From Suspending
 *
 * Use functionality listed in keepalive-cpukeepalive.h
 *
 * Example: @ref block-suspend.c "block-suspend.c"
 *
 * @section backgroundactivity Schedule Background Activity
 *
 * Use functionality listed in keepalive-backgroundactivity.h
 *
 * Example: @ref periodic-wakeup.c "periodic-wakeup.c"
 *
 * @section glibtimercompat GLib Timeout Compatibility API
 *
 * In some cases application just needs to ensure it wakes up to handle
 * simple tasks. For this purpose keepalive-timeout.h provides timer
 * functions that can be used as suspend proof drop in replacement for
 * glib timeouts.
 *
 * Example: @ref simple-timer-wakeup.c "simple-timer-wakeup.c"
 *
 */

/** @example keep-display-on.c
 *
 * @brief Simple example to demonstrate use of displaykeepalive_t objects
 * to prevent display blanking.
 */

/** @example block-suspend.c
 *
 * @brief Simple example to demonstrate use of cpukeepalive_t objects
 * to block device from suspending.
 */

/** @example periodic-wakeup.c
 *
 * @brief Simple example to demonstrate use of background_activity_t objects
 * to block device from suspending / while performing periodic tasks.
 */

/** @example simple-timer-wakeup.c
 *
 * @brief Simple example to demonstrate glib timeout like timers that
 * can wake the device from suspend.
 */

/** @file keepalive.h
 *
 * @brief Master header for including all libkeepalive features.
 */

#ifndef KEEPALIVE_GLIB_KEEPALIVE_H_
# define KEEPALIVE_GLIB_KEEPALIVE_H_

# ifdef __cplusplus
extern "C" {
# elif 0
} /* fool JED indentation ... */
# endif

# include "keepalive-heartbeat.h"
# include "keepalive-cpukeepalive.h"
# include "keepalive-displaykeepalive.h"
# include "keepalive-backgroundactivity.h"
# include "keepalive-timeout.h"

# ifdef __cplusplus
};
# endif

#endif /* KEEPALIVE_GLIB_KEEPALIVE_H_ */
