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

/* NOTE: Internal to libkeepalive-glib
 *
 * These functions are not exported and the header must not
 * be included in the devel package
 */

#ifndef KEEPALIVE_GLIB_LOGGING_H_
# define KEEPALIVE_GLIB_LOGGING_H_

# include <stdbool.h>
# include <syslog.h>

# ifdef __cplusplus
extern "C" {
# elif 0
} /* fool JED indentation ... */
# endif

/** Default logging verbosity to enable */
# ifndef LOGGING_DEFAULT_LEVEL
#  define LOGGING_DEFAULT_LEVEL LOG_WARNING
# endif

/** Default logging level to compile in */
# ifndef LOGGING_BUILD_LEVEL
#  define LOGGING_BUILD_LEVEL LOG_DEBUG
# endif

/** Default function entry logging */
# ifndef LOGGING_TRACE_FUNCTIONS
#  define LOGGING_TRACE_FUNCTIONS 0
# endif

void log_set_verbosity(int lev);
int  log_get_verbosity(void);

bool log_p(int lev);

void log_emit_(int lev, const char *func, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#define log_emit_func(LEV, FMT, ARGS...) do { \
    if( log_p(LEV) )\
        log_emit_(LEV, __func__, FMT, ## ARGS);\
} while(0)

#define log_emit_plain(LEV, FMT, ARGS...) do { \
    if( log_p(LEV) )\
        log_emit_(LEV, 0, FMT, ## ARGS);\
} while(0)

# if LOGGING_BUILD_LEVEL >= LOG_CRIT
#  define log_crit(   FMT, ARGS...) log_emit_plain(LOG_CRIT, FMT, ## ARGS)
# else
#  define log_crit(   FMT, ARGS...) do { } while( 0 )
# endif

# if LOGGING_BUILD_LEVEL >= LOG_ERR
#  define log_error(  FMT, ARGS...) log_emit_plain(LOG_ERR, FMT, ## ARGS)
# else
#  define log_error(  FMT, ARGS...) do { } while( 0 )
# endif

# if LOGGING_BUILD_LEVEL >= LOG_WARNING
#  define log_warning(FMT, ARGS...) log_emit_plain(LOG_WARNING, FMT, ## ARGS)
# else
#  define log_warning(FMT, ARGS...) do { } while( 0 )
# endif

# if LOGGING_BUILD_LEVEL >= LOG_NOTICE
#  define log_notice( FMT, ARGS...) log_emit_plain(LOG_NOTICE, FMT, ## ARGS)
# else
#  define log_notice( FMT, ARGS...) do { } while( 0 )
# endif

# if LOGGING_BUILD_LEVEL >= LOG_INFO
#  define log_info(   FMT, ARGS...) log_emit_plain(LOG_INFO, FMT, ## ARGS)
# else
#  define log_info(   FMT, ARGS...) do { } while( 0 )
# endif

# if LOGGING_BUILD_LEVEL >= LOG_DEBUG
#  define log_debug(  FMT, ARGS...) log_emit_plain(LOG_DEBUG, FMT, ## ARGS)
# else
#  define log_debug(  FMT, ARGS...) do { } while( 0 )
# endif

# if LOGGING_TRACE_FUNCTIONS
#  define log_enter_function()       log_emit_func(LOG_WARNING, "...")
#  define log_function(FMT, ARGS...) log_emit_func(LOG_WARNING, FMT, ## ARGS)
#else
#  define log_enter_function()       log_emit_func(LOG_DEBUG, "...")
#  define log_function(FMT, ARGS...) log_emit_func(LOG_DEBUG, FMT, ## ARGS)
#endif

#define log_abort(    FMT, ARGS...) do { log_crit(FMT " - aborted", ## ARGS); abort(); } while(0)

# ifdef __cplusplus
};
# endif

#endif /* KEEPALIVE_GLIB_LOGGING_H_ */
