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

/*
 * tclcmds.c --
 *
 * 	Connect Tcl command names to the functions that implement them
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclcmds.c,v 1.36 2003/01/31 22:47:30 mpagenva Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Tcl object and string commands.
 */

extern Tcl_ObjCmdProc
    NsTclAdpAppendObjCmd,
    NsTclAdpPutsObjCmd,
    NsTclAdpEvalObjCmd,
    NsTclAdpSafeEvalObjCmd,
    NsTclAdpIncludeObjCmd,
    NsTclAdpParseObjCmd,
    NsTclAdpDirObjCmd,
    NsTclAdpReturnObjCmd,
    NsTclAdpBreakObjCmd,
    NsTclAdpAbortObjCmd,
    NsTclAdpTellObjCmd,
    NsTclAdpTruncObjCmd,
    NsTclAdpDumpObjCmd,
    NsTclAdpArgcObjCmd,
    NsTclAdpArgvObjCmd,
    NsTclAdpBindArgsObjCmd,
    NsTclAdpExceptionObjCmd,
    NsTclAdpStreamObjCmd,
    NsTclAdpMimeTypeObjCmd,
    NsTclChanObjCmd,
    NsTclChmodObjCmd,
    NsTclCondObjCmd,
    NsTclConnObjCmd,
    NsTclConnSendFpObjCmd,
    NsTclCpFpObjCmd,
    NsTclCpObjCmd,
    NsTclCryptObjCmd,
    NsTclCritSecObjCmd,
    NsTclDummyObjCmd,
    NsTclICtlObjCmd,
    NsTclFTruncateObjCmd,
    NsTclGetAddrObjCmd,
    NsTclGetHostObjCmd,
    NsTclGetUrlObjCmd,
    NsTclGifSizeObjCmd,
    NsTclGmTimeObjCmd,
    NsTclGuessTypeObjCmd,
    NsTclHTUUDecodeObjCmd,
    NsTclHTUUEncodeObjCmd,
    NsTclHeadersObjCmd,
    NsTclHttpObjCmd,
    NsTclHttpTimeObjCmd,
    NsTclInfoObjCmd,
    NsTclJobObjCmd,
    NsTclJpegSizeObjCmd,
    NsTclKillObjCmd,
    NsTclLinkObjCmd,
    NsTclLocalTimeObjCmd,
    NsTclLogObjCmd,
    NsTclLogCtlObjCmd,
    NsTclLogRollObjCmd,
    NsTclMarkForDeleteObjCmd,
    NsTclMkdirObjCmd,
    NsTclModulePathObjCmd,
    NsTclMutexObjCmd,
    NsTclNormalizePathObjCmd,
    NsTclNsvAppendObjCmd,
    NsTclNsvArrayObjCmd,
    NsTclNsvExistsObjCmd,
    NsTclNsvGetObjCmd,
    NsTclNsvIncrObjCmd,
    NsTclNsvLappendObjCmd,
    NsTclNsvNamesObjCmd,
    NsTclNsvSetObjCmd,
    NsTclNsvUnsetObjCmd,
    NsTclParseHttpTimeObjCmd,
    NsTclParseQueryObjCmd,
    NsTclPurgeFilesObjCmd,
    NsTclRandObjCmd,
    NsTclRegisterAdpObjCmd,
    NsTclRegisterFilterObjCmd,
    NsTclRegisterProcObjCmd,
    NsTclRegisterTraceObjCmd,
    NsTclRenameObjCmd,
    NsTclRequestAuthorizeObjCmd,
    NsTclRespondObjCmd,
    NsTclReturnBadRequestObjCmd,
    NsTclReturnErrorObjCmd,
    NsTclReturnFileObjCmd,
    NsTclReturnForbiddenObjCmd,
    NsTclReturnFpObjCmd,
    NsTclReturnNotFoundObjCmd,
    NsTclReturnObjCmd,
    NsTclReturnRedirectObjCmd,
    NsTclReturnUnauthorizedObjCmd,
    NsTclRmdirObjCmd,
    NsTclRollFileObjCmd,
    NsTclRWLockObjCmd,
    NsTclSelectObjCmd,
    NsTclSemaObjCmd,
    NsTclServerObjCmd,
    NsTclSetObjCmd,
    NsTclShutdownObjCmd,
    NsTclSleepObjCmd,
    NsTclSockAcceptObjCmd,
    NsTclSockCallbackObjCmd,
    NsTclSockCheckObjCmd,
    NsTclSockListenCallbackObjCmd,
    NsTclSockListenObjCmd,
    NsTclSockNReadObjCmd,
    NsTclSockOpenObjCmd,
    NsTclSockSetBlockingObjCmd,
    NsTclSockSetNonBlockingObjCmd,
    NsTclSocketPairObjCmd,
    NsTclStartContentObjCmd,
    NsTclStrftimeObjCmd,
    NsTclSymlinkObjCmd,
    NsTclTimeObjCmd,
    NsTclTmpNamObjCmd,
    NsTclTruncateObjCmd,
    NsTclUnRegisterObjCmd,
    NsTclUnlinkObjCmd,
    NsTclUrl2FileObjCmd,
    NsTclUrlDecodeObjCmd,
    NsTclUrlEncodeObjCmd,
    NsTclVarObjCmd,
    NsTclWriteContentObjCmd,
    NsTclWriteFpObjCmd,
    NsTclWriteObjCmd;

extern Tcl_CmdProc
    NsTclAdpDebugCmd,
    NsTclAdpRegisterAdpCmd,
    NsTclAdpRegisterAdpCmd,
    NsTclAdpRegisterProcCmd,
    NsTclAdpStatsCmd,
    NsTclAfterCmd,
    NsTclAtCloseCmd,
    NsTclAtExitCmd,
    NsTclAtShutdownCmd,
    NsTclAtSignalCmd,
    NsTclCacheFlushCmd,
    NsTclCacheKeysCmd,
    NsTclCacheNamesCmd,
    NsTclCacheSizeCmd,
    NsTclCacheStatsCmd,
    NsTclCancelCmd,
    NsTclCharsetsCmd,
    NsTclConfigCmd,
    NsTclConfigSectionCmd,
    NsTclConfigSectionsCmd,
    NsTclEncodingForCharsetCmd,
    NsTclEnvCmd,
    NsTclEnvCmd,
    NsTclHrefsCmd,
    NsTclLibraryCmd,
    NsTclMkTempCmd,
    NsTclParseHeaderCmd,
    NsTclPauseCmd,
    NsTclQuoteHtmlCmd,
    NsTclRegisterTagCmd,
    NsTclResumeCmd,
    NsTclReturnAdminNoticeCmd,
    NsTclReturnNoticeCmd,
    NsTclSchedCmd,
    NsTclSchedDailyCmd,
    NsTclSchedWeeklyCmd,
    NsTclShareCmd,
    NsTclStripHtmlCmd,
    NsTclThreadCmd,
    NsTclUnscheduleCmd,
    Tcl_KeyldelCmd,
    Tcl_KeylgetCmd,
    Tcl_KeylkeysCmd,
    Tcl_KeylsetCmd;

/*
 * The following structure defines a command to be created
 * in new interps.
 */

typedef struct Cmd {
    char *name;
    Tcl_CmdProc *proc;
    Tcl_ObjCmdProc *objProc;
} Cmd;

/*
 * The following commands are generic, available in the config
 * and virtual server interps.
 */

static Cmd cmds[] = {
    {"ns_crypt", NULL, NsTclCryptObjCmd},
    {"ns_sleep", NULL, NsTclSleepObjCmd},
    {"ns_localtime", NULL, NsTclLocalTimeObjCmd},
    {"ns_gmtime", NULL, NsTclGmTimeObjCmd},
    {"ns_time", NULL, NsTclTimeObjCmd},
    {"ns_fmttime", NULL, NsTclStrftimeObjCmd},
    {"ns_httptime", NULL, NsTclHttpTimeObjCmd},
    {"ns_parsehttptime", NULL, NsTclParseHttpTimeObjCmd},
    {"ns_parsequery", NULL, NsTclParseQueryObjCmd},

    {"ns_rand", NULL, NsTclRandObjCmd},

    {"ns_info", NULL, NsTclInfoObjCmd},
    {"ns_modulepath", NULL, NsTclModulePathObjCmd},

    /*
     * log.c
     */

    {"ns_log", NULL, NsTclLogObjCmd},
    {"ns_logctl", NULL, NsTclLogCtlObjCmd},
    {"ns_logroll", NULL, NsTclLogRollObjCmd},

    {"ns_urlencode", NULL, NsTclUrlEncodeObjCmd},
    {"ns_urldecode", NULL, NsTclUrlDecodeObjCmd},
    {"ns_uuencode", NULL, NsTclHTUUEncodeObjCmd},
    {"ns_uudecode", NULL, NsTclHTUUDecodeObjCmd},
    {"ns_gifsize", NULL, NsTclGifSizeObjCmd},
    {"ns_jpegsize", NULL, NsTclJpegSizeObjCmd},
    {"ns_guesstype", NULL, NsTclGuessTypeObjCmd},

    {"ns_striphtml", NsTclStripHtmlCmd, NULL},
    {"ns_quotehtml", NsTclQuoteHtmlCmd, NULL},
    {"ns_hrefs", NsTclHrefsCmd, NULL},

    /*
     * tclconf.c
     */

    {"ns_config", NsTclConfigCmd, NULL},
    {"ns_configsection", NsTclConfigSectionCmd, NULL},
    {"ns_configsections", NsTclConfigSectionsCmd, NULL},

    /*
     * tclfile.c
     */

    {"ns_unlink", NULL, NsTclUnlinkObjCmd},
    {"ns_mkdir", NULL, NsTclMkdirObjCmd},
    {"ns_rmdir", NULL, NsTclRmdirObjCmd},
    {"ns_cp", NULL, NsTclCpObjCmd},
    {"ns_cpfp", NULL, NsTclCpFpObjCmd},
    {"ns_rollfile", NULL, NsTclRollFileObjCmd},
    {"ns_purgefiles", NULL, NsTclPurgeFilesObjCmd},
    {"ns_mktemp", NsTclMkTempCmd, NULL},
    {"ns_tmpnam", NULL, NsTclTmpNamObjCmd},
    {"ns_normalizepath", NULL, NsTclNormalizePathObjCmd},
    {"ns_link", NULL, NsTclLinkObjCmd},
    {"ns_symlink", NULL, NsTclSymlinkObjCmd},
    {"ns_rename", NULL, NsTclRenameObjCmd},
    {"ns_kill", NULL, NsTclKillObjCmd},
    {"ns_writefp", NULL, NsTclWriteFpObjCmd},
    {"ns_truncate", NULL, NsTclTruncateObjCmd},
    {"ns_ftruncate", NULL, NsTclFTruncateObjCmd},
    {"ns_chmod", NULL, NsTclChmodObjCmd},

    /*
     * tclenv.c
     */

    {"ns_env", NsTclEnvCmd, NULL},
    {"env", NsTclEnvCmd, NULL}, /* NB: Backwards compatible. */

    /*
     * tcljob.c
     */

    {"ns_job", NULL, NsTclJobObjCmd},

    /*
     * tclhttp.c
     */

    {"ns_http", NULL, NsTclHttpObjCmd},

    /*
     * tclsched.c
     */

    {"ns_schedule_proc", NsTclSchedCmd, NULL},
    {"ns_schedule_daily", NsTclSchedDailyCmd, NULL},
    {"ns_schedule_weekly", NsTclSchedWeeklyCmd, NULL},
    {"ns_atsignal", NsTclAtSignalCmd, NULL},
    {"ns_atshutdown", NsTclAtShutdownCmd, NULL},
    {"ns_atexit", NsTclAtExitCmd, NULL},
    {"ns_after", NsTclAfterCmd, NULL},
    {"ns_cancel", NsTclCancelCmd, NULL},
    {"ns_pause", NsTclPauseCmd, NULL},
    {"ns_resume", NsTclResumeCmd, NULL},
    {"ns_unschedule_proc", NsTclUnscheduleCmd, NULL},

    /*
     * tclset.c
     */

    {"ns_set", NULL, NsTclSetObjCmd},
    {"ns_parseheader", NsTclParseHeaderCmd, NULL},

    /*
     * tclsock.c
     */

    {"ns_sockcallback", NULL, NsTclSockCallbackObjCmd},
    {"ns_socklistencallback", NULL, NsTclSockListenCallbackObjCmd},
    {"ns_sockblocking", NULL, NsTclSockSetBlockingObjCmd},
    {"ns_socknonblocking", NULL, NsTclSockSetNonBlockingObjCmd},
    {"ns_socknread", NULL, NsTclSockNReadObjCmd},
    {"ns_sockopen", NULL, NsTclSockOpenObjCmd},
    {"ns_socklisten", NULL, NsTclSockListenObjCmd},
    {"ns_sockaccept", NULL, NsTclSockAcceptObjCmd},
    {"ns_sockcheck", NULL, NsTclSockCheckObjCmd},
    {"ns_sockselect", NULL, NsTclSelectObjCmd},
    {"ns_socketpair", NULL, NsTclSocketPairObjCmd},
    {"ns_hostbyaddr", NULL, NsTclGetHostObjCmd},
    {"ns_addrbyhost", NULL, NsTclGetAddrObjCmd},

    /*
     * tclxkeylist.c
     */

    {"keyldel", Tcl_KeyldelCmd, NULL},
    {"keylget", Tcl_KeylgetCmd, NULL},
    {"keylkeys", Tcl_KeylkeysCmd, NULL},
    {"keylset", Tcl_KeylsetCmd, NULL},

    /*
     * cache.c
     */

    {"ns_cache_flush", NsTclCacheFlushCmd, NULL},
    {"ns_cache_stats", NsTclCacheStatsCmd, NULL},
    {"ns_cache_names", NsTclCacheNamesCmd, NULL},
    {"ns_cache_size", NsTclCacheSizeCmd, NULL},
    {"ns_cache_keys", NsTclCacheKeysCmd, NULL},

    /*
     * tclthread.c
     */

    {"ns_thread", NsTclThreadCmd, NULL},
    {"ns_mutex", NULL, NsTclMutexObjCmd},
    {"ns_cond", NULL, NsTclCondObjCmd},
    {"ns_event", NULL, NsTclCondObjCmd},
    {"ns_rwlock", NULL, NsTclRWLockObjCmd},
    {"ns_sema", NULL, NsTclSemaObjCmd},
    {"ns_critsec", NULL, NsTclCritSecObjCmd},

    /*
     * tclinit.c
     */

    {"ns_init", NULL, NsTclDummyObjCmd},
    {"ns_cleanup", NULL, NsTclDummyObjCmd},
    {"ns_markfordelete", NULL, NsTclMarkForDeleteObjCmd},

    /*
     * encoding.c
     */

    {"ns_charsets", NsTclCharsetsCmd, NULL},
    {"ns_encodingforcharset", NsTclEncodingForCharsetCmd, NULL},

    /*
     * Add more basic Tcl commands here.
     */

    {NULL, NULL}
};

/*
 * The following commands require the NsServer context and
 * are available only in virtual server interps.
 */

static Cmd servCmds[] = {

    /*
     * tclrequest.c
     */

    {"ns_register_filter", NULL, NsTclRegisterFilterObjCmd},
    {"ns_register_trace", NULL, NsTclRegisterTraceObjCmd},
    {"ns_register_adp", NULL, NsTclRegisterAdpObjCmd},
    {"ns_register_proc", NULL, NsTclRegisterProcObjCmd},
    {"ns_unregister_adp", NULL, NsTclUnRegisterObjCmd},
    {"ns_unregister_proc", NULL, NsTclUnRegisterObjCmd},
    {"ns_atclose", NsTclAtCloseCmd, NULL},

    /*
     * tclresp.c
     */

    {"ns_return", NULL, NsTclReturnObjCmd},
    {"ns_respond", NULL, NsTclRespondObjCmd},
    {"ns_returnfile", NULL, NsTclReturnFileObjCmd},
    {"ns_returnfp", NULL, NsTclReturnFpObjCmd},
    {"ns_returnbadrequest", NULL, NsTclReturnBadRequestObjCmd},
    {"ns_returnerror", NULL, NsTclReturnErrorObjCmd},
    {"ns_returnnotice", NsTclReturnNoticeCmd, NULL},
    {"ns_returnadminnotice", NsTclReturnAdminNoticeCmd, NULL},
    {"ns_returnredirect", NULL, NsTclReturnRedirectObjCmd},
    {"ns_headers", NULL, NsTclHeadersObjCmd},
    {"ns_write", NULL, NsTclWriteObjCmd},
    {"ns_connsendfp", NULL, NsTclConnSendFpObjCmd},
    {"ns_returnforbidden", NULL, NsTclReturnForbiddenObjCmd},
    {"ns_returnunauthorized", NULL, NsTclReturnUnauthorizedObjCmd},
    {"ns_returnnotfound", NULL, NsTclReturnNotFoundObjCmd},

    /*
     * tclfile.c
     */

    {"ns_chan", NULL, NsTclChanObjCmd},
    {"ns_url2file", NULL, NsTclUrl2FileObjCmd},


    {"ns_library", NsTclLibraryCmd, NULL},
    {"ns_geturl", NULL, NsTclGetUrlObjCmd},

    {"ns_checkurl", NULL, NsTclRequestAuthorizeObjCmd},
    {"ns_requestauthorize", NULL, NsTclRequestAuthorizeObjCmd},

    /*
     * tcladmin.c
     */

    {"ns_shutdown", NULL, NsTclShutdownObjCmd},

    /*
     * conn.c
     */

    {"ns_conncptofp", NULL, NsTclWriteContentObjCmd},
    {"ns_writecontent", NULL, NsTclWriteContentObjCmd},
    {"ns_conn", NULL, NsTclConnObjCmd},
    {"ns_startcontent", NULL, NsTclStartContentObjCmd},

    /*
     * adpparse.c
     */

    {"ns_register_adptag", NsTclRegisterTagCmd, NULL},
    {"ns_adp_registeradp", NsTclAdpRegisterAdpCmd, NULL},
    {"ns_adp_registertag", NsTclAdpRegisterAdpCmd, NULL},
    {"ns_adp_registerproc", NsTclAdpRegisterProcCmd, NULL},

    /*
     * adpcmds.c
     */

    {"ns_adp_stats", NsTclAdpStatsCmd, NULL},
    {"ns_adp_debug", NsTclAdpDebugCmd, NULL},

    {"_ns_adp_include", NULL, NsTclAdpIncludeObjCmd},
    {"ns_adp_eval", NULL, NsTclAdpEvalObjCmd},
    {"ns_adp_safeeval", NULL, NsTclAdpSafeEvalObjCmd},
    {"ns_adp_parse", NULL, NsTclAdpParseObjCmd},
    {"ns_puts", NULL, NsTclAdpPutsObjCmd},
    {"ns_adp_puts", NULL, NsTclAdpPutsObjCmd},
    {"ns_adp_append", NULL, NsTclAdpAppendObjCmd},
    {"ns_adp_dir", NULL, NsTclAdpDirObjCmd},
    {"ns_adp_return", NULL, NsTclAdpReturnObjCmd},
    {"ns_adp_break", NULL, NsTclAdpBreakObjCmd},
    {"ns_adp_abort", NULL, NsTclAdpAbortObjCmd},
    {"ns_adp_tell", NULL, NsTclAdpTellObjCmd},
    {"ns_adp_trunc", NULL, NsTclAdpTruncObjCmd},
    {"ns_adp_dump", NULL, NsTclAdpDumpObjCmd},
    {"ns_adp_argc", NULL, NsTclAdpArgcObjCmd},
    {"ns_adp_argv", NULL, NsTclAdpArgvObjCmd},
    {"ns_adp_bind_args", NULL, NsTclAdpBindArgsObjCmd},
    {"ns_adp_exception", NULL, NsTclAdpExceptionObjCmd},
    {"ns_adp_stream", NULL, NsTclAdpStreamObjCmd},
    {"ns_adp_mime", NULL, NsTclAdpMimeTypeObjCmd},
    {"ns_adp_mimetype", NULL, NsTclAdpMimeTypeObjCmd},

    /*
     * tclvar.c
     */

    {"ns_share", NsTclShareCmd, NULL},
    {"ns_var", NULL, NsTclVarObjCmd},
    {"nsv_get", NULL, NsTclNsvGetObjCmd},
    {"nsv_exists", NULL, NsTclNsvExistsObjCmd},
    {"nsv_set", NULL, NsTclNsvSetObjCmd},
    {"nsv_incr", NULL, NsTclNsvIncrObjCmd},
    {"nsv_append", NULL, NsTclNsvAppendObjCmd},
    {"nsv_lappend", NULL, NsTclNsvLappendObjCmd},
    {"nsv_array", NULL, NsTclNsvArrayObjCmd},
    {"nsv_unset", NULL, NsTclNsvUnsetObjCmd},
    {"nsv_names", NULL, NsTclNsvNamesObjCmd},

    /*
     * serv.c
     */

    {"ns_server", NULL, NsTclServerObjCmd},

    /*
     * tclinit.c
     */

    {"ns_ictl", NULL, NsTclICtlObjCmd},

    /*
     * Add more server Tcl commands here.
     */

    {NULL, NULL}
};


/*
 *----------------------------------------------------------------------
 *
 * NsTclAddCmds --
 *
 *	Create basic and server Tcl commands.
 *
 * Results:
 *	TCL_OK. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static void
AddCmds(Cmd *cmdPtr, ClientData arg, Tcl_Interp *interp)
{
    while (cmdPtr->name != NULL) {
	if (cmdPtr->objProc != NULL) {
	    Tcl_CreateObjCommand(interp, cmdPtr->name, cmdPtr->objProc, arg, NULL);
	} else {
	    Tcl_CreateCommand(interp, cmdPtr->name, cmdPtr->proc, arg, NULL);
	}
	++cmdPtr;
    }
}

void
NsTclAddCmds(Tcl_Interp *interp, NsInterp *itPtr)
{
    AddCmds(cmds, itPtr, interp);
}

void
NsTclAddServerCmds(Tcl_Interp *interp, NsInterp *itPtr)
{
    AddCmds(servCmds, itPtr, interp);
}
