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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclcmds.c,v 1.18 2001/05/18 12:29:53 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Tcl object and string commands.
 */

extern Tcl_ObjCmdProc
    NsTclTimeObjCmd,
    NsTclAdpAppendObjCmd,
    NsTclAdpPutsObjCmd;

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
    NsTclLogCmd,
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
    NsTclVarCmd,
    NsTclWriteContentCmd,
    NsTclRegisterTagCmd,
    NsTclRegisterAdpTagCmd,
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
    NsTclAdpEvalCmd,
    NsTclAdpIncludeCmd,
    NsTclAdpDirCmd,
    NsTclAdpReturnCmd,
    NsTclAdpBreakCmd,
    NsTclAdpAbortCmd,
    NsTclAdpTellCmd,
    NsTclAdpTruncCmd,
    NsTclAdpDumpCmd,
    NsTclAdpArgcCmd,
    NsTclAdpArgvCmd,
    NsTclAdpBindArgsCmd,
    NsTclAdpExceptionCmd,
    NsTclAdpStreamCmd,
    NsTclAdpDebugCmd,
    NsTclAdpParseCmd,
    NsTclAdpMimeTypeCmd,
    NsTclNsvGetCmd,
    NsTclNsvExistsCmd,
    NsTclNsvSetCmd,
    NsTclNsvIncrCmd,
    NsTclNsvAppendCmd,
    NsTclNsvLappendCmd,
    NsTclNsvArrayCmd,
    NsTclNsvUnsetCmd,
    NsTclNsvNamesCmd,
    NsTclVarCmd,
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
    {"ns_crypt", NsTclCryptCmd, NULL},
    {"ns_sleep", NsTclSleepCmd, NULL},
    {"ns_localtime", NsTclLocalTimeCmd, NULL},
    {"ns_gmtime", NsTclGmTimeCmd, NULL},
    {"ns_time", NULL, NsTclTimeObjCmd},
    {"ns_fmttime", NsTclStrftimeCmd, NULL},
    {"ns_httptime", NsTclHttpTimeCmd, NULL},
    {"ns_parsehttptime", NsTclParseHttpTimeCmd, NULL},


    {"ns_rand", NsTclRandCmd, NULL},

    {"ns_info", NsTclInfoCmd, NULL},
    {"ns_modulepath", NsTclModulePathCmd, NULL},

    {"ns_log", NsTclLogCmd, NULL},

    {"ns_urlencode", NsTclUrlEncodeCmd, NULL},
    {"ns_urldecode", NsTclUrlDecodeCmd, NULL},
    {"ns_uuencode", NsTclHTUUEncodeCmd, NULL},
    {"ns_uudecode", NsTclHTUUDecodeCmd, NULL},
    {"ns_gifsize", NsTclGifSizeCmd, NULL},
    {"ns_jpegsize", NsTclJpegSizeCmd, NULL},

    /*
     * tclfile.c
     */

    {"ns_unlink", NsTclUnlinkCmd, NULL},
    {"ns_mkdir", NsTclMkdirCmd, NULL},
    {"ns_rmdir", NsTclRmdirCmd, NULL},
    {"ns_cp", NsTclCpCmd, NULL},
    {"ns_cpfp", NsTclCpFpCmd, NULL},
    {"ns_rollfile", NsTclRollFileCmd, NULL},
    {"ns_purgefiles", NsTclPurgeFilesCmd, NULL},
    {"ns_mktemp", NsTclMkTempCmd, NULL},
    {"ns_tmpnam", NsTclTmpNamCmd, NULL},
    {"ns_normalizepath", NsTclNormalizePathCmd, NULL},
    {"ns_link", NsTclLinkCmd, NULL},
    {"ns_symlink", NsTclSymlinkCmd, NULL},
    {"ns_rename", NsTclRenameCmd, NULL},
    {"ns_kill", NsTclKillCmd, NULL},
    {"ns_writefp", NsTclWriteFpCmd, NULL},
    {"ns_truncate", NsTclTruncateCmd, NULL},
    {"ns_ftruncate", NsTclFTruncateCmd, NULL},
    {"ns_chmod", NsTclChmodCmd, NULL},

    /*
     * tclenv.c
     */

    {"ns_env", NsTclEnvCmd, NULL},
    {"env", NsTclEnvCmd, NULL}, /* NB: Backwards compatible. */

    /*
     * tclsock.c
     */

    {"ns_sockblocking", NsTclSockSetBlockingCmd, NULL},
    {"ns_socknonblocking", NsTclSockSetNonBlockingCmd, NULL},
    {"ns_socknread", NsTclSockNReadCmd, NULL},
    {"ns_sockopen", NsTclSockOpenCmd, NULL},
    {"ns_socklisten", NsTclSockListenCmd, NULL},
    {"ns_sockaccept", NsTclSockAcceptCmd, NULL},
    {"ns_sockcheck", NsTclSockCheckCmd, NULL},
    {"ns_sockselect", NsTclSelectCmd, NULL},
    {"ns_socketpair", NsTclSocketPairCmd, NULL},
    {"ns_hostbyaddr", NsTclGetHostCmd, NULL},
    {"ns_addrbyhost", NsTclGetAddrCmd, NULL},

    /*
     * tclxkeylist.c
     */

    {"keyldel", Tcl_KeyldelCmd, NULL},
    {"keylget", Tcl_KeylgetCmd, NULL},
    {"keylkeys", Tcl_KeylkeysCmd, NULL},
    {"keylset", Tcl_KeylsetCmd, NULL},

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

    {"ns_sockcallback", NsTclSockCallbackCmd, NULL},
    {"ns_socklistencallback", NsTclSockListenCallbackCmd, NULL},

    /*
     * tclrequest.c
     */

    {"ns_register_filter", NsTclRegisterFilterCmd, NULL},
    {"ns_register_trace", NsTclRegisterTraceCmd, NULL},
    {"ns_register_adp", NsTclRegisterAdpCmd, NULL},
    {"ns_register_proc", NsTclRegisterProcCmd, NULL},
    {"ns_unregister_adp", NsTclUnRegisterCmd, NULL},
    {"ns_unregister_proc", NsTclUnRegisterCmd, NULL},
    {"ns_atclose", NsTclAtCloseCmd, NULL},

    /*
     * tclresp.c
     */

    {"ns_return", NsTclReturnCmd, NULL},
    {"ns_respond", NsTclRespondCmd, NULL},
    {"ns_returnfile", NsTclReturnFileCmd, NULL},
    {"ns_returnfp", NsTclReturnFpCmd, NULL},
    {"ns_returnbadrequest", NsTclReturnBadRequestCmd, NULL},
    {"ns_returnerror", NsTclReturnErrorCmd, NULL},
    {"ns_returnnotice", NsTclReturnNoticeCmd, NULL},
    {"ns_returnadminnotice", NsTclReturnAdminNoticeCmd, NULL},
    {"ns_returnredirect", NsTclReturnRedirectCmd, NULL},
    {"ns_headers", NsTclHeadersCmd, NULL},
    {"ns_write", NsTclWriteCmd, NULL},
    {"ns_connsendfp", NsTclConnSendFpCmd, NULL},
    {"ns_returnforbidden", NsTclReturnForbiddenCmd, NULL},
    {"ns_returnunauthorized", NsTclReturnUnauthorizedCmd, NULL},
    {"ns_returnnotfound", NsTclReturnNotFoundCmd, NULL},

    /*
     * tcljob.c
     */

    {"ns_job", NsTclJobCmd, NULL},

    /*
     * tclhttp.c
     */

    {"ns_http", NsTclHttpCmd, NULL},

    /*
     * tclfile.c
     */

    {"ns_chan", NsTclChanCmd, NULL},
    {"ns_url2file", NsTclUrl2FileCmd, NULL},

    /*
     * log.c
     */

    {"ns_logroll", NsTclLogRollCmd, NULL},

    {"ns_library", NsTclLibraryCmd, NULL},
    {"ns_guesstype", NsTclGuessTypeCmd, NULL},
    {"ns_geturl", NsTclGetUrlCmd, NULL},

    {"ns_checkurl", NsTclRequestAuthorizeCmd, NULL},
    {"ns_requestauthorize", NsTclRequestAuthorizeCmd, NULL},
    {"ns_markfordelete", NsTclMarkForDeleteCmd, NULL},

    /*
     * tcladmin.c
     */

    {"ns_shutdown", NsTclShutdownCmd, NULL},

    /*
     * conn.c
     */

    {"ns_parsequery", NsTclParseQueryCmd, NULL},
    {"ns_conncptofp", NsTclWriteContentCmd, NULL},
    {"ns_writecontent", NsTclWriteContentCmd, NULL},
    {"ns_conn", NsTclConnCmd, NULL},

    /*
     * adpparse.c
     */

    {"ns_register_adptag", NsTclRegisterTagCmd, NULL},
    {"ns_adp_registeradp", NsTclRegisterAdpTagCmd, NULL},
    {"ns_adp_registertag", NsTclRegisterAdpTagCmd, NULL},

    /*
     * dbtcl.c
     */

    {"ns_db", NsTclDbCmd, NULL},
    {"ns_quotelisttolist", NsTclQuoteListToListCmd, NULL},
    {"ns_getcsv", NsTclGetCsvCmd, NULL},
    {"ns_dberrorcode", NsTclDbErrorCodeCmd, NULL},
    {"ns_dberrormsg", NsTclDbErrorMsgCmd, NULL},
    {"ns_getcsv", NsTclGetCsvCmd, NULL},
    {"ns_dbconfigpath", NsTclDbConfigPathCmd, NULL},
    {"ns_pooldescription", NsTclPoolDescriptionCmd, NULL},

    /*
     * tclthread.c
     */

    {"ns_thread", NsTclThreadCmd, NULL},
    {"ns_mutex", NsTclMutexCmd, NULL},
    {"ns_cond", NsTclEventCmd, NULL},
    {"ns_event", NsTclEventCmd, NULL},
    {"ns_rwlock", NsTclRWLockCmd, NULL},
    {"ns_sema", NsTclSemaCmd, NULL},
    {"ns_critsec", NsTclCritSecCmd, NULL},

    /*
     * cache.c
     */

    {"ns_cache_flush", NsTclCacheFlushCmd, NULL},
    {"ns_cache_stats", NsTclCacheStatsCmd, NULL},
    {"ns_cache_names", NsTclCacheNamesCmd, NULL},
    {"ns_cache_size", NsTclCacheSizeCmd, NULL},
    {"ns_cache_keys", NsTclCacheKeysCmd, NULL},

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

    {"ns_set", NsTclSetCmd, NULL},
    {"ns_parseheader", NsTclParseHeaderCmd, NULL},

    /*
     * adpcmds.c
     */

    {"_ns_adp_include", NsTclAdpIncludeCmd, NULL},
    {"ns_adp_eval", NsTclAdpEvalCmd, NULL},
    {"ns_adp_parse", NsTclAdpParseCmd, NULL},
    {"ns_puts", NULL, NsTclAdpPutsObjCmd},
    {"ns_adp_puts", NULL, NsTclAdpPutsObjCmd},
    {"ns_adp_append", NULL, NsTclAdpAppendObjCmd},
    {"ns_adp_dir", NsTclAdpDirCmd, NULL},
    {"ns_adp_return", NsTclAdpReturnCmd, NULL},
    {"ns_adp_break", NsTclAdpBreakCmd, NULL},
    {"ns_adp_abort", NsTclAdpAbortCmd, NULL},
    {"ns_adp_tell", NsTclAdpTellCmd, NULL},
    {"ns_adp_trunc", NsTclAdpTruncCmd, NULL},
    {"ns_adp_dump", NsTclAdpDumpCmd, NULL},
    {"ns_adp_argc", NsTclAdpArgcCmd, NULL},
    {"ns_adp_argv", NsTclAdpArgvCmd, NULL},
    {"ns_adp_bind_args", NsTclAdpBindArgsCmd, NULL},
    {"ns_adp_exception", NsTclAdpExceptionCmd, NULL},
    {"ns_adp_stream", NsTclAdpStreamCmd, NULL},
    {"ns_adp_debug", NsTclAdpDebugCmd, NULL},
    {"ns_adp_mime", NsTclAdpMimeTypeCmd, NULL},
    {"ns_adp_mimetype", NsTclAdpMimeTypeCmd, NULL},

    /*
     * tclvar.c
     */

    {"ns_share", NsTclShareCmd, NULL},
    {"ns_var", NsTclVarCmd, NULL},
    {"nsv_get", NsTclNsvGetCmd, NULL},
    {"nsv_exists", NsTclNsvExistsCmd, NULL},
    {"nsv_set", NsTclNsvSetCmd, NULL},
    {"nsv_incr", NsTclNsvIncrCmd, NULL},
    {"nsv_append", NsTclNsvAppendCmd, NULL},
    {"nsv_lappend", NsTclNsvLappendCmd, NULL},
    {"nsv_array", NsTclNsvArrayCmd, NULL},
    {"nsv_unset", NsTclNsvUnsetCmd, NULL},
    {"nsv_names", NsTclNsvNamesCmd, NULL},

    /*
     * serv.c
     */

    {"ns_server", NsTclServerCmd, NULL},

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
NsTclAddCmds(NsInterp *itPtr, Tcl_Interp *interp)
{
    AddCmds(cmds, itPtr, interp);
    if (itPtr != NULL) {
	AddCmds(servCmds, itPtr, interp);
    }
}
