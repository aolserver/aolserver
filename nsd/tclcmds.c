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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclcmds.c,v 1.10 2001/03/13 22:28:49 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static struct {
    char *name;
    Tcl_CmdProc *proc;
} cmds[] = {

    {"ns_crypt", NsTclCryptCmd},

    {"ns_sleep", NsTclSleepCmd},

    {"ns_localtime", NsTclLocalTimeCmd},
    {"ns_gmtime", NsTclGmTimeCmd},
    {"ns_time", NsTclTimeCmd},
    {"ns_fmttime", NsTclStrftimeCmd},
    {"ns_httptime", NsTclHttpTimeCmd},
    {"ns_parsehttptime", NsTclParseHttpTimeCmd},


    {"ns_rand", NsTclRandCmd},

    {"ns_info", NsTclInfoCmd},
    {"ns_modulepath", NsTclModulePathCmd},

    {"ns_log", NsTclLogCmd},

    {"ns_urlencode", NsTclUrlEncodeCmd},
    {"ns_urldecode", NsTclUrlDecodeCmd},
    {"ns_uuencode", NsTclHTUUEncodeCmd},
    {"ns_uudecode", NsTclHTUUDecodeCmd},
    {"ns_gifsize", NsTclGifSizeCmd},
    {"ns_jpegsize", NsTclJpegSizeCmd},

    /*
     * tclfile.c
     */

    {"ns_unlink", NsTclUnlinkCmd},
    {"ns_mkdir", NsTclMkdirCmd},
    {"ns_rmdir", NsTclRmdirCmd},
    {"ns_cp", NsTclCpCmd},
    {"ns_cpfp", NsTclCpFpCmd},
    {"ns_rollfile", NsTclRollFileCmd},
    {"ns_purgefiles", NsTclPurgeFilesCmd},
    {"ns_mktemp", NsTclMkTempCmd},
    {"ns_tmpnam", NsTclTmpNamCmd},
    {"ns_normalizepath", NsTclNormalizePathCmd},
    {"ns_link", NsTclLinkCmd},
    {"ns_symlink", NsTclSymlinkCmd},
    {"ns_rename", NsTclRenameCmd},
    {"ns_kill", NsTclKillCmd},
    {"ns_writefp", NsTclWriteFpCmd},
    {"ns_truncate", NsTclTruncateCmd},
    {"ns_ftruncate", NsTclFTruncateCmd},
    {"ns_chmod", NsTclChmodCmd},

    /*
     * tclenv.c
     */

    {"ns_env", NsTclEnvCmd},
    {"env", NsTclEnvCmd}, /* NB: Backwards compatible. */

    /*
     * tclsock.c
     */

    {"ns_sockblocking", NsTclSockSetBlockingCmd},
    {"ns_socknonblocking", NsTclSockSetNonBlockingCmd},
    {"ns_socknread", NsTclSockNReadCmd},
    {"ns_sockopen", NsTclSockOpenCmd},
    {"ns_socklisten", NsTclSockListenCmd},
    {"ns_sockaccept", NsTclSockAcceptCmd},
    {"ns_sockcheck", NsTclSockCheckCmd},
    {"ns_sockselect", NsTclSelectCmd},
    {"ns_socketpair", NsTclSocketPairCmd},
    {"ns_hostbyaddr", NsTclGetHostCmd},
    {"ns_addrbyhost", NsTclGetAddrCmd},

    /*
     * tclxkeylist.c
     */

    {"keyldel", Tcl_KeyldelCmd},
    {"keylget", Tcl_KeylgetCmd},
    {"keylkeys", Tcl_KeylkeysCmd},
    {"keylset", Tcl_KeylsetCmd},

    /*
     * Add more basic Tcl commands here.
     */

    {NULL, NULL}
};

static struct {
    char *name;
    Tcl_CmdProc *proc;
} servcmds[] = {

    /*
     * tclsock.c
     */

    {"ns_sockcallback", NsTclSockCallbackCmd},
    {"ns_socklistencallback", NsTclSockListenCallbackCmd},

    /*
     * tclop.c
     */

    {"ns_register_filter", NsTclRegisterFilterCmd},
    {"ns_register_trace", NsTclRegisterTraceCmd},
    {"ns_register_proc", NsTclRegisterCmd},
    {"ns_unregister_proc", NsTclUnRegisterCmd},
    {"ns_atclose", NsTclAtCloseCmd},

    /*
     * tclresp.c
     */

    {"ns_return", NsTclReturnCmd},
    {"ns_respond", NsTclRespondCmd},
    {"ns_returnfile", NsTclReturnFileCmd},
    {"ns_returnfp", NsTclReturnFpCmd},
    {"ns_returnbadrequest", NsTclReturnBadRequestCmd},
    {"ns_returnerror", NsTclReturnErrorCmd},
    {"ns_returnnotice", NsTclReturnNoticeCmd},
    {"ns_returnadminnotice", NsTclReturnAdminNoticeCmd},
    {"ns_returnredirect", NsTclReturnRedirectCmd},
    {"ns_headers", NsTclHeadersCmd},
    {"ns_write", NsTclWriteCmd},
    {"ns_connsendfp", NsTclConnSendFpCmd},
    {"ns_returnforbidden", NsTclReturnForbiddenCmd},
    {"ns_returnunauthorized", NsTclReturnUnauthorizedCmd},
    {"ns_returnnotfound", NsTclReturnNotFoundCmd},

    /*
     * tcljob.c
     */

    {"ns_job", NsTclJobCmd},

    /*
     * tclfile.c
     */

    {"ns_detach", NsTclDetachCmd},
    {"ns_attach", NsTclAttachCmd},
    {"ns_url2file", NsTclUrl2FileCmd},

    /*
     * log.c
     */

    {"ns_logroll", NsTclLogRollCmd},

    {"ns_library", NsTclLibraryCmd},
    {"ns_guesstype", NsTclGuessTypeCmd},
    {"ns_geturl", NsTclGetUrlCmd},

    {"ns_checkurl", NsTclRequestAuthorizeCmd},
    {"ns_requestauthorize", NsTclRequestAuthorizeCmd},
    {"ns_get_multipart_formdata", NsTclGetMultipartFormdataCmd},
    {"ns_markfordelete", NsTclMarkForDeleteCmd},

    /*
     * tcladmin.c
     */

    {"ns_shutdown", NsTclShutdownCmd},

    /*
     * conn.c
     */

    {"ns_parsequery", NsTclParseQueryCmd},
    {"ns_conncptofp", NsTclWriteContentCmd},
    {"ns_writecontent", NsTclWriteContentCmd},
    {"ns_conn", NsTclConnCmd},

    /*
     * adpfancy.c
     */

    {"ns_register_adptag", NsTclRegisterTagCmd},
    {"ns_adp_registeradp", NsTclRegisterAdpCmd},
    {"ns_adp_registertag", NsTclRegisterAdpCmd},

    /*
     * dbtcl.c
     */

    {"ns_db", NsTclDbCmd},
    {"ns_quotelisttolist", NsTclQuoteListToListCmd},
    {"ns_getcsv", NsTclGetCsvCmd},
    {"ns_dberrorcode", NsTclDbErrorCodeCmd},
    {"ns_dberrormsg", NsTclDbErrorMsgCmd},
    {"ns_getcsv", NsTclGetCsvCmd},
    {"ns_dbconfigpath", NsTclDbConfigPathCmd},
    {"ns_pooldescription", NsTclPoolDescriptionCmd},

    /*
     * tclthread.c
     */

    {"ns_thread", NsTclThreadCmd},
    {"ns_mutex", NsTclMutexCmd},
    {"ns_cond", NsTclEventCmd},
    {"ns_event", NsTclEventCmd},
    {"ns_rwlock", NsTclRWLockCmd},
    {"ns_sema", NsTclSemaCmd},
    {"ns_critsec", NsTclCritSecCmd},

    /*
     * cache.c
     */

    {"ns_cache_flush", NsTclCacheFlushCmd},
    {"ns_cache_stats", NsTclCacheStatsCmd},
    {"ns_cache_names", NsTclCacheNamesCmd},
    {"ns_cache_size", NsTclCacheSizeCmd},
    {"ns_cache_keys", NsTclCacheKeysCmd},

    /*
     * tclsched.c
     */

    {"ns_schedule_proc", NsTclSchedCmd},
    {"ns_schedule_daily", NsTclSchedDailyCmd},
    {"ns_schedule_weekly", NsTclSchedWeeklyCmd},
    {"ns_atsignal", NsTclAtSignalCmd},
    {"ns_atshutdown", NsTclAtShutdownCmd},
    {"ns_atexit", NsTclAtExitCmd},
    {"ns_after", NsTclAfterCmd},
    {"ns_cancel", NsTclCancelCmd},
    {"ns_pause", NsTclPauseCmd},
    {"ns_resume", NsTclResumeCmd},
    {"ns_unschedule_proc", NsTclUnscheduleCmd},

    /*
     * tclconf.c
     */

    {"ns_config", NsTclConfigCmd},
    {"ns_configsection", NsTclConfigSectionCmd},
    {"ns_configsections", NsTclConfigSectionsCmd},

    {"ns_striphtml", NsTclStripHtmlCmd},
    {"ns_quotehtml", NsTclQuoteHtmlCmd},
    {"ns_hrefs", NsTclHrefsCmd},

    /*
     * tclset.c
     */

    {"ns_set", NsTclSetCmd},
    {"ns_parseheader", NsTclParseHeaderCmd},

    /*
     * adp.c
     */

    {"_ns_adp_include", NsTclAdpIncludeCmd},
    {"ns_adp_eval", NsTclAdpEvalCmd},
    {"ns_adp_parse", NsTclAdpParseCmd},
    {"ns_puts", NsTclAdpPutsCmd},
    {"ns_adp_puts", NsTclAdpPutsCmd},
    {"ns_adp_dir", NsTclAdpDirCmd},
    {"ns_adp_return", NsTclAdpReturnCmd},
    {"ns_adp_break", NsTclAdpBreakCmd},
    {"ns_adp_abort", NsTclAdpAbortCmd},
    {"ns_adp_tell", NsTclAdpTellCmd},
    {"ns_adp_trunc", NsTclAdpTruncCmd},
    {"ns_adp_dump", NsTclAdpDumpCmd},
    {"ns_adp_argc", NsTclAdpArgcCmd},
    {"ns_adp_argv", NsTclAdpArgvCmd},
    {"ns_adp_bind_args", NsTclAdpBindArgsCmd},
    {"ns_adp_exception", NsTclAdpExceptionCmd},
    {"ns_adp_stream", NsTclAdpStreamCmd},
    {"ns_adp_debug", NsTclAdpDebugCmd},
    {"ns_adp_mime", NsTclAdpMimeCmd},

    /*
     * tclvar.c
     */

    {"ns_share", NsTclShareCmd},
    {"ns_var", NsTclVarCmd},
    {"nsv_get", NsTclNsvGetCmd},
    {"nsv_exists", NsTclNsvExistsCmd},
    {"nsv_set", NsTclNsvSetCmd},
    {"nsv_incr", NsTclNsvIncrCmd},
    {"nsv_append", NsTclNsvAppendCmd},
    {"nsv_lappend", NsTclNsvLappendCmd},
    {"nsv_array", NsTclNsvArrayCmd},
    {"nsv_unset", NsTclNsvUnsetCmd},
    {"nsv_names", NsTclNsvNamesCmd},

    /*
     * serv.c
     */

    {"ns_server", NsTclServerCmd},

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

void
NsTclAddCmds(NsInterp *itPtr, Tcl_Interp *interp)
{
    int i;

    for (i = 0; cmds[i].name != NULL; ++i) {
	Tcl_CreateCommand(interp, cmds[i].name, cmds[i].proc, itPtr, NULL);
    }
    if (itPtr != NULL) {
    	for (i = 0; servcmds[i].name != NULL; ++i) {
	    Tcl_CreateCommand(interp, servcmds[i].name, servcmds[i].proc, itPtr, NULL);
        }
    }
}
