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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclcmds.c,v 1.44 2004/10/06 18:49:22 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

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
    NsTclDriverObjCmd,
    NsTclDummyObjCmd,
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
    NsTclNHttpObjCmd,
    NsTclHttpTimeObjCmd,
    NsTclICtlObjCmd,
    NsTclInfoObjCmd,
    NsTclJobObjCmd,
    NsTclJpegSizeObjCmd,
    NsTclKillObjCmd,
    NsTclLimitsObjCmd,
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
    NsTclPoolsObjCmd,
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
    NsTclSockQueWaitObjCmd,
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
    NsTclWriteObjCmd,
    TclX_KeyldelObjCmd,
    TclX_KeylgetObjCmd,
    TclX_KeylkeysObjCmd,
    TclX_KeylsetObjCmd;

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
    NsTclUnscheduleCmd;

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
    {"env", NsTclEnvCmd, NULL}, /* NB: Backwards compatible. */
    {"keyldel", NULL, TclX_KeyldelObjCmd},
    {"keylget", NULL, TclX_KeylgetObjCmd},
    {"keylkeys", NULL, TclX_KeylkeysObjCmd},
    {"keylset", NULL, TclX_KeylsetObjCmd},
    {"ns_addrbyhost", NULL, NsTclGetAddrObjCmd},
    {"ns_after", NsTclAfterCmd, NULL},
    {"ns_atexit", NsTclAtExitCmd, NULL},
    {"ns_atshutdown", NsTclAtShutdownCmd, NULL},
    {"ns_atsignal", NsTclAtSignalCmd, NULL},
    {"ns_cache_flush", NsTclCacheFlushCmd, NULL},
    {"ns_cache_keys", NsTclCacheKeysCmd, NULL},
    {"ns_cache_names", NsTclCacheNamesCmd, NULL},
    {"ns_cache_size", NsTclCacheSizeCmd, NULL},
    {"ns_cache_stats", NsTclCacheStatsCmd, NULL},
    {"ns_cancel", NsTclCancelCmd, NULL},
    {"ns_charsets", NsTclCharsetsCmd, NULL},
    {"ns_chmod", NULL, NsTclChmodObjCmd},
    {"ns_cleanup", NULL, NsTclDummyObjCmd},
    {"ns_cond", NULL, NsTclCondObjCmd},
    {"ns_config", NsTclConfigCmd, NULL},
    {"ns_configsection", NsTclConfigSectionCmd, NULL},
    {"ns_configsections", NsTclConfigSectionsCmd, NULL},
    {"ns_cp", NULL, NsTclCpObjCmd},
    {"ns_cpfp", NULL, NsTclCpFpObjCmd},
    {"ns_critsec", NULL, NsTclCritSecObjCmd},
    {"ns_crypt", NULL, NsTclCryptObjCmd},
    {"ns_driver", NULL, NsTclDriverObjCmd},
    {"ns_encodingforcharset", NsTclEncodingForCharsetCmd, NULL},
    {"ns_env", NsTclEnvCmd, NULL},
    {"ns_event", NULL, NsTclCondObjCmd},
    {"ns_fmttime", NULL, NsTclStrftimeObjCmd},
    {"ns_ftruncate", NULL, NsTclFTruncateObjCmd},
    {"ns_gifsize", NULL, NsTclGifSizeObjCmd},
    {"ns_gmtime", NULL, NsTclGmTimeObjCmd},
    {"ns_guesstype", NULL, NsTclGuessTypeObjCmd},
    {"ns_hostbyaddr", NULL, NsTclGetHostObjCmd},
    {"ns_hrefs", NsTclHrefsCmd, NULL},
    {"ns_http", NULL, NsTclNHttpObjCmd},
    {"ns_httptime", NULL, NsTclHttpTimeObjCmd},
    {"ns_info", NULL, NsTclInfoObjCmd},
    {"ns_init", NULL, NsTclDummyObjCmd},
    {"ns_job", NULL, NsTclJobObjCmd},
    {"ns_jpegsize", NULL, NsTclJpegSizeObjCmd},
    {"ns_kill", NULL, NsTclKillObjCmd},
    {"ns_limits", NULL, NsTclLimitsObjCmd},
    {"ns_link", NULL, NsTclLinkObjCmd},
    {"ns_localtime", NULL, NsTclLocalTimeObjCmd},
    {"ns_log", NULL, NsTclLogObjCmd},
    {"ns_logctl", NULL, NsTclLogCtlObjCmd},
    {"ns_logroll", NULL, NsTclLogRollObjCmd},
    {"ns_markfordelete", NULL, NsTclMarkForDeleteObjCmd},
    {"ns_mkdir", NULL, NsTclMkdirObjCmd},
    {"ns_mktemp", NsTclMkTempCmd, NULL},
    {"ns_modulepath", NULL, NsTclModulePathObjCmd},
    {"ns_mutex", NULL, NsTclMutexObjCmd},
    {"ns_normalizepath", NULL, NsTclNormalizePathObjCmd},
    {"ns_parseheader", NsTclParseHeaderCmd, NULL},
    {"ns_parsehttptime", NULL, NsTclParseHttpTimeObjCmd},
    {"ns_parsequery", NULL, NsTclParseQueryObjCmd},
    {"ns_pause", NsTclPauseCmd, NULL},
    {"ns_pools", NULL, NsTclPoolsObjCmd},
    {"ns_purgefiles", NULL, NsTclPurgeFilesObjCmd},
    {"ns_quotehtml", NsTclQuoteHtmlCmd, NULL},
    {"ns_rand", NULL, NsTclRandObjCmd},
    {"ns_rename", NULL, NsTclRenameObjCmd},
    {"ns_resume", NsTclResumeCmd, NULL},
    {"ns_rmdir", NULL, NsTclRmdirObjCmd},
    {"ns_rollfile", NULL, NsTclRollFileObjCmd},
    {"ns_rwlock", NULL, NsTclRWLockObjCmd},
    {"ns_schedule_daily", NsTclSchedDailyCmd, NULL},
    {"ns_schedule_proc", NsTclSchedCmd, NULL},
    {"ns_schedule_weekly", NsTclSchedWeeklyCmd, NULL},
    {"ns_sema", NULL, NsTclSemaObjCmd},
    {"ns_set", NULL, NsTclSetObjCmd},
    {"ns_sleep", NULL, NsTclSleepObjCmd},
    {"ns_sockaccept", NULL, NsTclSockAcceptObjCmd},
    {"ns_sockblocking", NULL, NsTclSockSetBlockingObjCmd},
    {"ns_sockcallback", NULL, NsTclSockCallbackObjCmd},
    {"ns_sockcheck", NULL, NsTclSockCheckObjCmd},
    {"ns_socketpair", NULL, NsTclSocketPairObjCmd},
    {"ns_socklisten", NULL, NsTclSockListenObjCmd},
    {"ns_socklistencallback", NULL, NsTclSockListenCallbackObjCmd},
    {"ns_socknonblocking", NULL, NsTclSockSetNonBlockingObjCmd},
    {"ns_socknread", NULL, NsTclSockNReadObjCmd},
    {"ns_sockopen", NULL, NsTclSockOpenObjCmd},
    {"ns_sockselect", NULL, NsTclSelectObjCmd},
    {"ns_striphtml", NsTclStripHtmlCmd, NULL},
    {"ns_symlink", NULL, NsTclSymlinkObjCmd},
    {"ns_thread", NsTclThreadCmd, NULL},
    {"ns_time", NULL, NsTclTimeObjCmd},
    {"ns_tmpnam", NULL, NsTclTmpNamObjCmd},
    {"ns_truncate", NULL, NsTclTruncateObjCmd},
    {"ns_unlink", NULL, NsTclUnlinkObjCmd},
    {"ns_unschedule_proc", NsTclUnscheduleCmd, NULL},
    {"ns_urldecode", NULL, NsTclUrlDecodeObjCmd},
    {"ns_urlencode", NULL, NsTclUrlEncodeObjCmd},
    {"ns_uudecode", NULL, NsTclHTUUDecodeObjCmd},
    {"ns_uuencode", NULL, NsTclHTUUEncodeObjCmd},
    {"ns_writefp", NULL, NsTclWriteFpObjCmd},

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
    {"_ns_adp_include", NULL, NsTclAdpIncludeObjCmd},
    {"ns_adp_abort", NULL, NsTclAdpAbortObjCmd},
    {"ns_adp_append", NULL, NsTclAdpAppendObjCmd},
    {"ns_adp_argc", NULL, NsTclAdpArgcObjCmd},
    {"ns_adp_argv", NULL, NsTclAdpArgvObjCmd},
    {"ns_adp_bind_args", NULL, NsTclAdpBindArgsObjCmd},
    {"ns_adp_break", NULL, NsTclAdpBreakObjCmd},
    {"ns_adp_debug", NsTclAdpDebugCmd, NULL},
    {"ns_adp_dir", NULL, NsTclAdpDirObjCmd},
    {"ns_adp_dump", NULL, NsTclAdpDumpObjCmd},
    {"ns_adp_eval", NULL, NsTclAdpEvalObjCmd},
    {"ns_adp_exception", NULL, NsTclAdpExceptionObjCmd},
    {"ns_adp_mime", NULL, NsTclAdpMimeTypeObjCmd},
    {"ns_adp_mimetype", NULL, NsTclAdpMimeTypeObjCmd},
    {"ns_adp_parse", NULL, NsTclAdpParseObjCmd},
    {"ns_adp_puts", NULL, NsTclAdpPutsObjCmd},
    {"ns_adp_registeradp", NsTclAdpRegisterAdpCmd, NULL},
    {"ns_adp_registerproc", NsTclAdpRegisterProcCmd, NULL},
    {"ns_adp_registertag", NsTclAdpRegisterAdpCmd, NULL},
    {"ns_adp_return", NULL, NsTclAdpReturnObjCmd},
    {"ns_adp_safeeval", NULL, NsTclAdpSafeEvalObjCmd},
    {"ns_adp_stats", NsTclAdpStatsCmd, NULL},
    {"ns_adp_stream", NULL, NsTclAdpStreamObjCmd},
    {"ns_adp_tell", NULL, NsTclAdpTellObjCmd},
    {"ns_adp_trunc", NULL, NsTclAdpTruncObjCmd},
    {"ns_atclose", NsTclAtCloseCmd, NULL},
    {"ns_chan", NULL, NsTclChanObjCmd},
    {"ns_checkurl", NULL, NsTclRequestAuthorizeObjCmd},
    {"ns_conn", NULL, NsTclConnObjCmd},
    {"ns_conncptofp", NULL, NsTclWriteContentObjCmd},
    {"ns_connsendfp", NULL, NsTclConnSendFpObjCmd},
    {"ns_geturl", NULL, NsTclGetUrlObjCmd},
    {"ns_headers", NULL, NsTclHeadersObjCmd},
    {"ns_ictl", NULL, NsTclICtlObjCmd},
    {"ns_library", NsTclLibraryCmd, NULL},
    {"ns_puts", NULL, NsTclAdpPutsObjCmd},
    {"ns_register_adp", NULL, NsTclRegisterAdpObjCmd},
    {"ns_register_adptag", NsTclRegisterTagCmd, NULL},
    {"ns_register_filter", NULL, NsTclRegisterFilterObjCmd},
    {"ns_register_proc", NULL, NsTclRegisterProcObjCmd},
    {"ns_register_trace", NULL, NsTclRegisterTraceObjCmd},
    {"ns_requestauthorize", NULL, NsTclRequestAuthorizeObjCmd},
    {"ns_respond", NULL, NsTclRespondObjCmd},
    {"ns_return", NULL, NsTclReturnObjCmd},
    {"ns_returnadminnotice", NsTclReturnAdminNoticeCmd, NULL},
    {"ns_returnbadrequest", NULL, NsTclReturnBadRequestObjCmd},
    {"ns_returnerror", NULL, NsTclReturnErrorObjCmd},
    {"ns_returnfile", NULL, NsTclReturnFileObjCmd},
    {"ns_returnforbidden", NULL, NsTclReturnForbiddenObjCmd},
    {"ns_returnfp", NULL, NsTclReturnFpObjCmd},
    {"ns_returnnotfound", NULL, NsTclReturnNotFoundObjCmd},
    {"ns_returnnotice", NsTclReturnNoticeCmd, NULL},
    {"ns_returnredirect", NULL, NsTclReturnRedirectObjCmd},
    {"ns_returnunauthorized", NULL, NsTclReturnUnauthorizedObjCmd},
    {"ns_server", NULL, NsTclServerObjCmd},
    {"ns_share", NsTclShareCmd, NULL},
    {"ns_shutdown", NULL, NsTclShutdownObjCmd},
    {"ns_sockquewait", NULL, NsTclSockQueWaitObjCmd},
    {"ns_startcontent", NULL, NsTclStartContentObjCmd},
    {"ns_unregister_adp", NULL, NsTclUnRegisterObjCmd},
    {"ns_unregister_proc", NULL, NsTclUnRegisterObjCmd},
    {"ns_url2file", NULL, NsTclUrl2FileObjCmd},
    {"ns_var", NULL, NsTclVarObjCmd},
    {"ns_write", NULL, NsTclWriteObjCmd},
    {"ns_writecontent", NULL, NsTclWriteContentObjCmd},
    {"nsv_append", NULL, NsTclNsvAppendObjCmd},
    {"nsv_array", NULL, NsTclNsvArrayObjCmd},
    {"nsv_exists", NULL, NsTclNsvExistsObjCmd},
    {"nsv_get", NULL, NsTclNsvGetObjCmd},
    {"nsv_incr", NULL, NsTclNsvIncrObjCmd},
    {"nsv_lappend", NULL, NsTclNsvLappendObjCmd},
    {"nsv_names", NULL, NsTclNsvNamesObjCmd},
    {"nsv_set", NULL, NsTclNsvSetObjCmd},
    {"nsv_unset", NULL, NsTclNsvUnsetObjCmd},

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
