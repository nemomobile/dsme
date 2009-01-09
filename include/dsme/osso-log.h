/**
   @file osso-log.h
   The OSSO logging macros. Version: 19 Apr 2004-2006.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

   @author Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>

   This file is part of Dsme.

   Dsme is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License
   version 2.1 as published by the Free Software Foundation.

   Dsme is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with Dsme.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DSME_OSSO_LOG_H
#define DSME_OSSO_LOG_H

#include <syslog.h>

/*** ULOG_* macros ***/

#define ULOG_CRIT(...) syslog(LOG_CRIT | LOG_USER, __VA_ARGS__)
#define ULOG_ERR(...) syslog(LOG_ERR | LOG_USER, __VA_ARGS__)
#define ULOG_WARN(...) syslog(LOG_WARNING | LOG_USER, __VA_ARGS__)
#define ULOG_INFO(...) syslog(LOG_INFO | LOG_USER, __VA_ARGS__)
#ifdef DEBUG
#define ULOG_DEBUG(...) syslog(LOG_DEBUG | LOG_USER, __VA_ARGS__)
#else
#define ULOG_DEBUG(...) ((void)(0))
#endif

#define ULOG_CRIT_L(FMT, ARG...) syslog(LOG_CRIT | LOG_USER, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#define ULOG_ERR_L(FMT, ARG...) syslog(LOG_ERR | LOG_USER, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#define ULOG_WARN_L(FMT, ARG...) syslog(LOG_WARNING | LOG_USER, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#define ULOG_INFO_L(FMT, ARG...) syslog(LOG_INFO | LOG_USER, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#ifdef DEBUG
#define ULOG_DEBUG_L(FMT, ARG...) syslog(LOG_DEBUG | LOG_USER, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#else
#define ULOG_DEBUG_L(FMT, ARG...) ((void)(0))
#endif

#define ULOG_CRIT_F(FMT, ARG...) syslog(LOG_CRIT | LOG_USER, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#define ULOG_ERR_F(FMT, ARG...) syslog(LOG_ERR | LOG_USER, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#define ULOG_WARN_F(FMT, ARG...) syslog(LOG_WARNING | LOG_USER, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#define ULOG_INFO_F(FMT, ARG...) syslog(LOG_INFO | LOG_USER, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#ifdef DEBUG
#define ULOG_DEBUG_F(FMT, ARG...) syslog(LOG_DEBUG | LOG_USER, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#else
#define ULOG_DEBUG_F(FMT, ARG...) ((void)(0))
#endif

/*** DLOG_* macros ***/

#define DLOG_CRIT(...) syslog(LOG_CRIT | LOG_DAEMON, __VA_ARGS__)
#define DLOG_ERR(...) syslog(LOG_ERR | LOG_DAEMON, __VA_ARGS__)
#define DLOG_WARN(...) syslog(LOG_WARNING | LOG_DAEMON, __VA_ARGS__)
#define DLOG_INFO(...) syslog(LOG_INFO | LOG_DAEMON, __VA_ARGS__)
#ifdef DEBUG
#define DLOG_DEBUG(...) syslog(LOG_DEBUG | LOG_DAEMON, __VA_ARGS__)
#else
#define DLOG_DEBUG(...) ((void)(0))
#endif

#define DLOG_CRIT_L(FMT, ARG...) syslog(LOG_CRIT | LOG_DAEMON, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#define DLOG_ERR_L(FMT, ARG...) syslog(LOG_ERR | LOG_DAEMON, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#define DLOG_WARN_L(FMT, ARG...) syslog(LOG_WARNING | LOG_DAEMON, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#define DLOG_INFO_L(FMT, ARG...) syslog(LOG_INFO | LOG_DAEMON, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#ifdef DEBUG
#define DLOG_DEBUG_L(FMT, ARG...) syslog(LOG_DEBUG | LOG_DAEMON, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#else
#define DLOG_DEBUG_L(FMT, ARG...) ((void)(0))
#endif

#define DLOG_CRIT_F(FMT, ARG...) syslog(LOG_CRIT | LOG_DAEMON, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#define DLOG_ERR_F(FMT, ARG...) syslog(LOG_ERR | LOG_DAEMON, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#define DLOG_WARN_F(FMT, ARG...) syslog(LOG_WARNING | LOG_DAEMON, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#define DLOG_INFO_F(FMT, ARG...) syslog(LOG_INFO | LOG_DAEMON, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#ifdef DEBUG
#define DLOG_DEBUG_F(FMT, ARG...) syslog(LOG_DEBUG | LOG_DAEMON, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#else
#define DLOG_DEBUG_F(FMT, ARG...) ((void)(0))
#endif

/*** KLOG_* macros ***/

#define KLOG_CRIT(...) syslog(LOG_CRIT | LOG_KERN, __VA_ARGS__)
#define KLOG_ERR(...) syslog(LOG_ERR | LOG_KERN, __VA_ARGS__)
#define KLOG_WARN(...) syslog(LOG_WARNING | LOG_KERN, __VA_ARGS__)
#define KLOG_INFO(...) syslog(LOG_INFO | LOG_KERN, __VA_ARGS__)
#ifdef DEBUG
#define KLOG_DEBUG(...) syslog(LOG_DEBUG | LOG_KERN, __VA_ARGS__)
#else
#define KLOG_DEBUG(...) ((void)(0))
#endif

#define KLOG_CRIT_L(FMT, ARG...) syslog(LOG_CRIT | LOG_KERN, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#define KLOG_ERR_L(FMT, ARG...) syslog(LOG_ERR | LOG_KERN, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#define KLOG_WARN_L(FMT, ARG...) syslog(LOG_WARNING | LOG_KERN, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#define KLOG_INFO_L(FMT, ARG...) syslog(LOG_INFO | LOG_KERN, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#ifdef DEBUG
#define KLOG_DEBUG_L(FMT, ARG...) syslog(LOG_DEBUG | LOG_KERN, __FILE__ \
	":%d: " FMT, __LINE__, ## ARG)
#else
#define KLOG_DEBUG_L(FMT, ARG...) ((void)(0))
#endif

#define KLOG_CRIT_F(FMT, ARG...) syslog(LOG_CRIT | LOG_KERN, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#define KLOG_ERR_F(FMT, ARG...) syslog(LOG_ERR | LOG_KERN, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#define KLOG_WARN_F(FMT, ARG...) syslog(LOG_WARNING | LOG_KERN, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#define KLOG_INFO_F(FMT, ARG...) syslog(LOG_INFO | LOG_KERN, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#ifdef DEBUG
#define KLOG_DEBUG_F(FMT, ARG...) syslog(LOG_DEBUG | LOG_KERN, \
	"%s:%d: " FMT, __FUNCTION__, __LINE__, ## ARG)
#else
#define KLOG_DEBUG_F(FMT, ARG...) ((void)(0))
#endif

/*** OPEN and CLOSE macros ***/

#define ULOG_OPEN(X) openlog(X, LOG_PID | LOG_NDELAY, LOG_USER)
#define LOG_CLOSE() closelog()
#define DLOG_OPEN(X) openlog(X, LOG_PID | LOG_NDELAY, LOG_DAEMON)
#define KLOG_OPEN(X) openlog(X, LOG_NDELAY, LOG_KERN)

#endif

/* EOF */
