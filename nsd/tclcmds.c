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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclcmds.c,v 1.30 2002/08/25 20:10:56 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

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
    NsTclInitObjCmd,
    NsTclJpegSizeObjCmd,
    NsTclKillObjCmd,
    //NsTclLibraryObjCmd,
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
    NsTclJobCmd,
    NsTclChanCmd,
    NsTclAfterCmd,
    NsTclAtCloseCmd,
    NsTclAtExitCmd,
    NsTclAtShutdownCmd,
    NsTclAtSignalCmd,
    NsTclSchedCmd,
    NsTclSchedDailyCmd,
    NsTclSchedWeeklyCmd,
    NsTclCancelCmd,
    NsTclPauseCmd,
    NsTclResumeCmd,
    NsTclUnscheduleCmd,
    NsTclChmodCmd,
    NsTclConfigCmd,
    NsTclConfigSectionCmd,
    NsTclConfigSectionsCmd,
    NsTclConnSendFpCmd,
    NsTclCpCmd,
    NsTclCpFpCmd,
    NsTclCritSecCmd,
    NsTclEvalCmd,
    NsTclEventCmd,
    NsTclFTruncateCmd,
    NsTclHeadersCmd,
    NsTclKillCmd,
    NsTclLinkCmd,
    NsTclLogRollCmd,
    NsTclMkTempCmd,
    NsTclMkdirCmd,
    NsTclMutexCmd,
    NsTclNormalizePathCmd,
    NsTclParseHeaderCmd,
    NsTclRWLockCmd,
    NsTclRandCmd,
    NsTclRegisterAdpCmd,
    NsTclRegisterProcCmd,
    NsTclRegisterFilterCmd,
    NsTclRegisterTraceCmd,
    NsTclRenameCmd,
    NsTclRespondCmd,
    NsTclReturnAdminNoticeCmd,
    NsTclReturnBadRequestCmd,
    NsTclReturnCmd,
    NsTclReturnErrorCmd,
    NsTclReturnFileCmd,
    NsTclReturnFpCmd,
    NsTclReturnNoticeCmd,
    NsTclReturnRedirectCmd,
    NsTclRmdirCmd,
    NsTclRollFileCmd,
    NsTclPurgeFilesCmd,
    NsTclSelectCmd,
    NsTclSemaCmd,
    NsTclSetCmd,
    NsTclReturnNotFoundCmd,
    NsTclReturnUnauthorizedCmd,
    NsTclReturnForbiddenCmd,
    NsTclGetHostCmd,
    NsTclGetAddrCmd,
    NsTclSockAcceptCmd,
    NsTclSockCallbackCmd,
    NsTclSockCheckCmd,
    NsTclSockListenCallbackCmd,
    NsTclSockListenCmd,
    NsTclSockNReadCmd,
    NsTclSockOpenCmd,
    NsTclSockSetBlockingCmd,
    NsTclSockSetNonBlockingCmd,
    NsTclSocketPairCmd,
    NsTclSymlinkCmd,
    NsTclThreadCmd,
    NsTclTmpNamCmd,
    NsTclTruncateCmd,
    NsTclUnRegisterCmd,
    NsTclUnlinkCmd,
    NsTclUrl2FileCmd,
    NsTclWriteCmd,
    NsTclWriteFpCmd,
    NsTclClientDebugCmd,
    NsTclParseQueryCmd,
    NsTclQueryResolveCmd,
    NsTclServerCmd,
    NsTclSetExpiresCmd,
    NsTclShutdownCmd,
    Tcl_KeyldelCmd,
    Tcl_KeylgetCmd,
    Tcl_KeylkeysCmd,
    Tcl_KeylsetCmd,
    NsTclConnCmd,
    NsTclCrashCmd,
    NsTclCryptCmd,
    NsTclGetUrlCmd,
    NsTclGifSizeCmd,
    NsTclGuessTypeCmd,
    NsTclHTUUDecodeCmd,
    NsTclHTUUEncodeCmd,
    NsTclHrefsCmd,
    NsTclHttpTimeCmd,
    NsTclInfoCmd,
    NsTclJpegSizeCmd,
    NsTclLibraryCmd,
    NsTclLocalTimeCmd,
    NsTclGmTimeCmd,
    NsTclMarkForDeleteCmd,
    NsTclModuleCmd,
    NsTclModulePathCmd,
    NsTclParseHttpTimeCmd,
    NsTclQuoteHtmlCmd,
    NsTclRequestAuthorizeCmd,
    NsTclShareCmd, 
    NsTclSleepCmd,
    NsTclStrftimeCmd,
    NsTclStripHtmlCmd,
    NsTclUrlDecodeCmd,
    NsTclUrlEncodeCmd,
    NsTclWriteContentCmd,
    NsTclCacheStatsCmd,
    NsTclCacheFlushCmd,
    NsTclCacheNamesCmd,
    NsTclCacheSizeCmd,
    NsTclCacheKeysCmd,
    NsTclDbCmd,
    NsTclDbConfigPathCmd,
    NsTclPoolDescriptionCmd,
    NsTclDbErrorCodeCmd,
    NsTclDbErrorMsgCmd,
    NsTclQuoteListToListCmd,
    NsTclGetCsvCmd,
    NsTclEnvCmd,
    NsTclAdpStatsCmd,
    NsTclAdpDebugCmd,
    NsTclAdpRegisterAdpCmd,
    NsTclAdpRegisterProcCmd,
    NsTclRegisterTagCmd,
    NsTclHttpCmd,
    NsTclShareCmd;

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
    {"ns_crypt", NsTclCryptCmd, NsTclCryptObjCmd},
    {"ns_sleep", NsTclSleepCmd, NsTclSleepObjCmd},
    {"ns_localtime", NsTclLocalTimeCmd, NsTclLocalTimeObjCmd},
    {"ns_gmtime", NsTclGmTimeCmd, NsTclGmTimeObjCmd},
    {"ns_time", NULL, NsTclTimeObjCmd},
    {"ns_fmttime", NsTclStrftimeCmd, NsTclStrftimeObjCmd},
    {"ns_httptime", NsTclHttpTimeCmd, NsTclHttpTimeObjCmd},
    {"ns_parsehttptime", NsTclParseHttpTimeCmd, NsTclParseHttpTimeObjCmd},


    {"ns_rand", NsTclRandCmd, NsTclRandObjCmd},

    {"ns_info", NsTclInfoCmd, NsTclInfoObjCmd},
    {"ns_modulepath", NsTclModulePathCmd, NsTclModulePathObjCmd},

    {"ns_log", NULL, NsTclLogObjCmd},
    {"ns_logctl", NULL, NsTclLogCtlObjCmd},

    {"ns_urlencode", NsTclUrlEncodeCmd, NsTclUrlEncodeObjCmd},
    {"ns_urldecode", NsTclUrlDecodeCmd, NsTclUrlDecodeObjCmd},
    {"ns_uuencode", NsTclHTUUEncodeCmd, NsTclHTUUEncodeObjCmd},
    {"ns_uudecode", NsTclHTUUDecodeCmd, NsTclHTUUDecodeObjCmd},
    {"ns_gifsize", NsTclGifSizeCmd, NsTclGifSizeObjCmd},
    {"ns_jpegsize", NsTclJpegSizeCmd, NsTclJpegSizeObjCmd},

    /*
     * tclfile.c
     */

    {"ns_unlink", NsTclUnlinkCmd, NsTclUnlinkObjCmd},
    {"ns_mkdir", NsTclMkdirCmd, NsTclMkdirObjCmd},
    {"ns_rmdir", NsTclRmdirCmd, NsTclRmdirObjCmd},
    {"ns_cp", NsTclCpCmd, NsTclCpObjCmd},
    {"ns_cpfp", NsTclCpFpCmd, NsTclCpFpObjCmd},
    {"ns_rollfile", NsTclRollFileCmd, NsTclRollFileObjCmd},
    {"ns_purgefiles", NsTclPurgeFilesCmd, NsTclPurgeFilesObjCmd},
    {"ns_mktemp", NsTclMkTempCmd, NULL},
    {"ns_tmpnam", NsTclTmpNamCmd, NsTclTmpNamObjCmd},
    {"ns_normalizepath", NsTclNormalizePathCmd, NsTclNormalizePathObjCmd},
    {"ns_link", NsTclLinkCmd, NsTclLinkObjCmd},
    {"ns_symlink", NsTclSymlinkCmd, NsTclSymlinkObjCmd},
    {"ns_rename", NsTclRenameCmd, NsTclRenameObjCmd},
    {"ns_kill", NsTclKillCmd, NsTclKillObjCmd},
    {"ns_writefp", NsTclWriteFpCmd, NsTclWriteFpObjCmd},
    {"ns_truncate", NsTclTruncateCmd, NsTclTruncateObjCmd},
    {"ns_ftruncate", NsTclFTruncateCmd, NsTclFTruncateObjCmd},
    {"ns_chmod", NsTclChmodCmd, NsTclChmodObjCmd},

    /*
     * tclenv.c
     */

    {"ns_env", NsTclEnvCmd, NULL},
    {"env", NsTclEnvCmd, NULL}, /* NB: Backwards compatible. */

    /*
     * tclsock.c
     */

    {"ns_sockblocking", NsTclSockSetBlockingCmd, NsTclSockSetBlockingObjCmd},
    {"ns_socknonblocking", NsTclSockSetNonBlockingCmd, NsTclSockSetNonBlockingObjCmd},
    {"ns_socknread", NsTclSockNReadCmd, NsTclSockNReadObjCmd},
    {"ns_sockopen", NsTclSockOpenCmd, NsTclSockOpenObjCmd},
    {"ns_socklisten", NsTclSockListenCmd, NsTclSockListenObjCmd},
    {"ns_sockaccept", NsTclSockAcceptCmd, NsTclSockAcceptObjCmd},
    {"ns_sockcheck", NsTclSockCheckCmd, NsTclSockCheckObjCmd},
    {"ns_sockselect", NsTclSelectCmd, NsTclSelectObjCmd},
    {"ns_socketpair", NsTclSocketPairCmd, NsTclSocketPairObjCmd},
    {"ns_hostbyaddr", NsTclGetHostCmd, NsTclGetHostObjCmd},
    {"ns_addrbyhost", NsTclGetAddrCmd, NsTclGetAddrObjCmd},

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
    {"ns_mutex", NsTclMutexCmd, NsTclMutexObjCmd},
    {"ns_cond", NsTclEventCmd, NsTclCondObjCmd},
    {"ns_event", NsTclEventCmd, NsTclCondObjCmd},
    {"ns_rwlock", NsTclRWLockCmd, NsTclRWLockObjCmd},
    {"ns_sema", NsTclSemaCmd, NsTclSemaObjCmd},
    {"ns_critsec", NsTclCritSecCmd, NsTclCritSecObjCmd},


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
     * tclsock.c
     */

    {"ns_sockcallback", NsTclSockCallbackCmd, NsTclSockCallbackObjCmd},
    {"ns_socklistencallback", NsTclSockListenCallbackCmd, NsTclSockListenCallbackObjCmd},

    /*
     * tclrequest.c
     */

    {"ns_register_filter", NsTclRegisterFilterCmd, NsTclRegisterFilterObjCmd},
    {"ns_register_trace", NsTclRegisterTraceCmd, NsTclRegisterTraceObjCmd},
    {"ns_register_adp", NsTclRegisterAdpCmd, NsTclRegisterAdpObjCmd},
    {"ns_register_proc", NsTclRegisterProcCmd, NsTclRegisterProcObjCmd},
    {"ns_unregister_adp", NsTclUnRegisterCmd, NsTclUnRegisterObjCmd},
    {"ns_unregister_proc", NsTclUnRegisterCmd, NsTclUnRegisterObjCmd},
    {"ns_atclose", NsTclAtCloseCmd, NULL},

    /*
     * tclresp.c
     */

    {"ns_return", NsTclReturnCmd, NsTclReturnObjCmd},
    {"ns_respond", NsTclRespondCmd, NsTclRespondObjCmd},
    {"ns_returnfile", NsTclReturnFileCmd, NsTclReturnFileObjCmd},
    {"ns_returnfp", NsTclReturnFpCmd, NsTclReturnFpObjCmd},
    {"ns_returnbadrequest", NsTclReturnBadRequestCmd, NsTclReturnBadRequestObjCmd},
    {"ns_returnerror", NsTclReturnErrorCmd, NsTclReturnErrorObjCmd},
    {"ns_returnnotice", NsTclReturnNoticeCmd, NULL},
    {"ns_returnadminnotice", NsTclReturnAdminNoticeCmd, NULL},
    {"ns_returnredirect", NsTclReturnRedirectCmd, NsTclReturnRedirectObjCmd},
    {"ns_headers", NsTclHeadersCmd, NsTclHeadersObjCmd},
    {"ns_write", NsTclWriteCmd, NsTclWriteObjCmd},
    {"ns_connsendfp", NsTclConnSendFpCmd, NsTclConnSendFpObjCmd},
    {"ns_returnforbidden", NsTclReturnForbiddenCmd, NsTclReturnForbiddenObjCmd},
    {"ns_returnunauthorized", NsTclReturnUnauthorizedCmd, NsTclReturnUnauthorizedObjCmd},
    {"ns_returnnotfound", NsTclReturnNotFoundCmd, NsTclReturnNotFoundObjCmd},

    /*
     * tcljob.c
     */

    {"ns_job", NsTclJobCmd, NULL},

    /*
     * tclhttp.c
     */

    {"ns_http", NsTclHttpCmd, NsTclHttpObjCmd},

    /*
     * tclfile.c
     */

    {"ns_chan", NsTclChanCmd, NsTclChanObjCmd},
    {"ns_url2file", NsTclUrl2FileCmd, NsTclUrl2FileObjCmd},

    /*
     * log.c
     */

    {"ns_logroll", NsTclLogRollCmd, NsTclLogRollObjCmd},

    {"ns_library", NsTclLibraryCmd, NULL},
    {"ns_guesstype", NsTclGuessTypeCmd, NsTclGuessTypeObjCmd},
    {"ns_geturl", NsTclGetUrlCmd, NsTclGetUrlObjCmd},

    {"ns_checkurl", NsTclRequestAuthorizeCmd, NsTclRequestAuthorizeObjCmd},
    {"ns_requestauthorize", NsTclRequestAuthorizeCmd, NsTclRequestAuthorizeObjCmd},
    {"ns_markfordelete", NsTclMarkForDeleteCmd, NsTclMarkForDeleteObjCmd},

    /*
     * tcladmin.c
     */

    {"ns_shutdown", NsTclShutdownCmd, NsTclShutdownObjCmd},

    /*
     * conn.c
     */

    {"ns_parsequery", NsTclParseQueryCmd, NsTclParseQueryObjCmd},
    {"ns_conncptofp", NsTclWriteContentCmd, NsTclWriteContentObjCmd},
    {"ns_writecontent", NsTclWriteContentCmd, NsTclWriteContentObjCmd},
    {"ns_conn", NsTclConnCmd, NsTclConnObjCmd},

    /*
     * adpparse.c
     */

    {"ns_register_adptag", NsTclRegisterTagCmd, NULL},
    {"ns_adp_registeradp", NsTclAdpRegisterAdpCmd, NULL},
    {"ns_adp_registertag", NsTclAdpRegisterAdpCmd, NULL},
    {"ns_adp_registerproc", NsTclAdpRegisterProcCmd, NULL},

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
     * tclconf.c
     */

    {"ns_config", NsTclConfigCmd, NULL},
    {"ns_configsection", NsTclConfigSectionCmd, NULL},
    {"ns_configsections", NsTclConfigSectionsCmd, NULL},

    {"ns_striphtml", NsTclStripHtmlCmd, NULL},
    {"ns_quotehtml", NsTclQuoteHtmlCmd, NULL},
    {"ns_hrefs", NsTclHrefsCmd, NULL},

    /*
     * tclset.c
     */

    {"ns_set", NsTclSetCmd, NsTclSetObjCmd},
    {"ns_parseheader", NsTclParseHeaderCmd, NULL},

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

    {"ns_server", NsTclServerCmd, NsTclServerObjCmd},
    {"ns_init", NULL, NsTclInitObjCmd},

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
	if (cmdPtr->objProc != NULL && (cmdPtr->proc == NULL || nsconf.tcl.objcmds)) {
	    Tcl_CreateObjCommand(interp, cmdPtr->name, cmdPtr->objProc, arg, NULL);
	} else {
	    Tcl_CreateCommand(interp, cmdPtr->name, cmdPtr->proc, arg, NULL);
	}
	++cmdPtr;
    }
}

void
NsTclAddCmds(NsInterp *itPtr, Tcl_Interp *interp)
{
    AddCmds(cmds, itPtr, interp);
    if (itPtr != NULL) {
	AddCmds(servCmds, itPtr, interp);
    }
}
