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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "ns.h"
#ifndef _WIN32
#include <pthread.h>
#endif
#include <assert.h>

#ifdef _WIN32

#include <fcntl.h>
#include <io.h>
#define STDOUT_FILENO	1
#define STDERR_FILENO	2
#define S_ISREG(m)	((m)&_S_IFREG)
#define S_ISDIR(m)	((m)&_S_IFDIR)
#include "getopt.h"
#include <sys/stat.h>

#else

#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <ctype.h>

#endif	/* WIN32 */

#ifdef HAVE_POLL
  #include <poll.h>
#else
  #define POLLIN 1
  #define POLLOUT 2
  #define POLLPRI 3
  struct pollfd {
    int fd;
    short events;
    short revents;
  };
  extern int poll(struct pollfd *, unsigned long, int);
#endif

#ifdef __hp
  #define seteuid(i)     setresuid((-1),(i),(-1))
#endif
#ifdef __sun
  #include <sys/filio.h>
  #include <sys/systeminfo.h>
  #define gethostname(b,s) (!(sysinfo(SI_HOSTNAME, b, s) > 0))
#endif
#ifdef __unixware
  #include <sys/filio.h>
#endif

#ifndef F_CLOEXEC
#define F_CLOEXEC 1
#endif

#ifdef _WIN32
#define NS_SIGTERM  1
#define NS_SIGHUP   2
#else
#define NS_SIGTERM  SIGTERM
#define NS_SIGHUP   SIGHUP
#endif

/*
 * constants
 */

#define NSD_NAME             "AOLserver"
#define NSD_VERSION	     NS_PATCH_LEVEL
#define NSD_LABEL            "aolserver4_0"
#define NSD_TAG              "$Name:  $"
#define NS_CONFIG_PARAMETERS "ns/parameters"
#define NS_CONFIG_THREADS    "ns/threads"

#define ADP_OK       0
#define ADP_BREAK    1
#define ADP_ABORT    2
#define ADP_RETURN   4

#define LOG_ROLL	1
#define LOG_EXPAND	2
#define LOG_DEBUG	4
#define LOG_DEV		8
#define LOG_NONOTICE	16
#define LOG_USEC	32

/*
 * The following is the default text/html content type
 * sent to the browsers.  The charset is also used for
 * both input (url query) and output encodings.
 */

#define NSD_TEXTHTML    "text/html; charset=iso-8859-1"

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
    int             pid;
    time_t          boot_t;
    char            hostname[255];
    char	    address[16];
    int             shutdowntimeout;
    int             backlog;

    /*
     * The following table holds the configured virtual servers.  The
     * dstring maintains a Tcl list of the names.
     */

    Tcl_HashTable   servertable;
    Tcl_DString     servers;

    /*
     * The following table holds config section sets from
     * the config file.
     */

    Tcl_HashTable   sections;

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
	char *file;
	int  flags;
	int  maxlevel;
	int  maxback;
	int  maxbuffer;
	int  flushint;
    } log;

    struct {
	int maxelapsed;
    } sched;

#ifdef _WIN32    
    struct {
	bool checkexit;
    } exec;
#endif

    struct {
	bool enabled;
	int timeout;
	int maxkeep;
	int npending;
	int allmethods;
    } keepalive;

    struct {
	char *sharedlibrary;
	char *version;
        bool lockoninit;
    } tcl;

    struct {
        char         *outputCharset;
        Tcl_Encoding  outputEncoding;
        bool          hackContentTypeP;
        char         *urlCharset;
        Tcl_Encoding  urlEncoding;
    } encoding;

};

extern struct _nsconf nsconf;

/*
 * The following structure defines a key for hashing
 * a file by device/inode.
 */

typedef struct FileKey {
    dev_t dev;
    ino_t ino;
} FileKey;

#define FILE_KEYS (sizeof(FileKey)/sizeof(int))

/*
 * The following structure defines blocks of ADP.  The
 * len pointer is an array of ints with positive values
 * indicating text to copy and negative values indicating
 * scripts to evaluate.  The text and script chars are
 * packed together without null char separators starting
 * at base.  The len and base data are either stored
 * in an AdpParse structure or copied at the end of
 * a cached Page structure.
 */

typedef struct AdpCode {
    int		nblocks;
    int		nscripts;
    char       *base;
    int	       *len;
} AdpCode;

/*
 * The following structure is used to accumulate the 
 * results of parsing an ADP string.
 */

typedef struct AdpParse {
    AdpCode	code;
    Tcl_DString hdr;
    Tcl_DString text;
} AdpParse;

/*
 * The following structure defines the entire request
 * including HTTP request line, headers, and content.
 */

typedef struct Request {
    struct Request *nextPtr;	/* Next on free list. */
    Ns_Request	   *request;	/* Parsed request line. */
    Ns_Set	   *headers;	/* Input headers. */
    char	    peer[16];	/* Client peer address. */
    int		    port;	/* Client peer port. */

    /*
     * The following pointers are used to access the
     * buffer contents after the read-ahead is complete.
     */

    char	   *next;	/* Next read offset. */
    char	   *content;	/* Start of content. */
    int		    length;	/* Length of content. */
    int		    avail;	/* Bytes avail in buffer. */
    int		    leadblanks;	/* # of leading blank lines read */

    /*
     * The following offsets are used to manage the 
     * buffer read-ahead process.
     */

    int		    woff;	/* Next write buffer offset. */
    int		    roff;	/* Next read buffer offset. */
    int		    coff;	/* Content buffer offset. */

    Tcl_DString	    buffer;	/* Request and content buffer. */

} Request;

/*
 * The following structure maitains data for each instance of
 * a driver initialized with Ns_DriverInit.
 */

struct NsServer;

typedef struct Driver {

    /*
     * Visible in Ns_Driver.
     */

    void	*arg;		    /* Driver callback data. */
    char	*server;	    /* Virtual server name. */
    char	*module;	    /* Driver module. */
    char        *name;		    /* Driver name. */
    char        *location;	    /* Location, e.g, "http://foo:9090" */
    char        *address;	    /* Address in location. */
    int     	 sendwait;	    /* send() I/O timeout. */
    int     	 recvwait;	    /* recv() I/O timeout. */
    int		 bufsize;	    /* Conn bufsize (0 for SSL) */
    int		 sndbuf;	    /* setsockopt() SNDBUF option. */
    int		 rcvbuf;	    /* setsockopt() RCVBUF option. */

    /*
     * Private to Driver.
     */

    struct Driver *nextPtr;	    /* Next in list of drivers. */
    struct NsServer *servPtr;	    /* Driver virtual server. */
    Ns_DriverProc *proc;	    /* Driver callback. */
    int		 opts;		    /* Driver options. */
    int     	 closewait;	    /* Graceful close timeout. */
    int     	 keepwait;	    /* Keepalive timeout. */
    SOCKET		 sock;		    /* Listening socket. */
    int		 pidx;		    /* poll() index. */
    char        *bindaddr;	    /* Numerical listen address. */
    int          port;		    /* Port in location. */
    int		 backlog;	    /* listen() backlog. */

    int          maxinput;          /* Maximum request bytes to read. */
    unsigned int loggingFlags;      /* Logging control flags */

} Driver;

/*
 * The following structure maintains a socket to a
 * connected client.  The socket is used to maintain state
 * during request read-ahead before connection processing
 * and keepalive after connection processing.
 */

typedef struct Sock {

    /*
     * Visible in Ns_Sock.
     */

    struct Driver *drvPtr;
    SOCKET     sock;
    void      *arg;

    /*
     * Private to Sock.
     */

    struct Sock *nextPtr;
    struct NsServer *servPtr;
    char *location;
    struct sockaddr_in sa;
    int		 keep;
    int		 pidx;		    /* poll() index. */
    Ns_Time	 timeout;
    Request	*reqPtr;

} Sock;

/*
 * The following structure maintains data from an
 * updated form file.
 */

typedef struct FormFile {
    Ns_Set *hdrs;
    off_t   off;
    off_t   len;
} FormFile;

/*
 * The following structure maintains state for a connection
 * being processed.
 */

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
    struct Sock *sockPtr;

    /*
     * The following are copied from sockPtr so they're valid
     * after the connection is closed (e.g., within traces).
     */

    char *server;
    char *location;
    struct Request *reqPtr;
    struct NsServer *servPtr;
    struct Driver *drvPtr;

    int          id;
    char	 idstr[16];
    Ns_Time	 startTime;
    Tcl_Interp  *interp;
    Tcl_Encoding encoding;
    Tcl_Encoding urlEncoding;
    int          nContentSent;
    int          responseStatus;
    int          responseLength;
    int          recursionCount;
    Ns_Set      *query;
    Tcl_HashTable files;
    Tcl_DString	 queued;
    void	*cls[NS_CONN_MAXCLS];
} Conn;

/*
 * The following structure maintains a connection thread pool.
 */

typedef struct ConnPool {
    char 	    *pool;
    struct ConnPool *nextPtr;
    struct NsServer *servPtr;

    /*
     * The following struct maintains the active and waiting connection
     * queues, the free conn list, the next conn id, and the number
     * of waiting connects.
     */

    struct {
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
	Ns_Cond     	    cond;
    } queue;

    /*
     * The following struct maintins the state of the threads.  Min and max
     * threads are determined at startup and then NsQueueConn ensures the
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
    } threads;

} ConnPool;

/*
 * The following structure is allocated for each virtual server.
 */

typedef struct NsServer {
    char    	    	   *server;
    Ns_LocationProc 	   *locationProc;

    /*
     * The following struct maintains the connection pool(s).
     */

    struct {
	Ns_Mutex    	    lock;
	int		    nextconnid;
	bool		    shutdown;
    	ConnPool	   *firstPtr;
    	ConnPool	   *defaultPtr;
	Ns_Thread   	    joinThread;
    } pools;
    
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
     * Encoding defaults for the server
     */
    struct {
        char         *outputCharset;
        Tcl_Encoding  outputEncoding;
        bool          hackContentTypeP;
        char         *urlCharset;
        Tcl_Encoding  urlEncoding;
    } encoding;

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
	Ns_Mutex	    plock;
    } request;

    /*
     * The following struct maintains filters and traces.
     */

    struct {
	struct Filter *firstFilterPtr;
	struct Trace  *firstTracePtr;
	struct Trace  *firstCleanupPtr;
    } filter;

    /*
     * The following struct maintains the core Tcl config.
     */

    struct {         
	char	           *library;
	struct Trace       *traces[4];
	char	    	   *initfile;
	Ns_RWLock	    lock;
	char		   *script;
	int		    length;
	int		    epoch;
	Tcl_Obj		   *modules;
    } tcl;
    
    /*
     * The following struct maintains ADP config,
     * registered tags, and read-only page text.
     */

    struct {
	char	    	   *errorpage;
	char	    	   *startpage;
	bool	    	    enableexpire;
	bool	    	    enabledebug;
	char	    	   *debuginit;
	char	    	   *defaultparser;
	size_t 	    	    cachesize;
	Ns_Cond	    	    pagecond;
	Ns_Mutex	    pagelock;
	Tcl_HashTable	    pages;
	Ns_RWLock	    taglock;
	Tcl_HashTable	    tags;
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
	struct Bucket  	   *buckets;
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
     * The following struct maintains detached Tcl
     * channels for the benefit of the ns_chan command.
     */

    struct {
	Ns_Mutex	    lock;
	Tcl_HashTable	    table;
    } chans;

} NsServer;
    
/*
 * The following structure is allocated for each interp.
 */

typedef struct NsInterp {

    struct NsInterp	  *nextPtr;
    Tcl_Interp		  *interp;
    NsServer  	    	  *servPtr;
    int		   	   delete;
    int			   epoch;

    /*
     * The following pointer maintains the first in
     * a FIFO list of callbacks to invoke at interp
     * de-allocate time.
     */

    struct Defer	 *firstDeferPtr;

    /*
     * The following pointer maintains the first in
     * a LIFO list of scripts to evaluate when a
     * connection closes.
     */

    struct AtClose	*firstAtClosePtr;

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
     * context including the private pages cache.
     */

    struct {
	bool               stream;
	int	           exception;
	int                depth;
	int                objc;
	Tcl_Obj		 **objv;
	char              *cwd;
	int                errorLevel;
	int                debugLevel;
	int                debugInit;
	char              *debugFile;
	Ns_Cache	  *cache;
	Tcl_DString	  *outputPtr;
	Tcl_DString	  *responsePtr;
	Tcl_DString	  *typePtr;
    } adp;
    
    /*
     * The following table maintains private Ns_Set's
     * entered into this interp.
     */

    Tcl_HashTable sets;
    
    /*
     * The following table maintains shared channels
     * register with the ns_chan command.
     */

    Tcl_HashTable chans;

    /*
     * The following table maintains the Tcl HTTP requests.
     */

    Tcl_HashTable https;

} NsInterp;

/*
 * Libnsd initialization routines.
 */

extern void NsInitBinder(void);
extern void NsInitCache(void);
extern void NsInitConf(void);
extern void NsInitEncodings(void);
extern void NsInitListen(void);
extern void NsInitLog(void);
extern void NsInitMimeTypes(void);
extern void NsInitModLoad(void);
extern void NsInitProcInfo(void);
extern void NsInitQueue(void);
extern void NsInitDrivers(void);
extern void NsInitSched(void);
extern void NsInitTcl(void);
extern void NsInitUrlSpace(void);
extern void NsInitRequests(void);

extern int NsQueueConn(Sock *sockPtr, Ns_Time *nowPtr);
extern void NsMapPool(ConnPool *poolPtr, char *map);
extern int NsSockSend(Sock *sockPtr, struct iovec *bufs, int nbufs);
extern void NsSockClose(Sock *sockPtr, int keep);

extern Request *NsGetRequest(Sock *sockPtr);
extern void NsFreeRequest(Request *reqPtr);

extern NsServer *NsGetServer(char *server);
extern NsServer *NsGetInitServer(void);
extern NsInterp *NsGetInterp(Tcl_Interp *interp);

extern Ns_OpProc NsFastGet;
extern Ns_OpProc NsAdpProc;

extern Ns_Cache *NsFastpathCache(char *server, int size);

extern void NsFreeAdp(NsInterp *itPtr);
extern void NsFreeAtClose(NsInterp *itPtr);

extern void NsRunAtClose(Tcl_Interp *interp);

extern int NsUrlToFile(Ns_DString *dsPtr, NsServer *servPtr, char *url);

/*
 * External callback functions.
 */

extern Ns_Callback NsTclCallback;
extern Ns_Callback NsTclSignalProc;
extern Ns_SchedProc NsTclSchedProc;
extern Ns_ArgProc NsTclArgProc;
extern Ns_ThreadProc NsTclThread;
extern Ns_ArgProc NsTclThreadArgProc;
extern Ns_Callback NsCachePurge;
extern Ns_ArgProc NsCacheArgProc;
extern Ns_SockProc NsTclSockProc;
extern Ns_ArgProc NsTclSockArgProc;
extern Ns_ThreadProc NsConnThread;
extern Ns_ArgProc NsConnArgProc;

extern void NsGetCallbacks(Tcl_DString *dsPtr);
extern void NsGetSockCallbacks(Tcl_DString *dsPtr);
extern void NsGetScheduled(Tcl_DString *dsPtr);

#ifdef _WIN32
extern int NsConnectService(void);
extern int NsInstallService(char *service);
extern int NsRemoveService(char *service);
#endif

extern void NsCreatePidFile(char *service);
extern void NsRemovePidFile(char *service);

extern void NsLogOpen(void);
extern void NsTclInitObjs(void);
extern void NsUpdateMimeTypes(void);
extern void NsUpdateEncodings(void);
extern void NsUpdateUrlEncode(void);
extern void NsRunPreStartupProcs(void);
extern void NsStartServers(void);
extern void NsBlockSignals(int debug);
extern void NsHandleSignals(void);
extern void NsStopDrivers(void);
extern void NsPreBind(char *bindargs, char *bindfile);
extern void NsClosePreBound(void);
extern void NsInitServer(char *server, Ns_ServerInitProc *initProc);
extern char *NsConfigRead(char *file);
extern void NsConfigEval(char *config, int argc, char **argv, int optind);
extern void NsConfUpdate(void);
extern void NsEnableDNSCache(int timeout, int maxentries);
extern void NsStopServers(Ns_Time *toPtr);
extern void NsStartServer(NsServer *servPtr);
extern void NsStopServer(NsServer *servPtr);
extern void NsWaitServer(NsServer *servPtr, Ns_Time *toPtr);
extern void NsStartDrivers(void);
extern void NsWaitDriversShutdown(Ns_Time *toPtr);
extern void NsStartSchedShutdown(void);
extern void NsWaitSchedShutdown(Ns_Time *toPtr);
extern void NsStartSockShutdown(void); 
extern void NsWaitSockShutdown(Ns_Time *toPtr);
extern void NsStartShutdownProcs(void);
extern void NsWaitShutdownProcs(Ns_Time *toPtr);

extern void NsStartJobsShutdown(void);
extern void NsWaitJobsShutdown(Ns_Time *toPtr);

extern void NsTclInitServer(char *server);
extern void NsLoadModules(char *server);
extern struct Bucket *NsTclCreateBuckets(char *server, int nbuckets);

extern void NsClsCleanup(Conn *connPtr);
extern void NsTclAddCmds(Tcl_Interp *interp, NsInterp *itPtr);
extern void NsTclAddServerCmds(Tcl_Interp *interp, NsInterp *itPtr);

extern void NsRestoreSignals(void);
extern void NsSendSignal(int sig);

/*
 * ADP routines.
 */

extern Ns_Cache *NsAdpCache(char *server, int size);
extern void NsAdpSetMimeType(NsInterp *itPtr, char *type);
extern void NsAdpSetCharSet(NsInterp *itPtr, char *charset);
extern void NsAdpFlush(NsInterp *itPtr);
extern void NsAdpStream(NsInterp *itPtr);
extern int NsAdpDebug(NsInterp *itPtr, char *host, char *port, char *procs);
extern int NsAdpEval(NsInterp *itPtr, int objc, Tcl_Obj *objv[], int safe,
                     char *resvar);
extern int NsAdpSource(NsInterp *itPtr, int objc, Tcl_Obj *objv[],
                       char *resvar);
extern int NsAdpInclude(NsInterp *itPtr, char *file, int objc, Tcl_Obj *objv[]);
extern void NsAdpParse(AdpParse *parsePtr, NsServer *servPtr, char *utf, int safe);

/*
 * Tcl support routines.
 */

extern Ns_TclInterpInitProc NsTclCreateCmds;
extern char 	  *NsTclConnId(Ns_Conn *conn);
extern int 	   NsIsIdConn(char *inID);
extern void	   NsTclInitQueueType(void);
extern void	   NsTclInitAddrType(void);
extern void	   NsTclInitTimeType(void);

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
#ifndef _WIN32
extern int  Ns_ConnRunRequest(Ns_Conn *conn);
extern int  Ns_GetGid(char *group);
extern int  Ns_GetUserGid(char *user);
extern int  Ns_TclGetOpenFd(Tcl_Interp *, char *, int write, int *fp);
#endif
extern void NsStopSockCallbacks(void);
extern void NsStopScheduledProcs(void);
extern void NsGetBuf(char **bufPtr, int *sizePtr);
extern Tcl_Encoding NsGetTypeEncodingWithDef(char *type, int *used_default);
extern void NsComputeEncodingFromType(char *type, Tcl_Encoding *enc,
                                      int *new_type, Tcl_DString *type_ds);

/*
 * Proxy support
 */

extern int NsConnRunProxyRequest(Ns_Conn *conn);

#endif
