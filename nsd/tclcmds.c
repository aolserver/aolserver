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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclcmds.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static TclCmd tclCmds[ ] = {

    /*
     * tclset.c
     */

    {
        "ns_set", NsTclSetCmd, NULL
    },
    {
        "ns_parseheader", NsTclParseHeaderCmd, NULL
    },

    /*
     * tclsched.c
     */

    {
        "ns_schedule_proc", NsTclSchedCmd, NULL
    },
    {
        "ns_schedule_daily", NsTclSchedDailyCmd, NULL
    },
    {
        "ns_schedule_weekly", NsTclSchedWeeklyCmd, NULL
    },
    {
        "ns_atsignal", NsTclAtSignalCmd, NULL
    },
    {
        "ns_atshutdown", NsTclAtShutdownCmd, NULL
    },
    {
        "ns_atexit", NsTclAtExitCmd, NULL
    },
    {
        "ns_after", NsTclAfterCmd, NULL
    },
    {
        "ns_cancel", NsTclCancelCmd, (ClientData) 'c'
    },
    {
        "ns_pause", NsTclCancelCmd, (ClientData) 'p'
    },
    {
        "ns_resume", NsTclCancelCmd, (ClientData) 'r'
    },
    {
        "ns_unschedule_proc", NsTclCancelCmd, (ClientData) 'u'
    },

    /*
     * tclsock.c
     */

    {
        "ns_sockblocking", NsTclSockSetBlockingCmd, NULL
    },
    {
        "ns_socknonblocking", NsTclSockSetNonBlockingCmd, NULL
    },
    {
        "ns_socknread", NsTclSockNReadCmd, NULL
    },
    {
        "ns_sockopen", NsTclSockOpenCmd, NULL
    },
    {
        "ns_socklisten", NsTclSockListenCmd, NULL
    },
    {
        "ns_sockaccept", NsTclSockAcceptCmd, NULL
    },
    {
        "ns_sockcallback", NsTclSockCallbackCmd, NULL
    },
    {
        "ns_socklistencallback", NsTclSockListenCallbackCmd, NULL
    },
    {
        "ns_sockcheck", NsTclSockCheckCmd, NULL
    },
    {
        "ns_sockselect", NsTclSelectCmd, NULL
    },
    {
        "ns_hostbyaddr", NsTclGetByCmd, NULL
    },
    {
        "ns_addrbyhost", NsTclGetByCmd, (ClientData) 1
    },
    {
	"ns_socketpair", NsTclSocketPairCmd, NULL
    },


    /*
     * tclop.c
     */

    {
        "ns_register_filter", NsTclRegisterFilterCmd, NULL
    },
    {
        "ns_register_trace", NsTclRegisterTraceCmd, NULL
    },
    {
        "ns_register_proc", NsTclRegisterCmd, NULL
    },
    {
        "ns_unregister_proc", NsTclUnRegisterCmd, NULL
    },
    {
        "ns_eval", NsTclEvalCmd, NULL
    },
    {
	"ns_atclose", NsTclAtCloseCmd, NULL
    },

    /*
     * tclconf.c
     */

    {
        "ns_config", NsTclConfigCmd, NULL
    },
    {
        "ns_configsection", NsTclConfigSectionCmd, NULL
    },
    {
        "ns_configsections", NsTclConfigSectionsCmd, NULL
    },

    /*
     * tclresp.c
     */

    {
        "ns_return", NsTclReturnCmd, NULL
    },
    {
        "ns_respond", NsTclRespondCmd, NULL
    },
    {
        "ns_returnfile", NsTclReturnFileCmd, NULL
    },
    {
        "ns_returnfp", NsTclReturnFpCmd, NULL
    },
    {
        "ns_returnbadrequest", NsTclReturnBadRequestCmd, NULL
    },
    {
        "ns_returnerror", NsTclReturnErrorCmd, NULL
    },
    {
        "ns_returnnotice", NsTclReturnNoticeCmd, NULL
    },
    {
        "ns_returnadminnotice", NsTclReturnAdminNoticeCmd, NULL
    },
    {
        "ns_returnredirect", NsTclReturnRedirectCmd, NULL
    },
    {
        "ns_headers", NsTclHeadersCmd, NULL
    },
    {
        "ns_write", NsTclWriteCmd, NULL
    },
    {
        "ns_connsendfp", NsTclConnSendFpCmd, NULL
    },

    /*
     * tclfile.c
     */

    {
        "ns_unlink", NsTclUnlinkCmd, NULL
    },
    {
        "ns_mkdir", NsTclMkdirCmd, NULL
    },
    {
        "ns_rmdir", NsTclRmdirCmd, NULL
    },
    {
        "ns_cp", NsTclCpCmd, NULL
    },
    {
        "ns_cpfp", NsTclCpFpCmd, NULL
    },
    {
        "ns_url2file", NsTclUrl2FileCmd, NULL
    },
    {
        "ns_rollfile", NsTclRollFileCmd, (ClientData) "roll"
    },
    {
        "ns_purgefiles", NsTclRollFileCmd, (ClientData) "purge"
    },
    {
        "ns_mktemp", NsTclMkTempCmd, NULL
    },
    {
        "ns_tmpnam", NsTclTmpNamCmd, NULL
    },
    {
        "ns_normalizepath", NsTclNormalizePathCmd, NULL
    },
    {
        "ns_link", NsTclLinkCmd, NULL
    },
    {
	"ns_symlink", NsTclSymlinkCmd, NULL
    },
    {
        "ns_rename", NsTclRenameCmd, NULL
    },
    {
        "ns_kill", NsTclKillCmd, NULL
    },
    {
        "ns_writefp", NsTclWriteFpCmd, NULL
    },
    {
	"ns_truncate", NsTclTruncateCmd, NULL
    },
    {
	"ns_ftruncate", NsTclFTruncateCmd, NULL
    },
    {
	"ns_chmod", NsTclChmodCmd, NULL
    },
    {
	"ns_getchannels", NsTclGetChannelsCmd, NULL
    },

    /*
     * log.c
     */

    {
        "ns_log", NsTclLogCmd, NULL
    },
    {
        "ns_logroll", NsTclLogRollCmd, NULL
    },

    /*
     * tclcmds.c (this file)
     */

    {
        "ns_var", NsTclVarCmd, NULL
    },
    {
        "ns_share", NsTclShareCmd, NULL
    },
    {
        "ns_info", NsTclInfoCmd, NULL
    },
    {
        "ns_library", NsTclLibraryCmd, NULL
    },
    {
        "ns_guesstype", NsTclGuessTypeCmd, NULL
    },
    {
        "ns_geturl", NsTclGetUrlCmd, NULL
    },
    {
        "ns_conncptofp", NsTclWriteContentCmd, NULL
    },
    {
        "ns_writecontent", NsTclWriteContentCmd, NULL
    },
    {
        "ns_striphtml", NsTclStripHtmlCmd, NULL
    },
    {
        "ns_conn", NsTclConnCmd, NULL
    },
    {
        "ns_quotehtml", NsTclQuoteHtmlCmd, NULL
    },
    {
        "ns_crypt", NsTclCryptCmd, NULL
    },
    {
        "ns_checkurl", NsTclRequestAuthorizeCmd, NULL
    },
    {
        "ns_requestauthorize", NsTclRequestAuthorizeCmd, NULL
    },
    {
        "ns_hrefs", NsTclHrefsCmd, NULL
    },
    {
        "ns_localtime", NsTclLocalTimeCmd
    },
    {
        "ns_gmtime", NsTclGmTimeCmd
    },
    {
        "ns_time", NsTclTimeCmd, NULL
    },
    {
        "ns_fmttime", NsTclStrftimeCmd, NULL
    },
    {
        "ns_sleep", NsTclSleepCmd, NULL
    },
    {
        "ns_urlencode", NsTclUrlEncodeCmd, NULL
    },
    {
        "ns_urldecode", NsTclUrlDecodeCmd, NULL
    },
    {
        "ns_module", NsTclModuleCmd, NULL
    },
    {
        "ns_modulepath", NsTclModulePathCmd, NULL
    },
    {
        "ns_uuencode", NsTclHTUUEncodeCmd, NULL
    },
    {
        "ns_uudecode", NsTclHTUUDecodeCmd, NULL
    },
    {
        "ns_httptime", NsTclHttpTimeCmd, NULL
    },
    {
        "ns_parsehttptime", NsTclParseHttpTimeCmd, NULL
    },
    {
        "ns_get_multipart_formdata", NsTclGetMultipartFormdataCmd, NULL
    },
    {
	"ns_gifsize", NsTclGifSizeCmd, NULL
    },
    {
	"ns_markfordelete", NsTclMarkForDeleteCmd, NULL
    },
    {
	"ns_jpegsize", NsTclJpegSizeCmd, NULL
    },

    /*
     * tclthread.c
     */

    {
        "ns_thread", NsTclThreadCmd, NULL
    },
    {
        "ns_mutex", NsTclMutexCmd, NULL
    },
    {
        "ns_cond", NsTclEventCmd, NULL
    },
    {
        "ns_event", NsTclEventCmd, NULL
    },
    {
	"ns_rwlock", NsTclRWLockCmd, NULL
    },
    {
        "ns_sema", NsTclSemaCmd, NULL
    },
    {
        "ns_critsec", NsTclCritSecCmd, NULL
    },

    /*
     * tcladmin.c
     */

    {
        "ns_server", NsTclServerCmd, NULL
    },
    {
        "ns_shutdown", NsTclShutdownCmd, NULL
    },

    /*
     * random.c
     */

    {
	"ns_rand", NsTclRandCmd, NULL
    },

    /*
     * conn.c
     */

    {
	"ns_parsequery", NsTclParseQueryCmd, NULL
    },

    /*
     * adp.c
     */

    {
	"ns_puts", NsTclPutsCmd, NULL
    },
    {
	"ns_adp_puts", NsTclPutsCmd, NULL
    },
    {
	"ns_adp_dir", NsTclDirCmd, NULL
    },
    {
	"ns_adp_break", NsTclBreakCmd, (ClientData) ADP_BREAK
    },
    {
	"ns_adp_return", NsTclBreakCmd, (ClientData) ADP_RETURN
    },
    {
	"ns_adp_abort", NsTclBreakCmd, (ClientData) ADP_ABORT
    },
    {
	"ns_adp_exception", NsTclExceptionCmd, NULL
    },
    {
	"ns_adp_argc", NsTclArgcCmd, NULL
    },
    {
	"ns_adp_argv", NsTclArgvCmd, NULL
    },
    {
	"ns_adp_bind_args", NsTclBindCmd, NULL
    },
    {
	"ns_adp_tell", NsTclTellCmd, NULL
    },
    {
	"ns_adp_trunc", NsTclTruncCmd, NULL
    },
    {
	"ns_adp_dump", NsTclDumpCmd, NULL
    },
    {
	"ns_adp_eval", NsTclAdpEvalCmd, NULL
    },
    {
	"ns_adp_parse", NsTclAdpParseCmd, NULL
    },
    {
	"ns_adp_stream", NsTclStreamCmd, NULL
    },
    {
	"ns_adp_debug", NsTclDebugCmd, NULL
    },
    {
	"ns_adp_mimetype", NsTclAdpMimeCmd, NULL
    },

    /*
     * adpfancy.c
     */

    {
	"ns_register_adptag", NsTclRegisterTagCmd, NULL
    },
    {
	"ns_adp_registeradp", NsTclRegisterAdpCmd, NULL
    },

    /*
     * cache.c
     */

    {
	"ns_cache_flush", NsTclCacheFlushCmd, NULL
    },
    {
	"ns_cache_stats", NsTclCacheStatsCmd, NULL
    },
    {
	"ns_cache_names", NsTclCacheNamesCmd, NULL
    },
    {
	"ns_cache_size", NsTclCacheSizeCmd, NULL
    },

    /*
     * dbtcl.c
     */

    {
	"ns_db", NsTclDbCmd, NULL
    },
    {
	"ns_dbconfigpath", NsTclDbConfigPathCmd, NULL
    },
    {
	"ns_pooldescription", NsTclPoolDescriptionCmd, NULL
    },
    {
	"ns_dberrorcode", NsTclDbErrorCodeCmd, NULL
    },
    {
	"ns_dberrormsg", NsTclDbErrorMsgCmd, NULL
    },
    {
	"ns_quotelisttolist", NsTclQuoteListToListCmd, NULL
    },
    {
	"ns_getcsv", NsTclGetCsvCmd, NULL
    },
    {
	"ns_column", NsTclUnsupDbCmd, NULL
    },
    {
	"ns_table", NsTclUnsupDbCmd, NULL
    },
    {
	"ns_dbreturnerror", NsTclUnsupDbCmd, NULL
    },

    /*
     * tclenv.c
     */

    {
	"ns_env", NsTclEnvCmd, NULL
    },
    {
	"env", NsTclEnvCmd, NULL	/* NB: Backwards compatible. */
    },
    
    /*
     * tclstats.c
     */

    {
    	"ns_stats", NsTclStatsCmd, NULL
    },

    /*
     * tclvar.c
     */

    {
    	"nsv_array", NsTclVArrayCmd, NULL,
    },
    {
    	"nsv_get", NsTclVGetCmd, (ClientData) 'g'
    },
    {
    	"nsv_exists", NsTclVGetCmd, (ClientData) 'e'
    },
    {
    	"nsv_set", NsTclVSetCmd, (ClientData) 's'
    },
    {
    	"nsv_append", NsTclVAppendCmd, (ClientData) 'a'
    },
    {
    	"nsv_lappend", NsTclVAppendCmd, (ClientData) 'l'
    },
    {
    	"nsv_unset", NsTclVUnsetCmd, NULL
    },
    {
    	"nsv_incr", NsTclVIncrCmd, NULL
    },

    /*
     * add more Tcl commands here 
     */

    {
        NULL, NULL, NULL
    }
};


/*
 *----------------------------------------------------------------------
 *
 * NsTclCreateCmds --
 *
 *	Loop over all the tcl commands in this file and create them. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

void
NsTclCreateCmds(Tcl_Interp *interp)
{
    struct TclCmd *cmd;
    char          *crash;

    cmd = tclCmds;
    while (cmd->name != NULL) {
	Tcl_CreateCommand(interp, cmd->name, cmd->proc, cmd->clientData, NULL);
        ++cmd;
    }

    Tcl_CreateCommand(interp, "ns_returnforbidden", NsTclSimpleReturnCmd,
		      (void *) Ns_ConnReturnForbidden, NULL);
    Tcl_CreateCommand(interp, "ns_returnunauthorized",
		      NsTclSimpleReturnCmd,
		      (void *) Ns_ConnReturnUnauthorized, NULL);
    Tcl_CreateCommand(interp, "ns_returnnotfound", NsTclSimpleReturnCmd,
		      (void *) Ns_ConnReturnNotFound, NULL);

    crash = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "CrashCmd");
    if (crash != NULL) {
        Tcl_CreateCommand(interp, crash, NsTclCrashCmd, NULL, NULL);
    }
}

