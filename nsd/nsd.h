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
#else
#define NS_SIGTERM  SIGTERM
#define NS_SIGHUP   SIGHUP
#endif

#define UCHAR(c)	((unsigned char) (c))

/*
 * constants
 */

#define NSD_NAME             "AOLserver"
#define NSD_VERSION	     "4.0b1"
#define NSD_LABEL            "aolserver4_0"
#define NSD_TAG              "$Name:  $"
#define NS_CONFIG_PARAMETERS "ns/parameters"
#define NS_CONFIG_SERVERS    "ns/servers"
#define NS_CONFIG_THREADS    "ns/threads"

#define ADP_OK       0
#define ADP_BREAK    1
#define ADP_ABORT    2
#define ADP_OVERFLOW 3
#define ADP_RETURN   4

#define Dev         (Debug+1)

/*
 * Typedef definitions.
 */

typedef int bool;

struct _nsconf {
    char    	   *argv0;
    char	   *nsd;
    char           *name;
    char           *version;
    char           *home;
    char           *config;
    char           *build;
    int		    configfmt;
    int             pid;
    time_t          boot_t;
    char            hostname[255];
    char	    address[16];
    int             shutdowntimeout;
    int		    startuptimeout;
    int             backlog;
    Tcl_DString     servers;

    /*
     * The following struct maintains server state.
     */

    struct {
    	Ns_Mutex	lock;
    	Ns_Cond	    	cond;
    	int		started;
    	int		stopping;
    } state;
    
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
	int maxentries;
	int maxsize;
    } dstring;

    struct {
	bool checkexit;
    } exec;

    struct {
	bool enabled;
	int timeout;
	int maxkeep;
	int npending;
    } keepalive;

    struct {
	char *sharedlibrary;
	char *version;
    } tcl;

};

extern struct _nsconf nsconf;


/*
 * This is the internal structure for a comm driver.
 */

typedef struct Driver {
    struct Driver      *nextPtr;
    char	       *server;
    char               *label;
    void               *arg;
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

struct NsServer;

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
    
    struct NsServer *servPtr;
    struct Conn *prevPtr;
    struct Conn *nextPtr;
    int          id;
    char	 idstr[16];
    time_t	 startTime;
    Driver      *drvPtr;
    void        *drvData;
    Ns_Set      *query;
    char	*peer;
    char	 peerBuf[32];
    Tcl_Interp  *interp;
    ConnState    readState;
    ConnState    sendState;
    int          nContent;
    int          nContentSent;
    int          responseStatus;
    int          responseLength;
    int          recursionCount;
    int		 keepAlive;
    void	*cls[NS_CONN_MAXCLS];
} Conn;

/*
 * The following structure maintains new interp init callbacks.
 */

typedef struct Init {
    struct Init *nextPtr;
    Ns_TclInterpInitProc *proc;
    void *arg;
} Init;

/*
 * The following structure defines a collection of arrays.  
 * Only the arrays within a given bucket share a lock,
 * allowing for more concurency in nsv.
 */

typedef struct Bucket {
    Ns_Mutex lock;
    Tcl_HashTable arrays;
} Bucket;

/*
 * The following structure maintains scripts to evaluate
 * when a connection closes.
 */

typedef struct AtClose {
    struct AtClose *nextPtr;
    char script[1];
} AtClose;

/*
 * The following structure maintains callbacks to registered
 * to be executed at interp deallocate time.
 */
 
typedef struct AtCleanup {
    struct AtCleanup *nextPtr;
    Ns_TclDeferProc *procPtr;
    void 	    *arg;
} AtCleanup;

/*
 * The following stuctures maintain connection filters
 * and traces.
 */

typedef struct Filter {
    struct Filter *nextPtr;
    Ns_FilterProc *proc;
    char 	  *method;
    char          *url;
    int	           when;
    void	  *arg;
} Filter;

typedef struct Trace {
    struct Trace    *nextPtr;
    Ns_TraceProc    *proc;
    void    	    *arg;
} Trace;

/*
 * The following structure defines a job queued in a
 * server Tcl job pool.
 */

typedef struct Job {
    struct Job	    *nextPtr;
    int		     flags;
    int              code;
    char	    *errorCode;
    char	    *errorInfo;
    Tcl_DString	     ds;
} Job;

/*
 * The following structure is allocated for each virtual server.
 */

typedef struct NsServer {

    char    	    	   *server;
    
    /* 
     * The following struct maintains various server options.
     */

    struct {
	bool	    	    aolpress;
	bool	    	    flushcontent;
	bool	    	    modsince;
	bool 	    	    noticedetail;
    	char 	    	   *realm;
	Ns_HeaderCaseDisposition hdrcase;
    } opts;
    
    /* 
     * The following struct maintains conn-related limits.
     */

    struct {
    	int 	    	    maxheaders;
	int 	    	    maxline;
	int 	    	    maxpost;
	int	    	    sendfdmin;
    	int 	    	    errorminsize;
    } limits;

    /*
     * The following struct maintains the active and waiting connection
     * queues, the free conn list, the next conn id, and the number
     * of waiting connects.  If yield is set, Ns_QueueConn will yield
     * after adding a conn to the waiting list.
     */

    struct {
    	int 	    	    yield;
	unsigned int	    nextid;
    	Conn	    	   *freePtr;
	struct {
	    int     	    num;
    	    Conn    	   *firstPtr;
    	    Conn    	   *lastPtr;
	} wait;
	struct {
    	    Conn    	   *firstPtr;
	    Conn    	   *lastPtr;
	} active;
	Ns_Mutex    	    lock;
	Ns_Cond     	    cond;
	bool	    	    shutdown;
    } queue;

    /*
     * The following struct maintins the state of the threads.  Min and max
     * threads are determined at startup and then Ns_QueueConn ensure the
     * current number of threads remains within that range with individual
     * threads waiting no more than the timeout for a connection to
     * arrive.  The number of idle threads is maintained for the benefit of
     * the ns_server command. 
     */

    struct {
	unsigned int	    nextid;
	int 	    	    min;
	int 	    	    max;
    	int 	    	    current;
	int 	    	    idle;
	int 	    	    timeout;
	Ns_Thread   	    last;
    } threads;

    struct {
	char	    	   *pageroot;
	char	    	  **dirv;
	int 	    	    dirc;
	char	    	   *dirproc;
	char	    	   *diradp;
	bool	    	    mmap;
	int 	    	    cachemaxentry;
	Ns_UrlToFileProc   *url2file;
	Ns_Cache    	   *cache;
    } fastpath;

    /*
     * The following struct maintains request tables.
     */

    struct {
	Ns_RequestAuthorizeProc *authProc;
	Tcl_HashTable	    redirect;
	Tcl_HashTable	    proxy;
    } request;

    /*
     * The following struct maintains filters and traces.
     */

    struct {
	Filter *firstFilterPtr;
	Trace  *firstTracePtr;
	Trace  *firstCleanupPtr;
    } filter;

    /*
     * The following struct maintains the core Tcl config.
     */

    struct {         
	char	           *library;
	Init               *firstInitPtr;
	char	    	   *initfile;
    } tcl;
    
    /*
     * The following struct maintains ADP config and
     * possibly a shared cache.
     */

    struct {
	char	    	   *errorpage;
	char	    	   *startpage;
	bool	    	    enableexpire;
	bool	    	    enabledebug;
	char	    	   *debuginit;
	char	    	   *defaultparser;
	bool	    	    threadcache;
	int 	    	    cachesize;
	Ns_Cache    	   *cache;
	Tcl_HashTable	    tags;
	Ns_Mutex	    lock;
    } adp;
    
    /*
     * The following struct maintains the Ns_Set's
     * entered into Tcl with NS_TCL_SET_SHARED.
     */

    struct {
	Ns_Mutex    	    lock;
	Tcl_HashTable	    table;
    } sets;    
    
    /*
     * The following struct maintains the arrays
     * for the nsv commands.
     */

    struct {
	Bucket      	   *buckets;
	int 	    	    nbuckets;
    } nsv;
    
    /*
     * The following struct maintains the vars and
     * lock for the old ns_var command.
     */

    struct {
	Ns_Mutex    	    lock;
	Tcl_HashTable	    table;
    } var;
    
    /*
     * The following struct maintains the init state
     * of ns_share variables, updated with the
     * ns_share -init command.
     */

    struct {
	Ns_Cs    	    cs;
	Ns_Mutex    	    lock;
	Ns_Cond     	    cond;
    	Tcl_HashTable	    inits;
    	Tcl_HashTable	    vars;
    } share;

    /*
     * The following struct maintains the default
     * and allowed database pool lists.
     */

    struct {
	char		   *defpool;
	char		   *allowed;
    } db;

    /*
     * The following struct maintains detached Tcl
     * channels for the benefit of the ns_chan command.
     */

    struct {
	Ns_Mutex	    lock;
	Tcl_HashTable	    table;
    } chans;

    /*
     * The following struct maintains the Tcl job queue.
     */

    struct {
	int		    stop;
	Job		   *firstPtr;
	Tcl_HashTable	    table;
	unsigned int	    nextid;
	struct {
	    int		    max;
	    int		    idle;
	    int		    current;
	    unsigned int    next;
	} threads;
	Ns_Mutex	    lock;
	Ns_Cond		    cond;
    } job;

} NsServer;
    
/*
 * The following structure is allocated for each interp.
 */

typedef struct NsInterp {

    Tcl_Interp		  *interp;
    NsServer  	    	  *servPtr;
    bool		   delete;
    Tcl_HashEntry	  *hPtr;

    /*
     * The following pointer maintains the first in
     * a FIFO list of callbacks to invoke at interp
     * de-allocate time.
     */

    AtCleanup		  *firstAtCleanupPtr;

    /*
     * The following pointer maintains the first in
     * a LIFO list of scripts to evaluate when a
     * connection closes.
     */

    AtClose		  *firstAtClosePtr;

    /*
     * The following pointer and struct maintain state for
     * the active connection, if any, and support the ns_conn
     * command.
     */

#define CONN_TCLFORM		1
#define CONN_TCLHDRS		2
#define CONN_TCLOUTHDRS		4

    Ns_Conn		  *conn;

    struct {
	int		   flags;
	char	   	   form[16];
	char	   	   hdrs[16];
	char	   	   outhdrs[16];
    } nsconn;

    /*
     * The following struct maintains per-interp ADP
     * context including the output buffer and
     * eval stack.
     */

    struct {
	bool               stream;
	int	           exception;
	int                depth;
	int                argc;
	char             **argv;
	char		  *file;
	char              *cwd;
	char              *mimetype;
	int                evalLevel;
	int                errorLevel;
	int                debugLevel;
	int                debugInit;
	char              *debugFile;
	Ns_DString	   output;
	Ns_Cache	  *cache;
    } adp;
    
    /*
     * The following table maintains private Ns_Set's
     * entered into this interp.
     */

   Tcl_HashTable sets;
    
    /*
     * The following table maintains allocated
     * database handles.
     */

    Tcl_HashTable dbs;

    /*
     * The following table maintains shared channels
     * register with the ns_chan command.
     */

    Tcl_HashTable chans;

} NsInterp;

/*
 * The following structure is allocated by NsGetTls
 * for each thread.
 */

typedef struct NsTls {
    struct {
	Tcl_HashTable	    owned;
    } db;

    struct {
	Ns_DString	   *firstPtr;
	int		    ncached;
    } dstring;

    /*
     * The following struct maintains a table of
     * Tcl interps keyed by virtual server.
     */

    struct {
	Tcl_HashTable	    interps;
    } tcl;

#ifdef WIN32
    struct {
	char		    errmsg[32];
    } win;
#endif

} NsTls;

extern NsTls    *NsGetTls(void);
extern NsServer *NsGetServer(char *server);
extern NsInterp *NsGetInterp(Tcl_Interp *interp);

extern Ns_OpProc NsFastGet;
extern Ns_OpProc NsAdpProc;

extern Ns_Cache *NsFastpathCache(char *server, int size);

extern void NsFreeSets(NsInterp *itPtr);
extern void NsFreeDbs(NsInterp *itPtr);
extern void NsFreeAdp(NsInterp *itPtr);
extern void NsFreeAtClose(NsInterp *itPtr);

extern void NsRunAtClose(Tcl_Interp *interp);

extern int NsUrlToFile(Ns_DString *dsPtr, NsServer *servPtr, char *url);

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
extern int NsInstallService(char *server);
extern int NsRemoveService(char *server);
#endif

extern void NsInitBinder(char *args, char *file);
extern void NsForkBinder(void);
extern void NsStopBinder(void);

extern void NsClsCleanup(Conn *connPtr);

extern void NsInitMimeTypes(void);
extern void NsLogOpen(void);
extern void NsDbInitPools(void);

extern void NsInitServer(Ns_ServerInitProc *proc, char *server);
extern void NsDbInitServer(char *server);
extern void NsTclInitServer(char *server);
extern void NsLoadModules(char *server);

extern void NsEnableDNSCache(int timeout);

extern void NsCreatePidFile(char *server);
extern void NsRemovePidFile(char *server);
extern int  NsGetLastPid(char *server);
extern void NsKillPid(int pid);
extern void NsBlockSignals(int debug);
extern void NsHandleSignals(void);
extern void NsRestoreSignals(void);
extern void NsSendSignal(int sig);
extern void NsShutdown(int timeout);

extern void NsStartServers(void);
extern void NsStopServers(Ns_Time *toPtr);
extern void NsStartServer(NsServer *servPtr);
extern void NsStopServer(NsServer *servPtr);
extern void NsWaitServer(NsServer *servPtr, Ns_Time *toPtr);

extern void NsStartDrivers(char *server);
extern void NsWaitServerWarmup(Ns_Time *);
extern void NsWaitSockIdle(Ns_Time *);
extern void NsWaitSchedIdle(Ns_Time *);

extern void NsStopDrivers(void);
extern void NsStartKeepAlive(void);
extern void NsStopKeepAlive(void);

extern void NsStartSchedShutdown(void);
extern void NsWaitSchedShutdown(Ns_Time *toPtr);

extern void NsStartSockShutdown(void); 
extern void NsWaitSockShutdown(Ns_Time *toPtr);

extern void NsStartShutdownProcs(void);
extern void NsWaitShutdownProcs(Ns_Time *toPtr);

extern void NsTclStopJobs(NsServer *servPtr);
extern void NsTclWaitJobs(NsServer *servPtr, Ns_Time *toPtr);

/*
 * ADP routines.
 */

extern Ns_Cache *NsAdpCache(char *server, int size);
extern void NsAdpParse(NsServer *servPtr, Ns_DString *outPtr, char *in);
extern void NsAdpSetMimeType(NsInterp *itPtr, char *type);
extern void NsAdpFlush(NsInterp *itPtr);
extern void NsAdpStream(NsInterp *itPtr);
extern int NsAdpDebug(NsInterp *itPtr, char *host, char *port, char *procs);
extern int NsAdpEval(NsInterp *itPtr, char *script, int argc, char **argv);
extern int NsAdpSource(NsInterp *itPtr, char *file, int argc, char **argv);
extern int NsAdpInclude(NsInterp *itPtr, char *file, int argc, char **argv);

/*
 * Database routines.
 */

extern void 		NsDbClose(Ns_DbHandle *);
extern void 		NsDbDisconnect(Ns_DbHandle *);
extern struct DbDriver *NsDbGetDriver(Ns_DbHandle *);
extern struct DbDriver *NsDbLoadDriver(char *driver);
extern void 		NsDbLogSql(Ns_DbHandle *, char *sql);
extern int 		NsDbOpen(Ns_DbHandle *);
extern void 		NsDbDriverInit(char *server, struct DbDriver *);

/*
 * Tcl support routines.
 */

extern Ns_TclInterpInitProc NsTclCreateCmds;
extern char 	  *NsTclConnId(Ns_Conn *conn);
extern int 	   NsIsIdConn(char *inID);
extern int 	   NsTclEval(Tcl_Interp *interp, char *script);
extern void 	   NsTclCreateGenericCmds(Tcl_Interp *);
extern int	   NsTclShareVar(Tcl_Interp *interp, char *varName);

/*
 * Callback routines.
 */

extern int  NsRunFilters(Ns_Conn *conn, int why);
extern void NsRunCleanups(Ns_Conn *conn);
extern void NsRunTraces(Ns_Conn *conn);
extern void NsRunPreStartupProcs(void);
extern void NsRunSignalProcs(void);
extern void NsRunStartupProcs(void);
extern void NsRunAtReadyProcs(void);
extern void NsRunAtExitProcs(void);


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

/*
 * Proxy support
 */

extern int NsConnRunProxyRequest(Ns_Conn *conn);

/*
 * AOLserver Tcl commands
 */

extern void NsTclAddCmds(NsInterp *itPtr, Tcl_Interp *interp);

extern Tcl_CmdProc NsTclJobCmd;
extern Tcl_CmdProc NsTclChanCmd;
extern Tcl_CmdProc NsTclAfterCmd;
extern Tcl_CmdProc NsTclAtCloseCmd;
extern Tcl_CmdProc NsTclAtExitCmd;
extern Tcl_CmdProc NsTclAtShutdownCmd;
extern Tcl_CmdProc NsTclAtSignalCmd;
extern Tcl_CmdProc NsTclSchedCmd;
extern Tcl_CmdProc NsTclSchedDailyCmd;
extern Tcl_CmdProc NsTclSchedWeeklyCmd;
extern Tcl_CmdProc NsTclCancelCmd;
extern Tcl_CmdProc NsTclPauseCmd;
extern Tcl_CmdProc NsTclResumeCmd;
extern Tcl_CmdProc NsTclUnscheduleCmd;
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
extern Tcl_CmdProc NsTclPurgeFilesCmd;
extern Tcl_CmdProc NsTclSelectCmd;
extern Tcl_CmdProc NsTclSemaCmd;
extern Tcl_CmdProc NsTclSetCmd;
extern Tcl_CmdProc NsTclReturnNotFoundCmd;
extern Tcl_CmdProc NsTclReturnUnauthorizedCmd;
extern Tcl_CmdProc NsTclReturnForbiddenCmd;
extern Tcl_CmdProc NsTclGetHostCmd;
extern Tcl_CmdProc NsTclGetAddrCmd;
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

extern Tcl_CmdProc NsTclRegisterTagCmd;
extern Tcl_CmdProc NsTclRegisterAdpCmd;

extern Tcl_CmdProc NsTclCacheStatsCmd;
extern Tcl_CmdProc NsTclCacheFlushCmd;
extern Tcl_CmdProc NsTclCacheNamesCmd;
extern Tcl_CmdProc NsTclCacheSizeCmd;
extern Tcl_CmdProc NsTclCacheKeysCmd;
extern Tcl_CmdProc NsTclDbCmd;
extern Tcl_CmdProc NsTclDbConfigPathCmd;
extern Tcl_CmdProc NsTclPoolDescriptionCmd;
extern Tcl_CmdProc NsTclDbErrorCodeCmd;
extern Tcl_CmdProc NsTclDbErrorMsgCmd;
extern Tcl_CmdProc NsTclQuoteListToListCmd;
extern Tcl_CmdProc NsTclGetCsvCmd;
extern Tcl_CmdProc NsTclEnvCmd;

extern Tcl_CmdProc NsTclAdpEvalCmd;
extern Tcl_CmdProc NsTclAdpIncludeCmd;
extern Tcl_CmdProc NsTclAdpPutsCmd;
extern Tcl_CmdProc NsTclAdpDirCmd;
extern Tcl_CmdProc NsTclAdpReturnCmd;
extern Tcl_CmdProc NsTclAdpBreakCmd;
extern Tcl_CmdProc NsTclAdpAbortCmd;
extern Tcl_CmdProc NsTclAdpTellCmd;
extern Tcl_CmdProc NsTclAdpTruncCmd;
extern Tcl_CmdProc NsTclAdpDumpCmd;
extern Tcl_CmdProc NsTclAdpArgcCmd;
extern Tcl_CmdProc NsTclAdpArgvCmd;
extern Tcl_CmdProc NsTclAdpBindArgsCmd;
extern Tcl_CmdProc NsTclAdpExceptionCmd;
extern Tcl_CmdProc NsTclAdpStreamCmd;
extern Tcl_CmdProc NsTclAdpDebugCmd;
extern Tcl_CmdProc NsTclAdpParseCmd;
extern Tcl_CmdProc NsTclAdpMimeCmd;

extern Tcl_CmdProc NsTclNsvGetCmd;
extern Tcl_CmdProc NsTclNsvExistsCmd;
extern Tcl_CmdProc NsTclNsvSetCmd;
extern Tcl_CmdProc NsTclNsvIncrCmd;
extern Tcl_CmdProc NsTclNsvAppendCmd;
extern Tcl_CmdProc NsTclNsvLappendCmd;
extern Tcl_CmdProc NsTclNsvArrayCmd;
extern Tcl_CmdProc NsTclNsvUnsetCmd;
extern Tcl_CmdProc NsTclNsvNamesCmd;
extern Tcl_CmdProc NsTclVarCmd;
extern Tcl_CmdProc NsTclShareCmd;

#endif

