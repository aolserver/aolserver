/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 * 
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */

#ifndef NSCONF_H
#define NSCONF_H

#define LOG_EXPANDED_BOOL      NS_FALSE
#define LOG_DEBUG_BOOL         NS_FALSE
#define LOG_NOTICE_BOOL        NS_TRUE
#define LOG_DEV_BOOL           NS_FALSE
#define LOG_ROLL_BOOL          NS_TRUE
#define LOG_BUFFER_BOOL        NS_FALSE
#define LOG_MAXBUFFER_INT      10
#define LOG_MAXLEVEL_INT       INT_MAX
#define LOG_MAXBACK_INT        10
#define LOG_FLUSHINT_INT       10

#define THREAD_MUTEXMETER_BOOL NS_FALSE
#define THREAD_STACKSIZE_INT   nsThreadStackSize

#define SCHED_MAXELAPSED_INT   2

#define SHUTDOWNTIMEOUT        20
#define STARTUPTIMEOUT         20
#define IOBUFSIZE              16000
#define BACKLOG                32

#define DNS_CACHE_BOOL         NS_TRUE
#define DNS_TIMEOUT_INT        60

#define DSTRING_MAXSIZE_INT    3*1024
#define DSTRING_MAXENTRIES_INT 10

#define EXEC_CHECKEXIT_BOOL    NS_TRUE

#define KEEPALIVE_MAXKEEP_INT  100
#define KEEPALIVE_TIMEOUT_INT  30

#define SERV_AOLPRESS_BOOL     NS_FALSE
#define SERV_CONNSPERTHREAD_INT 0
#define SERV_ERRORMINSIZE_INT  514
#define SERV_GLOBALSTATS_BOOL  NS_FALSE
#define SERV_MAXCONNS_INT      100
#define SERV_MAXDROPPED_INT    0
#define SERV_MAXTHREADS_INT    20
#define SERV_MAXURLSTATS_INT   1000
#define SERV_MINTHREADS_INT    0
#define SERV_NOTICEDETAIL_BOOL NS_TRUE
#define SERV_SENDFDMIN_INT     2048
#define SERV_THREADTIMEOUT_INT 120
#define SERV_URLSTATS_BOOL     NS_FALSE
#define SERV_QUIET_BOOL        NS_FALSE

#define ADP_CACHESIZE_INT      5000*1024
#define ADP_CACHE_BOOL         NS_TRUE
#define ADP_ENABLEDEBUG_BOOL   NS_FALSE
#define ADP_ENABLEEXPIRE_BOOL  NS_FALSE
#define ADP_TAGLOCKS_BOOL      NS_FALSE

#define CONN_FLUSHCONTENT_BOOL NS_FALSE
#define CONN_MAXHEADERS_INT    16384
#define CONN_MAXLINE_INT       8192
#define CONN_MAXPOST_INT       65536
#define CONN_MODSINCE_BOOL     NS_TRUE


#define FASTPATH_CACHEMAXENTRY_INT 8192
#define FASTPATH_CACHESIZE_INT 5000*1024
#define FASTPATH_CACHE_BOOL    NS_TRUE
#define FASTPATH_MMAP_BOOL     NS_FALSE


#define TCL_AUTOCLOSE_BOOL     NS_TRUE
#define TCL_DEBUG_BOOL         NS_FALSE
#define TCL_NSVBUCKETS_INT     8
#define TCL_STATLEVEL_INT      0
#define TCL_STATMAXBUF_INT     1000

#endif
