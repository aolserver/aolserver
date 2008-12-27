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

#define NSD_EXPORTS
#include "ns.h"

#ifndef _WIN32
#include <pthread.h>
#include <sys/mman.h>
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
#include <grp.h>

#endif	/* WIN32 */

#ifdef HAVE_POLL
  #include <poll.h>
#else
  #define POLLIN 1
  #define POLLOUT 2
  #define POLLPRI 4
  #define POLLHUP 8
  struct pollfd {
    int fd;
    short events;
    short revents;
  };
  extern int poll(struct pollfd *, unsigned long, int);
#endif

#ifdef __linux
  #include <sys/prctl.h>
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

#define _MAX(x,y) ((x) > (y) ? (x) : (y))
#define _MIN(x,y) ((x) > (y) ? (y) : (x))

/*
 * constants
 */

#define NSD_NAME             "AOLserver"
#define NSD_VERSION	     NS_PATCH_LEVEL
#define NSD_LABEL            "aolserver4_5"
#define NSD_TAG              "$Name:  $"
#define NS_CONFIG_PARAMETERS "ns/parameters"
#define NS_CONFIG_THREADS    "ns/threads"

#define ADP_OK       0
#define ADP_BREAK    1
#define ADP_ABORT    2
#define ADP_RETURN   4

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
    int             debug;

    /*
     * The following struct maintains server state.
     */

    struct {
    	Ns_Mutex	lock;
    	Ns_Cond	    	cond;
    	int		started;
    	int		stopping;
    } state;

    /*
     * The following struct is the maximum HTTP major/minor version
     * supported.
     */

    struct {
	unsigned int major;
	unsigned int minor;
    } http;
    
    struct {
	int maxelapsed;
    } sched;

#ifdef _WIN32    
    struct {
	bool checkexit;
    } exec;
#endif

    struct {
	char *sharedlibrary;
	char *version;
        bool lockoninit;
    } tcl;

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
 * The following structure maintains an ADP call frame.
 */

typedef struct AdpFrame {
    struct AdpFrame   *prevPtr;
    int		       line;
    int                objc;
    Tcl_Obj	      *ident;
    Tcl_Obj          **objv;
    char	      *savecwd;
    char	      *file;
    Ns_DString         cwdbuf;
    Tcl_DString	      *outputPtr;
} AdpFrame;

/*
 * The following structure defines blocks of ADP.  The
 * len pointer is an array of ints with positive values
 * indicating text to copy and negative values indicating
 * scripts to evaluate.  The text and script chars are
 * packed together without null char separators starting
 * at base.  The len data is stored at the end of the
 * text dstring when parsing is complete.
 */

typedef struct AdpCode {
    int		nblocks;
    int		nscripts;
    int	       *len;
    int	       *line;
    Tcl_DString text;
} AdpCode;

#define AdpCodeLen(cp,i)	((cp)->len[(i)])
#define AdpCodeLine(cp,i)	((cp)->line[(i)])
#define AdpCodeText(cp)		((cp)->text.string)
#define AdpCodeBlocks(cp)	((cp)->nblocks)
#define AdpCodeScripts(cp)	((cp)->nscripts)

/*
 * Various ADP option bits.
 */

#define ADP_SAFE	0x01	/* Use Tcl_SafeEval for ADP. */
#define ADP_SINGLE	0x02	/* Combine blocks into a single script. */
#define ADP_DEBUG	0x04	/* Enable debugging. */
#define ADP_EXPIRE	0x08	/* Send Expires: now header on output. */
#define ADP_NOCACHE	0x10	/* Disable caching. */
#define ADP_TRACE	0x20	/* Trace execution. */
#define ADP_GZIP	0x80	/* Enable gzip compression. */
#define ADP_DETAIL	0x100	/* Log connection details on error. */
#define ADP_STRICT	0x200	/* Strict error handling. */
#define ADP_DISPLAY	0x400	/* Display error messages in output stream. */
#define ADP_TRIM	0x800	/* Display error messages in output stream. */
#define ADP_FLUSHED	0x1000	/* Some output has been sent. */
#define ADP_ERRLOGGED	0x2000	/* Error message has already been logged. */
#define ADP_AUTOABORT	0x4000	/* Raise abort on flush error. */
#define ADP_EVAL_FILE	0x8000	/* Object to evaluate is a file. */

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
    char        *name;		    /* Driver name, e.g., "nssock". */
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
    char	*fullname;	    /* Full name, i.e., server/module. */
    int          flags;             /* Driver state flags. */
    Ns_Thread	 thread;	    /* Thread id to join on shutdown. */
    Ns_Mutex	 lock;		    /* Lock to protect lists below. */
    Ns_Cond 	 cond;		    /* Cond to signal reader threads,
				     * driver query, startup, and shutdown. */
    int     	 trigger[2];	    /* Wakeup trigger pipe. */

    Ns_DriverProc *proc;	    /* Driver callback. */
    int		 opts;		    /* Driver options. */

    int     	 closewait;	    /* Graceful close timeout. */
    int     	 keepwait;	    /* Keepalive timeout. */
    char        *bindaddr;	    /* Numerical listen address. */
    int          port;		    /* Port in location. */
    int		 backlog;	    /* listen() backlog. */
    
    int          maxline;           /* Maximum request line length to read. */
    int          maxheader;         /* Maximum total header length to read. */
    int		 maxinput;	    /* Maximum request bytes to read. */

    struct Sock *freeSockPtr;       /* Sock free list. */
    int     	 maxsock;	    /* Maximum open Sock's. */
    int     	 nactive;	    /* Number of active Sock's. */
    unsigned int nextid;	    /* Next sock unique id. */

    Ns_Thread   *readers;	    /* Array of reader Ns_Thread's. */
    int		 maxreaders;	    /* Max reader threads. */
    int		 nreaders;	    /* Current num reader threads. */
    int		 idlereaders;	    /* Idle reader threads. */

    struct Sock *readSockPtr;       /* Sock's waiting for reader threads. */
    struct Sock *runSockPtr;        /* Sock's returning from reader threads. */
    struct Sock *closeSockPtr;      /* Sock's returning from conn threads. */

    struct Conn *firstConnPtr;      /* First Conn waiting to run. */
    struct Conn *lastConnPtr;       /* Last Conn waiting to run. */
    struct Conn *freeConnPtr;       /* Conn's returning from conn threads. */

    struct QueWait *freeQueWaitPtr;

    Tcl_DString *queryPtr;	    /* Buffer to copy driver query data. */

    struct {
        unsigned int spins;
        unsigned int accepts;
        unsigned int reads;
        unsigned int writes;
        unsigned int queued;
        unsigned int timeout;
        unsigned int overflow;
        unsigned int dropped;
    } stats;
    
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
    struct Conn *connPtr;
    struct sockaddr_in sa;
    unsigned int id;
    int		 state;
    int		 pidx;		    /* poll() index. */
    Ns_Time      acceptTime;
    Ns_Time	 timeout;
    unsigned int nreads;
    unsigned int nwrites;
} Sock;

/*
 * The following structure defines a per-request limits.
 */

typedef struct Limits {
    Ns_Mutex        lock;
    char           *name;
    unsigned int    maxrun;
    unsigned int    maxwait;
    unsigned int    nrunning;
    unsigned int    nwaiting;
    unsigned int    ndropped;
    unsigned int    noverflow;
    unsigned int    ntimeout;
    size_t	    maxupload;
    int             timeout;
} Limits;

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
    
    struct Conn *nextPtr;
    struct Conn *prevPtr;
    struct Sock *sockPtr;
    struct Limits *limitsPtr;

    /*
     * Client http major/minor version number.
     */

    unsigned int major;
    unsigned int minor;

    /*
     * Start and end of request line for later parsing.
     */

    char *rstart;
    char *rend;

    /*
     * The following are copied from sockPtr so they're valid
     * after the connection is closed (e.g., within traces).
     */

    char *server;
    char *location;
    struct NsServer *servPtr;
    struct Driver *drvPtr;

    unsigned int id;
    char	 idstr[16];
    struct {
        Ns_Time  accept;
        Ns_Time  read;
        Ns_Time  ready;
        Ns_Time  queue;
        Ns_Time  run;
        Ns_Time  close;
        Ns_Time  done;
    } times;
    struct NsInterp *itPtr;
    char	*type;
    Tcl_Encoding outputEncoding;
    Tcl_Encoding urlEncoding;
    Tcl_Encoding queryEncoding;
    int          nContentSent;
    int          status;
    int          responseLength;
    int          recursionCount;
    Ns_Set      *query;
    Tcl_HashTable files;

    /*
     * The following are copied from Sock.
     */
     
    char	    peer[16];	/* Client peer address. */
    int		    port;	/* Client peer port. */

    /*
     * The following array maintains conn local storage.
     */
     
    void	   *cls[NS_CONN_MAXCLS];
    struct QueWait *queWaitPtr;

    /*
     * The following pointers are used to access the
     * buffer contents after the read-ahead is complete.
     */

    char	   *next;	/* Next read offset. */
    size_t          avail;	/* Bytes avail in buffer. */
    char	   *content;	/* Start of content. */
    int             tfd;        /* Temp fd for file-based content. */
    void	   *map;	/* Mmap'ed content, if any. */
    void	   *maparg;	/* Argument for NsUnMap. */

    /*
     * The following offsets are used to manage the 
     * buffer read-ahead process.
     *
     * NB: The ibuf and obuf dstrings must be the last elements of
     * the conn as all elements before are zero'ed during conn cleanup.
     *
     */

    int		    roff;	/* Next read buffer offset. */
    Tcl_DString	    ibuf;	/* Request and content input buffer. */
    Tcl_DString	    obuf;	/* Output buffer for queued headers. */

} Conn;

/*
 * The following structure maintains a connection thread pool.
 */

typedef struct Pool {
    Ns_Mutex        lock;
    Ns_Cond         cond;
    char           *name;
    int             shutdown;

    /*
     * The following struct maintains the active and waiting connection
     * queues, the next conn id, and the number of waiting connects.
     */

    struct {
	struct {
	    int     	    num;
            struct Conn    *firstPtr;
            struct Conn    *lastPtr;
        } wait;
        struct {
            struct Conn    *firstPtr;
            struct Conn    *lastPtr;
	} active;
    } queue;

    /*
     * The following struct maintins the state of the threads.  Min and max
     * threads are determined at startup and then NsQueueConn ensures the
     * current number of threads remains within that range with individual
     * threads waiting no more than the timeout for a connection to
     * arrive.  The number of idle threads is maintained for the benefit of
     * the ns_server command.  Threads will handle up to maxconns before
     * exit (default is the "connsperthread" virtual server config).
     */

    struct {
	unsigned int	    nextid;
	int 	    	    min;
	int 	    	    max;
    	int 	    	    current;
	int 	    	    idle;
	int 	    	    waiting;
	int 	    	    starting;
	int 	    	    timeout;
	int		    maxconns;
	int		    spread;
    	unsigned int	    queued;
    } threads;

} Pool;

#define SERV_AOLPRESS		0x0001	/* AOLpress support. */
#define SERV_CHUNKED		0x0002	/* Output can be chunked. */
#define SERV_MODSINCE		0x0004	/* Check if-modified-since. */
#define SERV_NOTICEDETAIL	0x0008	/* Add detail to notice messages. */
#define SERV_GZIP		0x0010	/* Enable GZIP compression. */

/*
 * The following struct maintains nsv's, shared string variables.
 */

typedef struct Nsv {
    struct Bucket  	   *buckets;
    int 	    	    nbuckets;
} Nsv;

/*
 * The following structure is allocated for each virtual server.
 */

typedef struct NsServer {
    char    	    	   *server;
    Ns_LocationProc 	   *locationProc;

    /*
     * Default charset for text/ types.
     */

    char		  *defcharset;

    /*
     * The following encoding is used for decoding request URL's.
     * This is a server-wide config as the request must be read
     * and parsed before the URL can be determined to identify
     * a possible alternate encoding.
     */

    Tcl_Encoding           urlEncoding;

    /*
     * The following encoding is used for decoding input
     * query strings and forms unless a more specific encoding
     * is set via ns_register_encoding.
     */

    Tcl_Encoding           inputEncoding;

    /* 
     * The following struct maintains various server options.
     */

    struct {
   	int		    flags;
	size_t 	    	    gzipmin;
	int		    gziplevel;
    	char 	    	   *realm;
	Ns_HeaderCaseDisposition hdrcase;
    } opts;
    
    /* 
     * The following struct maintains conn-related limits.
     */

    struct {
    	int 	    	    errorminsize;
	int		    connsperthread;
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
	/*
	 * The following is the bootstrap script, normally bin/init.tcl.
	 */

	char	    	   *initfile;

	/*
	 * The following support the loop control facilities.
	 */

	Tcl_HashTable loops;
	Ns_Mutex      llock;
	Ns_Cond	      lcond;

	/*
	 * The following support traces and one-time inits.
	 */

	Ns_RWLock	    tlock;	/* Lock for trace list. */
	struct TclTrace    *firstTracePtr;
	struct TclTrace    *lastTracePtr;
	Ns_Cs		    olock;	/* Lock for one-time inits. */
	Tcl_HashTable	    once;	/* Table of one-time inits. */
	Ns_Mutex	    plock;	/* Lock for package table. */
	Tcl_HashTable	    packages;	/* Table of server packages. */

	/*
	 * The following support the legacy module directories config.
	 */

	char	           *library;	/* Legacy library. */
	char		   *script;	/* Legacy init script. */
	int		    length;
	int		    epoch;
	Ns_RWLock	    slock;	/* Lock for init script. */
	Tcl_DString	    modules;	/* List of server modules. */
    } tcl;

    /*
     * The following struct maintains ADP config,
     * registered tags, and read-only page text.
     */

    struct {
	int		    flags;
	int		    tracesize;
	char	    	   *errorpage;
	char	    	   *startpage;
	char	    	   *debuginit;
	size_t		    bufsize;
	size_t		    cachesize;
	Ns_Cond	    	    pagecond;
	Ns_Mutex	    pagelock;
	Tcl_HashTable       pages;
	Ns_RWLock	    taglock;
	Tcl_HashTable       tags;
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
     * The following maintains per-server nsv's.
     */

    Nsv nsv;
    
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
    struct NsInterp	  *nextPtr;	/* Next interps for given server. */
    Tcl_Interp		  *interp;	/* Pointer to cooresponding interp. */
    NsServer  	    	  *servPtr;	/* Pointer to interp server. */
    int		   	   delete;	/* Delete interp on next deallocate. */
    int			   epoch;	/* Epoch of legacy config. */

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

    struct nsconn {
	int		   flags;
	char	   	   form[16];
	char	   	   hdrs[16];
	char	   	   outhdrs[16];
    } nsconn;

    /*
     * The following struct maintains per-interp ADP
     * context including the private pages cache.
     */

    struct adp {
	int		   flags;
	int		   exception;
	int		   refresh;
	size_t		   bufsize;
	int                errorLevel;
	int                debugLevel;
	int                debugInit;
	char              *debugFile;
	Ns_Cache	  *cache;
	int                depth;
	char		  *cwd;
	struct AdpFrame	  *framePtr;
	Ns_Conn		  *conn;
	Tcl_Channel	   chan;
	Tcl_DString	   output;
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
extern void NsInitConfig(void);
extern void NsInitEncodings(void);
extern void NsInitFd(void);
extern void NsInitListen(void);
extern void NsInitLog(void);
extern void NsInitMimeTypes(void);
extern void NsInitModLoad(void);
extern void NsInitNsv(void);
extern void NsInitProcInfo(void);
extern void NsInitQueue(void);
extern void NsInitLimits(void);
extern void NsInitPools(void);
extern void NsInitDrivers(void);
extern void NsInitSched(void);
extern void NsInitServers(void);
extern void NsInitTcl(void);
extern void NsInitTclCache(void);
extern void NsInitUrlSpace(void);
extern void NsInitRequests(void);
extern char *NsFindVersion(char *request, unsigned int *majorPtr,
			   unsigned int *minorPtr);
extern void NsQueueConn(Conn *connPtr);
extern int NsCheckQuery(Ns_Conn *conn);
extern void NsAppendConn(Tcl_DString *bufPtr, Conn *connPtr, char *state);
extern void NsAppendRequest(Tcl_DString *dsPtr, Ns_Request *request);
extern int  NsConnSend(Ns_Conn *conn, struct iovec *bufs, int nbufs);
extern void NsSockClose(Sock *sockPtr, int keep);
extern int  NsPoll(struct pollfd *pfds, int nfds, Ns_Time *timeoutPtr);
extern void NsFreeConn(Conn *connPtr);
extern NsServer *NsGetServer(char *server);
extern char *NsGetServers(void);
extern NsServer *NsGetInitServer(void);
extern NsInterp *NsGetInterpData(Tcl_Interp *interp);
extern void NsFreeConnInterp(Conn *connPtr);
extern Ns_OpProc NsAdpProc;

extern Ns_Cache *NsFastpathCache(char *server, int size);
extern void NsAdpInit(NsInterp *itPtr);
extern void NsAdpReset(NsInterp *itPtr);
extern void NsAdpFree(NsInterp *itPtr);
extern void NsTclRunAtClose(NsInterp *itPtr);
extern int  NsUrlToFile(Ns_DString *dsPtr, NsServer *servPtr, char *url);

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

extern char *NsConnContent(Ns_Conn *conn, char **nextPtr, int *availPtr);
extern void NsConnSeek(Ns_Conn *conn, int count);
extern void *NsMap(int fd, off_t start, size_t len, int writeable, void **argPtr);
extern void NsUnMap(void *addr, void *arg);

#ifdef _WIN32
extern int  NsConnectService(void);
extern int  NsInstallService(char *service);
extern int  NsRemoveService(char *service);
#endif

extern void NsCreatePidFile(char *service);
extern void NsRemovePidFile(char *service);

extern void NsLogOpen(void);
extern void NsLogConf(void);
extern void NsTclInitObjs(void);
extern void NsUpdateMimeTypes(void);
extern void NsUpdateEncodings(void);
extern void NsRunPreStartupProcs(void);
extern void NsBlockSignals(int debug);
extern void NsHandleSignals(void);
extern void NsStopDrivers(void);
extern void NsPreBind(char *bindargs, char *bindfile);
extern SOCKET NsSockGetBound(struct sockaddr_in *saPtr);
extern void NsClosePreBound(void);
extern void NsInitServer(char *server, Ns_ServerInitProc *initProc);
extern char *NsConfigRead(char *file);
extern void NsConfigEval(char *config, int argc, char **argv, int optind);
extern void NsConfUpdate(void);
extern void NsEnableDNSCache(void);
extern void NsStartPools(void);
extern void NsStopPools(Ns_Time *timeoutPtr);
extern int NsTclGetPool(Tcl_Interp *interp, char *pool, Pool **poolPtrPtr);
extern Tcl_ObjCmdProc NsTclListPoolsObjCmd;
extern void NsCreateConnThread(Pool *poolPtr, int joinThreads);
extern void NsJoinConnThreads(void);
extern int  NsStartDrivers(void);
extern void NsWaitDriversShutdown(Ns_Time *toPtr);
extern void NsStartSchedShutdown(void);
extern void NsWaitSchedShutdown(Ns_Time *toPtr);
extern void NsStartSockShutdown(void); 
extern void NsWaitSockShutdown(Ns_Time *toPtr);
extern void NsStartShutdownProcs(void);
extern void NsWaitShutdownProcs(Ns_Time *toPtr);
extern void NsWaitDriversShutdown(Ns_Time *toPtr);
extern void NsStartQueueShutdown(void);
extern void NsWaitQueueShutdown(Ns_Time *toPtr);

extern void NsStartJobsShutdown(void);
extern void NsWaitJobsShutdown(Ns_Time *toPtr);

extern void NsTclInitServer(char *server);
extern int NsTclGetServer(NsInterp *itPtr, char **serverPtr);
extern int NsTclGetConn(NsInterp *itPtr, Ns_Conn **connPtr);
extern void NsLoadModules(char *server);
extern struct Bucket *NsTclCreateBuckets(char *server, int nbuckets);
extern void NsClsCleanup(Conn *connPtr);
extern void NsTclAddCmds(Tcl_Interp *interp, NsInterp *itPtr);
extern void NsRestoreSignals(void);
extern void NsSendSignal(int sig);

/*
 * Conn routines.
 */

extern Limits *NsGetRequestLimits(char *server, char *method, char *url);
extern Pool *NsGetConnPool(Conn *connPtr);

/*
 * ADP routines.
 */

extern Ns_Cache *NsAdpCache(char *server, int size);
extern void NsAdpSetMimeType(NsInterp *itPtr, char *type);
extern void NsAdpSetCharSet(NsInterp *itPtr, char *charset);
extern int NsAdpGetBuf(NsInterp *itPtr, Tcl_DString **dsPtrPtr);
extern int NsAdpAppend(NsInterp *itPtr, char *buf, int len);
extern int NsAdpFlush(NsInterp *itPtr, int stream);
extern int NsAdpDebug(NsInterp *itPtr, char *host, char *port, char *procs);
extern int NsAdpEval(NsInterp *itPtr, int objc, Tcl_Obj *objv[], int flags,
                     char *resvar);
extern int NsAdpSource(NsInterp *itPtr, int objc, Tcl_Obj *objv[],
                       int flags, char *resvar);
extern int NsAdpInclude(NsInterp *itPtr, int objc, Tcl_Obj *objv[],
			char *file, Ns_Time *ttlPtr);
extern void NsAdpParse(AdpCode *codePtr, NsServer *servPtr, char *utf,
		       int flags);
extern void NsAdpFreeCode(AdpCode *codePtr);
extern void NsAdpLogError(NsInterp *itPtr);

/*
 * Tcl support routines.
 */

extern int  NsTclCheckConnId(Tcl_Interp *interp, Tcl_Obj *objPtr);
extern void NsTclInitQueueType(void);
extern void NsTclInitAddrType(void);
extern void NsTclInitCacheType(void);
extern void NsTclInitKeylistType(void);
extern void NsTclInitTimeType(void);

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

extern bool  NsParamBool(char *key, bool def);
extern int   NsParamInt(char *key, int def);
extern char *NsParamString(char *key, char *def);

extern int  NsCloseAllFiles(int errFd);
#ifndef _WIN32
extern int  Ns_ConnRunRequest(Ns_Conn *conn);
extern int  Ns_GetGid(char *group);
extern int  Ns_GetUserGid(char *user);
extern int  Ns_TclGetOpenFd(Tcl_Interp *, char *, int write, int *fp);
#endif
extern void NsStopSockCallbacks(void);
extern void NsStopScheduledProcs(void);
extern Tcl_Encoding NsGetInputEncoding(Conn *connPtr);
extern Tcl_Encoding NsGetOutputEncoding(Conn *connPtr);

/*
 * Proxy support
 */

extern int NsConnRunProxyRequest(Ns_Conn *conn);

#endif
