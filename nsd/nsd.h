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

#ifndef NSD_H
#define NSD_H

#include "ns.h"
#include <assert.h>

#ifdef WIN32

#include <fcntl.h>
#include <io.h>
#define STDOUT_FILENO	1
#define STDERR_FILENO	2
#define S_ISREG(m)	((m)&_S_IFREG)
#define S_ISDIR(m)	((m)&_S_IFDIR)
#include "getopt.h"

#else

#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#ifdef __sun
#include <sys/filio.h>
#endif
#ifdef __hp
#define seteuid(i)     setresuid((-1),(i),(-1))
#endif
#ifdef __sun
#include <sys/systeminfo.h>
#define gethostname(b,s) (!(sysinfo(SI_HOSTNAME, b, s) > 0))
#endif
#ifdef __unixware
#include <sys/filio.h>
#include <strings.h>
#include <sys/uio.h>
#include <sys/sendv.h>
#endif

#endif	/* WIN32 */

#include <sys/stat.h>
#include <ctype.h>

#ifndef F_CLOEXEC
#define F_CLOEXEC 1
#endif

#ifdef WIN32
#define NS_SIGTERM  1
#define NS_SIGHUP   2
#define NS_SIGTCL   4
#else
#define NS_SIGTERM  SIGTERM
#define NS_SIGHUP   SIGHUP
#define NS_SIGTCL   SIGUSR2
#endif

#define UCHAR(c)	((unsigned char) (c))

/*
 * constants
 */

#define NSD_NAME             "AOLserver"
#define NSD_VERSION	     "3.2"
#define NSD_LABEL            "aolserver3_2"
#define NS_CONFIG_PARAMETERS "ns/parameters"
#define NS_CONFIG_SERVERS    "ns/servers"
#define NS_CONFIG_THREADS    "ns/threads"

#define DEFAULT_MAXLINE       8192
#define DEFAULT_MAXHEADERS   16384
#define DEFAULT_MAXPOST      65536

#define ADP_OK       0
#define ADP_BREAK    1
#define ADP_ABORT    2
#define ADP_OVERFLOW 3
#define ADP_RETURN   4

#define STATS_GLOBAL 1
#define STATS_PERURL 2

#define Dev         (Debug+1)

/*
 * Typedef definitions.
 */

typedef int bool;

struct _nsconf {
    char    	   *argv0;
    char    	   *nsd;
    char           *name;
    char           *version;
    char           *home;
    char           *config;
    int		    configfmt;
    int             pid;
    time_t          boot_t;
    char            hostname[255];
    char	    address[16];
    int             shutdowntimeout;
    int             backlog;
    char           *server;
    int             bufsize;

    struct {
	bool dev;
	bool debug;
	bool expanded;
	int  maxback;
    } log;

    struct {
	int maxelapsed;
    } sched;

    struct {
	char *errorpage;
	char *startpage;
	bool cache;
	bool threadcache;
	int cachesize;
	bool enableexpire;
	bool enabledebug;
	char *debuginit;
	bool taglocks;
	char *defaultparser;
    } adp;

    struct {
    	int maxheaders;
    	int maxline;
    	int maxpost;
	bool flushcontent;
    	bool modsince;
	Ns_HeaderCaseDisposition hdrcase;
    } conn;

    struct {
	bool cache;
	int timeout;
    } dns;

    struct {
	int maxentries;
	int maxsize;
    } dstring;

    struct {
	bool checkexit;
    } exec;

    struct {
	char *pageroot;
	char **dirv;
	int dirc;
	char *dirproc;
	char *diradp;
	bool mmap;
	bool cache;
	int cachesize;
	int cachemaxentry;
    } fastpath;

    struct {
	bool enabled;
	int timeout;
	int maxkeep;
	int npending;
    } keepalive;

    struct {
	char *library;
	char *sharedlibrary;
	bool autoclose;
	bool debug;
	int  statlevel;
	int  statmaxbuf;
	int  nsvbuckets;
    } tcl;

    struct {
	char *realm;
	bool aolpress;
	int sendfdmin;
	int maxconns;
	int maxdropped;
	int connsperthread;
	int minthreads;
	int maxthreads;
	int threadtimeout;
	int stats;
	int maxurlstats;
	int errorminsize;
	int noticedetail;
    } serv;
    
};

extern struct _nsconf nsconf;


/*
 * This is the internal structure for a comm driver.
 */

typedef struct Driver {
    struct Driver      *nextPtr;
    char               *label;
    void               *drvData;
    int                 running;

    /*
     * The following procs are required for all drivers.
     */

    Ns_ConnReadProc         *readProc;
    Ns_ConnWriteProc        *writeProc;
    Ns_ConnCloseProc 	    *closeProc;

    /*
     * The following procs, if present, indicates the
     * the driver is capable of connection: keep-alive.
     */

    Ns_ConnDetachProc       *detachProc;
    Ns_ConnConnectionFdProc *sockProc;

    /*
     * The following procs are optional.
     */

    Ns_ConnDriverNameProc   *nameProc;
    Ns_DriverStartProc      *startProc;
    Ns_DriverStopProc       *stopProc;
    Ns_DriverAcceptProc     *acceptProc;
    Ns_ConnInitProc 	    *initProc;
    Ns_ConnFreeProc 	    *freeProc;
    Ns_ConnHostProc         *hostProc;
    Ns_ConnPortProc         *portProc;
    Ns_ConnLocationProc     *locationProc;
    Ns_ConnPeerProc         *peerProc;
    Ns_ConnPeerPortProc     *peerPortProc;
    Ns_ConnSendFdProc       *sendFdProc;
    Ns_ConnSendFileProc     *sendFileProc;
} Driver;

typedef enum {
    Headers,
    Content
} ConnState;

typedef struct Conn {

    /*
     * Visible in an Ns_Conn:
     */
    
    Ns_Request  *request;
    Ns_Set      *headers;
    Ns_Set      *outputheaders;
    char        *authUser;
    char        *authPasswd;
    int          contentLength;
    int          flags;

    /*
     * Visible only in a Conn:
     */
    
    struct Conn *prevPtr;
    struct Conn *nextPtr;
    int          id;
    time_t	 startTime;
    Ns_Time	 tqueue;
    Ns_Time	 tstart;
    Ns_Time	 tclose;
    Driver      *drvPtr;
    void        *drvData;
    Ns_Set      *query;
    char	*peer;
    char	 peerBuf[32];

    /*
     * Tcl state for the ns_conn command.
     */

    Tcl_Interp  *interp;
    int		 tclInit;
    char         tclHdrs[20];
    char         tclOutputHdrs[20];
    int     	 tclFormInit;
    char         tclForm[20];
    char         tclConn[20];

    ConnState    readState;
    ConnState    sendState;
    int          nContent;
    int          nContentSent;
    int          responseStatus;
    int          responseLength;
    int          recursionCount;
    int		 keepAlive;
} Conn;

/*
 * Tcl support structures.
 */

typedef struct TclSet {
    int     flags;
    Ns_Set *set;
} TclSet;

struct Defer;
struct AtClose;

typedef struct TclData {
    Tcl_Interp	  *interp;
    int		   deleteInterp;
    Tcl_HashTable  sets;
    unsigned int   setNum;
    unsigned int   dbNum;
    Tcl_HashTable  dbs;
    char	  *defDb;
    struct Defer  *firstDeferPtr;
    struct AtClose *firstAtClosePtr;
    int		   lastEpoch;
} TclData;

typedef struct TclCmd {
    char           *name;
    Tcl_CmdProc    *proc;
    ClientData      clientData;
} TclCmd;

typedef struct TclCmdInfo {
    struct TclCmdInfo *nextPtr;
} TclCmdInfo;

/*
 * The following structure maintains per-thread ADP context.
 */

typedef struct AdpData {
    int	               exception;
    int                depth;
    int                argc;
    char             **argv;
    char              *cwd;
    char              *mimeType;
    int                evalLevel;
    int                errorLevel;
    int                debugLevel;
    int                debugInit;
    char              *debugFile;
    Ns_DString	       output;
    Ns_Conn           *conn;
    int                fStream;
    Ns_AdpParserProc  *parserProc;
    Ns_Cache	      *cachePtr;
} AdpData;

/*
 * The following structure defines a parsed page in the ADP cache.
 */

typedef struct Page {
    time_t  mtime;
    off_t   size;
    off_t   length;
    void   *pdPtr;	    /* Page data, used by adp81. */
    char    chunks[4];
} Page;

/*
 * External callback functions.
 */

extern Ns_ThreadProc NsTclThread;
extern Ns_Callback NsTclCallback;
extern Ns_Callback NsTclSignalProc;
extern Ns_SchedProc NsTclSchedProc;
extern Ns_ArgProc NsTclArgProc;
extern Ns_Callback NsCachePurge;
extern Ns_ArgProc NsCacheArgProc;
extern Ns_SockProc NsTclSockProc;
extern Ns_ArgProc NsTclSockArgProc;
extern Ns_ThreadProc NsConnThread;
extern Ns_ArgProc NsConnArgProc;

extern void NsGetCallbacks(Tcl_DString *dsPtr);
extern void NsGetSockCallbacks(Tcl_DString *dsPtr);
extern void NsGetScheduled(Tcl_DString *dsPtr);


/*
 * Config file routines.
 */

extern char *NsConfigRead(char *file);
extern void  NsConfigFree(char *buf);
extern void  NsConfigEval(char *script);
extern void  NsConfigParse(char *file, char *config);
extern void  NsConfigParseAux();  /* AuxConfigDir */
extern void  NsConfInit(void);


/*
 * Initialization routines.
 */

#ifdef WIN32
extern void NsInitTls(void);
extern int NsConnectService(Ns_ServerInitProc *initProc);
extern int NsInstallService(void);
extern int NsRemoveService(void);
#endif

extern void NsProcInit(void);
extern void NsTclStatsInit(void);
extern void NsAdpInit(void);
extern void NsAdpParsers(void);
extern void NsCacheInit(void);
extern void NsDNSInit(void);
extern void NsDbInit(void);
extern void NsDbTclInit(void);
extern void NsInitBinder(char *args, char *file);
extern void NsForkBinder(void);
extern void NsGetURLInit(void);
extern void NsInitFastpath(void);
extern void NsInitMimeTypes(void);
extern void NsInitReturn(void);
extern void NsLoadModules(void);
extern void NsLogOpen(void);
extern void NsPidFileInit(int fkill);
extern void NsPortInit(void);
extern void NsSchedInit(void);
extern void NsStopBinder(void);
extern char *NsTclFindExecutable(char *argv0);
extern void NsTclInit(void);
extern void NsTclInitGlobal(void);
extern void NsTclInitScripts(void);

extern void NsCreatePidFile(void);
extern void NsRemovePidFile(void);
extern int  NsGetLastPid(void);
extern void NsKillPid(int pid);
extern void NsBlockSignals(int debug);
extern void NsHandleSignals(void);
extern void NsRestoreSignals(void);
extern void NsSendSignal(int sig);
extern void NsShutdown(int timeout);

extern void NsStartServer(void);
extern void NsStopDrivers(void);
extern void NsStartKeepAlive(void);
extern void NsStopKeepAlive(void);
extern void NsStopServer(Ns_Time *toPtr);

extern void NsStartSchedShutdown(void);
extern void NsWaitSchedShutdown(Ns_Time *toPtr);

extern void NsStartSockShutdown(void); 
extern void NsWaitSockShutdown(Ns_Time *toPtr);

extern void NsStartShutdownProcs(void);
extern void NsWaitShutdownProcs(Ns_Time *toPtr);

extern void NsRunAtExitProcs(void);

/*
 * ADP routines.
 */

extern int      NsAdpEval(Tcl_Interp *, char *file, char *chunks);
extern void     NsAdpFancyInit(char *server, char *path);
extern void     NsAdpFlush(AdpData *adPtr);
extern AdpData *NsAdpGetData(void);
extern Page    *NsAdpCopyShared(Ns_DString *dsPtr, struct stat *stPtr);
extern Page    *NsAdpCopyPrivate(Ns_DString *dsPtr, struct stat *stPtr);
extern int      NsAdpRunPrivate(Tcl_Interp *, char *file, Page *pagePtr);
extern void     NsAdpFreePrivate(Page *pagePtr);
extern void	NsAdpLogError(Tcl_Interp *, char *file, int chunk);

/*
 * Fancy ADP routines.
 */

extern void NsAdpFancyInit();

/*
 * Database routines.
 */

extern void 		NsDbClose(Ns_DbHandle *);
extern void 		NsDbDisconnect(Ns_DbHandle *);
extern struct DbDriver *NsDbGetDriver(Ns_DbHandle *);
extern struct DbDriver *NsDbLoadDriver(char *driver);
extern void 		NsDbLogSql(Ns_DbHandle *, char *sql);
extern int 		NsDbOpen(Ns_DbHandle *);
extern void 		NsDbServerInit(struct DbDriver *);

/*
 * Tcl support routines.
 */

extern Ns_Callback NsCleanupTclDb;
extern Ns_Callback NsCleanupTclSet;
extern char 	  *NsConstructScriptCall(char *script, char *scriptarg);
extern char 	  *NsTclConnId(Ns_Conn *conn);
extern int 	   NsIsIdConn(char *inID);
extern void 	   NsSigRestore(void);
extern int 	   NsTclEval(Tcl_Interp *interp, char *script);
extern void 	   NsTclCopyCommands(Tcl_Interp *from, Tcl_Interp *to);
extern void 	   NsTclCreateCmds(Tcl_Interp *);
extern void 	   NsTclCreateObjCmds(Tcl_Interp *);
extern void	   NsTclFreeAtClose(struct AtClose *firstPtr);
extern TclData    *NsTclGetData(Tcl_Interp *);
extern int	   NsTclShareVar(Tcl_Interp *interp, char *varName);

extern TclCmdInfo *NsTclGetCmdInfo(Tcl_Interp *interp, char *name);
extern void 	   NsTclCreateCommand(Tcl_Interp *interp, TclCmdInfo *cmdPtr);
extern void	   NsTclRunInits(void);

/*
 * Callback routines.
 */

extern int  NsRunFilters(Ns_Conn *conn, int why);
extern void NsRunCleanups(Ns_Conn *conn);
extern void NsRunTraces(Ns_Conn *conn);
extern void NsRunPreStartupProcs(void);
extern void NsRunSignalProcs(void);
extern void NsRunStartupProcs(void);

/*
 * Utility functions.
 */

extern int  NsCloseAllFiles(int errFd);
extern int  Ns_ConnRunRequest(Ns_Conn *conn);
extern int  Ns_GetGid(char *group);
extern int  Ns_GetUserGid(char *user);
extern int  Ns_TclGetOpenFd(Tcl_Interp *, char *, int write, int *fp);
extern void NsStopSockCallbacks(void);
extern void NsStopScheduledProcs(void);
extern void NsGetBuf(char **bufPtr, int *sizePtr);

/*
 * Drivers
 */

extern int 	  NsKeepAlive(Ns_Conn *connPtr);
extern void 	  NsStartDrivers(void);

/*
 * Proxy support
 */

extern void NsInitProxyRequests(void);
extern int NsConnRunProxyRequest(Ns_Conn *conn);

/*
 * AOLserver Tcl commands
 */

extern Tcl_CmdProc NsTclAfterCmd;
extern Tcl_CmdProc NsTclAtCloseCmd;
extern Tcl_CmdProc NsTclAtExitCmd;
extern Tcl_CmdProc NsTclAtShutdownCmd;
extern Tcl_CmdProc NsTclAtSignalCmd;
extern Tcl_CmdProc NsTclCancelCmd;
extern Tcl_CmdProc NsTclChmodCmd;
extern Tcl_CmdProc NsTclConfigCmd;
extern Tcl_CmdProc NsTclConfigSectionCmd;
extern Tcl_CmdProc NsTclConfigSectionsCmd;
extern Tcl_CmdProc NsTclConnSendFpCmd;
extern Tcl_CmdProc NsTclCpCmd;
extern Tcl_CmdProc NsTclCpFpCmd;
extern Tcl_CmdProc NsTclCritSecCmd;
extern Tcl_CmdProc NsTclEvalCmd;
extern Tcl_CmdProc NsTclEventCmd;
extern Tcl_CmdProc NsTclFTruncateCmd;
extern Tcl_CmdProc NsTclGetByCmd;
extern Tcl_CmdProc NsTclHeadersCmd;
extern Tcl_CmdProc NsTclKillCmd;
extern Tcl_CmdProc NsTclLinkCmd;
extern Tcl_CmdProc NsTclLogCmd;
extern Tcl_CmdProc NsTclLogRollCmd;
extern Tcl_CmdProc NsTclMkTempCmd;
extern Tcl_CmdProc NsTclMkdirCmd;
extern Tcl_CmdProc NsTclMutexCmd;
extern Tcl_CmdProc NsTclNormalizePathCmd;
extern Tcl_CmdProc NsTclParseHeaderCmd;
extern Tcl_CmdProc NsTclRWLockCmd;
extern Tcl_CmdProc NsTclRandCmd;
extern Tcl_CmdProc NsTclRegisterCmd;
extern Tcl_CmdProc NsTclRegisterFilterCmd;
extern Tcl_CmdProc NsTclRegisterTraceCmd;
extern Tcl_CmdProc NsTclRenameCmd;
extern Tcl_CmdProc NsTclRespondCmd;
extern Tcl_CmdProc NsTclReturnAdminNoticeCmd;
extern Tcl_CmdProc NsTclReturnBadRequestCmd;
extern Tcl_CmdProc NsTclReturnCmd;
extern Tcl_CmdProc NsTclReturnErrorCmd;
extern Tcl_CmdProc NsTclReturnFileCmd;
extern Tcl_CmdProc NsTclReturnFpCmd;
extern Tcl_CmdProc NsTclReturnNoticeCmd;
extern Tcl_CmdProc NsTclReturnRedirectCmd;
extern Tcl_CmdProc NsTclRmdirCmd;
extern Tcl_CmdProc NsTclRollFileCmd;
extern Tcl_CmdProc NsTclSchedCmd;
extern Tcl_CmdProc NsTclSchedDailyCmd;
extern Tcl_CmdProc NsTclSchedWeeklyCmd;
extern Tcl_CmdProc NsTclSelectCmd;
extern Tcl_CmdProc NsTclSemaCmd;
extern Tcl_CmdProc NsTclSetCmd;
extern Tcl_CmdProc NsTclSimpleReturnCmd;
extern Tcl_CmdProc NsTclSockAcceptCmd;
extern Tcl_CmdProc NsTclSockCallbackCmd;
extern Tcl_CmdProc NsTclSockCheckCmd;
extern Tcl_CmdProc NsTclSockListenCallbackCmd;
extern Tcl_CmdProc NsTclSockListenCmd;
extern Tcl_CmdProc NsTclSockNReadCmd;
extern Tcl_CmdProc NsTclSockOpenCmd;
extern Tcl_CmdProc NsTclSockSetBlockingCmd;
extern Tcl_CmdProc NsTclSockSetNonBlockingCmd;
extern Tcl_CmdProc NsTclSocketPairCmd;
extern Tcl_CmdProc NsTclSymlinkCmd;
extern Tcl_CmdProc NsTclThreadCmd;
extern Tcl_CmdProc NsTclTmpNamCmd;
extern Tcl_CmdProc NsTclTruncateCmd;
extern Tcl_CmdProc NsTclUnRegisterCmd;
extern Tcl_CmdProc NsTclUnlinkCmd;
extern Tcl_CmdProc NsTclUrl2FileCmd;
extern Tcl_CmdProc NsTclWriteCmd;
extern Tcl_CmdProc NsTclWriteFpCmd;
extern Tcl_CmdProc NsTclClientDebugCmd;
extern Tcl_CmdProc NsTclParseQueryCmd;
extern Tcl_CmdProc NsTclQueryResolveCmd;
extern Tcl_CmdProc NsTclServerCmd;
extern Tcl_CmdProc NsTclSetExpiresCmd;
extern Tcl_CmdProc NsTclShutdownCmd;
extern Tcl_CmdProc Tcl_KeyldelCmd;
extern Tcl_CmdProc Tcl_KeylgetCmd;
extern Tcl_CmdProc Tcl_KeylkeysCmd;
extern Tcl_CmdProc Tcl_KeylsetCmd;

extern Tcl_CmdProc NsTclConnCmd;
extern Tcl_CmdProc NsTclCrashCmd;
extern Tcl_CmdProc NsTclCryptCmd;
extern Tcl_CmdProc NsTclGetMultipartFormdataCmd;
extern Tcl_CmdProc NsTclGetUrlCmd;
extern Tcl_CmdProc NsTclGifSizeCmd;
extern Tcl_CmdProc NsTclGuessTypeCmd;
extern Tcl_CmdProc NsTclHTUUDecodeCmd;
extern Tcl_CmdProc NsTclHTUUEncodeCmd;
extern Tcl_CmdProc NsTclHrefsCmd;
extern Tcl_CmdProc NsTclHttpTimeCmd;
extern Tcl_CmdProc NsTclInfoCmd;
extern Tcl_CmdProc NsTclJpegSizeCmd;
extern Tcl_CmdProc NsTclLibraryCmd;
extern Tcl_CmdProc NsTclLocalTimeCmd;
extern Tcl_CmdProc NsTclGmTimeCmd;
extern Tcl_CmdProc NsTclMarkForDeleteCmd;
extern Tcl_CmdProc NsTclModuleCmd;
extern Tcl_CmdProc NsTclModulePathCmd;
extern Tcl_CmdProc NsTclParseHttpTimeCmd;
extern Tcl_CmdProc NsTclQuoteHtmlCmd;
extern Tcl_CmdProc NsTclRequestAuthorizeCmd;
extern Tcl_CmdProc NsTclShareCmd; 
extern Tcl_CmdProc NsTclSleepCmd;
extern Tcl_CmdProc NsTclStrftimeCmd;
extern Tcl_CmdProc NsTclStripHtmlCmd;
extern Tcl_CmdProc NsTclTimeCmd;
extern Tcl_CmdProc NsTclUrlDecodeCmd;
extern Tcl_CmdProc NsTclUrlEncodeCmd;
extern Tcl_CmdProc NsTclVarCmd;
extern Tcl_CmdProc NsTclWriteContentCmd;

extern Tcl_CmdProc NsTclPutsCmd;
extern Tcl_CmdProc NsTclDirCmd;
extern Tcl_CmdProc NsTclIncludeCmd;
extern Tcl_CmdProc NsTclAdpEvalCmd;
extern Tcl_CmdProc NsTclAdpParseCmd;
extern Tcl_CmdProc NsTclBreakCmd;
extern Tcl_CmdProc NsTclTellCmd;
extern Tcl_CmdProc NsTclTruncCmd;
extern Tcl_CmdProc NsTclArgcCmd;
extern Tcl_CmdProc NsTclArgvCmd;
extern Tcl_CmdProc NsTclBindCmd;
extern Tcl_CmdProc NsTclDumpCmd;
extern Tcl_CmdProc NsTclExceptionCmd;
extern Tcl_CmdProc NsTclStreamCmd;
extern Tcl_CmdProc NsTclDebugCmd;
extern Tcl_CmdProc NsTclAdpMimeCmd;

extern Tcl_CmdProc NsTclRegisterTagCmd;
extern Tcl_CmdProc NsTclRegisterAdpCmd;

extern Tcl_CmdProc NsTclCacheStatsCmd;
extern Tcl_CmdProc NsTclCacheFlushCmd;
extern Tcl_CmdProc NsTclCacheNamesCmd;
extern Tcl_CmdProc NsTclCacheSizeCmd;

extern Tcl_CmdProc NsTclDbCmd;
extern Tcl_CmdProc NsTclDbConfigPathCmd;
extern Tcl_CmdProc NsTclPoolDescriptionCmd;
extern Tcl_CmdProc NsTclDbErrorCodeCmd;
extern Tcl_CmdProc NsTclDbErrorMsgCmd;
extern Tcl_CmdProc NsTclQuoteListToListCmd;
extern Tcl_CmdProc NsTclGetCsvCmd;
extern Tcl_CmdProc NsTclUnsupDbCmd;

extern Tcl_CmdProc NsTclEnvCmd;
extern Tcl_CmdProc NsTclGetChannelsCmd;

extern Tcl_CmdProc NsTclVGetCmd;
extern Tcl_CmdProc NsTclVSetCmd;
extern Tcl_CmdProc NsTclVArrayCmd;
extern Tcl_CmdProc NsTclVUnsetCmd;
extern Tcl_CmdProc NsTclVIncrCmd;
extern Tcl_CmdProc NsTclVAppendCmd;
extern Tcl_CmdProc NsTclVNamesCmd;

extern Tcl_CmdProc NsTclStatsCmd;

extern char     *nsServer;
extern char     *nsBuildDate;
extern int       nsConfQuiet;

#endif
