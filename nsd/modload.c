/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.lcs.mit.edu/.
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
 * modload.c --
 *
 *	Load .so files into the server and initialize them. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/modload.c,v 1.1.1.1 2000/05/02 13:48:21 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#if defined(USE_DLSHL)
#include <dl.h>

#elif defined(USE_DYLD)
#include <mach-o/dyld.h>
static int dyld_handlers_installed = 0;
static void dyld_linkEdit_symbol_handler(NSLinkEditErrors c, int errorNumber,
      const char *fileName, const char *errorString);
static NSModule dyld_multiple_symbol_handler(NSSymbol s, NSModule old,
	NSModule new);
static void dyld_undefined_symbol_handler(const char *symbolName);

#elif !defined(WIN32)
#include <dlfcn.h>
#ifdef USE_RTLD_LAZY
#ifdef RTLD_NOW
#undef RTLD_NOW
#endif
#define RTLD_NOW RTLD_LAZY
#endif
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL  0
#endif
#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif

#endif

#define DEFAULT_INITPROC "Ns_ModuleInit"

/*
 * The following structure is used for static module loading.
 */

typedef struct Module {
    struct Module *nextPtr;
    char *name;
    Ns_ModuleInitProc *proc;
} Module;

static Module *firstPtr;

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable modulesTable;
static void *DlOpen(char *file);
static void *DlSym(void *handle, char *name);
static char *DlError(void);


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterModule --
 *
 *	Register a static module.  This routine is typically called
 * 	from a custom Ns_ServerInit() to initialize static code
 * 	by name in a manner similar to dynamic modules, i.e.,
 *	the init proc will be called and any cooresponding Tcl
 * 	script files later evaluated.
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
Ns_RegisterModule(char *name, Ns_ModuleInitProc *proc)
{
    Module *modPtr, **nextPtrPtr;

    modPtr = ns_malloc(sizeof(Module));
    modPtr->name = name;
    modPtr->proc = proc;
    modPtr->nextPtr = NULL;
    nextPtrPtr = &firstPtr;
    while (*nextPtrPtr != NULL) {
	nextPtrPtr = &((*nextPtrPtr)->nextPtr); 
    }
    *nextPtrPtr = modPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleLoad --
 *
 *	Load a module and initialize it. 
 *
 * Results:
 *	NS_OK or NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ModuleLoad(char *server, char *module, char *file, char *init)
{
    Ns_ModuleInitProc *initProc;
    int                status = NS_OK;
    int               *verPtr;

    if (init == NULL) {
        init = DEFAULT_INITPROC;
    }
    initProc = (Ns_ModuleInitProc *) Ns_ModuleSymbol(file, init);
    if (initProc == NULL) {
        status = NS_ERROR;
    } else {

	/*
	 * Get the version of the module stored in the Ns_ModuleVersion
	 * variable and check that it's version 1.0 or higher. If it's
	 * lower, then ignore failure from the init proc.
	 */

        verPtr = (int *) Ns_ModuleSymbol(file, "Ns_ModuleVersion");

	/*
	 * Run the init proc.
	 */
	
        status = (*initProc) (server, module);
        if (verPtr == NULL || *verPtr < 1) {
            status = NS_OK;
        } else if (status != NS_OK) {
	    Ns_Log(Error, "could not load \"%s\": "
		      "%s returned %d", file, init, status);
	}
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleSymbol --
 *
 *	Load a module if it's not already loaded, and extract a 
 *	requested symbol from it. 
 *
 * Results:
 *	A pointer to the symbol's value. 
 *
 * Side effects:
 *	May load the module if it hasn't been loaded yet. 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_ModuleSymbol(char *file, char *name)
{
    Tcl_HashEntry *hPtr;
    Ns_DString     ds, ds2;
    int		   new;
    void	  *module;
    void          *symbol;
    static int     initialized = 0;

    /*
     * Clean up the module filename.
     */
    
    Ns_DStringInit(&ds);
    Ns_DStringInit(&ds2);
    if (Ns_PathIsAbsolute(file)) {
        Ns_DStringAppend(&ds, file);
    } else {
        Ns_HomePath(&ds, "bin", file, NULL);
    }
    file = Ns_NormalizePath(&ds2, ds.string);

    /*
     * Find or load the module.
     */
    
    if (initialized == 0) {
    	Tcl_InitHashTable(&modulesTable, TCL_STRING_KEYS);
	initialized = 1;
    }
    hPtr = Tcl_CreateHashEntry(&modulesTable, file, &new);
    if (new == 0) {
        module = Tcl_GetHashValue(hPtr);
    } else {

	/*
	 * Load the module because it was not found in the hash table.
	 */
	
    	Ns_Log(Notice, "loading: %s", file);
	module = DlOpen(file);
	if (module == NULL) {
            Ns_Log(Warning, "could not load %s:  %s", file, DlError());
            Tcl_DeleteHashEntry(hPtr);
	} else {
            Tcl_SetHashValue(hPtr, module);
        }
    }
    symbol = NULL;
    
    /*
     * Now load the symbol from the module, assuming the module has
     * been loaded without errors.
     */

    if (module != NULL) {
	symbol = DlSym(module, name);
	if (symbol == NULL) {
	    Ns_Log(Warning, "no such symbol \"%s\" in module \"%s\"",
		name, file);
	}
    }

    Ns_DStringFree(&ds);
    Ns_DStringFree(&ds2);

    return symbol;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleGetSymbol --
 *
 *	Locate a given symbol in the program's symbol table and 
 *	return the address of it. This differs from the other Module 
 *	functions in that it doesn't require the shared library file 
 *	name - this should sniff the entire symbol space. 
 *
 * Results:
 *	A pointer to the requested symbol's value. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_ModuleGetSymbol(char *name)
{
    return DlSym(NULL, name);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLoadModules --
 *
 *	Load all modules. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will load and initialize modules. 
 *
 *----------------------------------------------------------------------
 */

void 
NsLoadModules(void)
{
    Module *modPtr, *nextPtr;
    Ns_Set *modules;
    int     i;
    char   *file, *module, *init, *s, *e;

    /*
     * Get ahold of the key/value pairs for this section.  e.g. in the nsd.ini:
     *
     * [ns/server/server1/modules]
     * nssock=nssock.so
     * nsperm=nsperm.so(spoonman)
     *
     * The "modules" set would have keys [nssock, nsperm]
     * and values [nssock.so, nsperm.so(spoonman)]
     *
     * In the 'nsperm.so(spoonman)', the function "spoonman" will be the one
     * used to initialize the module.  [it's special-cased so that value "TCL"
     * doesn't do that.  I suppose that's because TCL is built-in.]
     *
     */

    modules = Ns_ConfigSection(Ns_ConfigPath(nsServer, NULL, "modules", NULL));
    if (modules != NULL) {
	/*
	 * Spin over the modules specified in this subsection
	 */
	
        for (i = 0; i < Ns_SetSize(modules); ++i) {
            module = Ns_SetKey(modules, i);
            file = Ns_SetValue(modules, i);

	    /*
	     * See if there's an optional argument to the module file name.
	     */

            init = NULL; 
            s = strchr(file, '(');
            if (s != NULL) {
                *s = '\0';
                init = s + 1;
                e = strchr(init, ')');
                if (e != NULL) {
                    *e = '\0';
                }
            }

	    /*
	     * If the file is not called tcl (which is a special case),
	     * then try to load the module.
	     */
	    
            if (!STRIEQ(file, "tcl") &&
		Ns_ModuleLoad(nsServer, module, file, init) != NS_OK) {
		Ns_Fatal("could not load: %s", file);
            }
	    Ns_Log(Debug, "initializing: %s", module);

	    /*
	     * Append this module to the list of modules that need
	     * initialization (tcl sourcing, eventually).
	     */
	    
            Ns_TclInitModule(nsServer, module);

            if (s != NULL) {
                *s = '(';
                if (e != NULL) {
                    *e = ')';
                }
            }
        }
    }

    /*
     * Initialize the static modules (if any).  Note
     * that a static module could add a new static module
     * and so the loop is repeated until they're all gone.
     */

    while (firstPtr != NULL) {
    	modPtr = firstPtr;
	firstPtr = NULL;
    	while (modPtr != NULL) {
	    nextPtr = modPtr->nextPtr;
	    Ns_Log(Notice, "modload: initializing: %s", modPtr->name);
	    if ((*modPtr->proc)(nsServer, modPtr->name) != NS_OK) {
	    	Ns_Fatal("could not initialize: %s", modPtr->name);
	    }
            Ns_TclInitModule(nsServer, modPtr->name);
	    ns_free(modPtr);
	    modPtr = nextPtr;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * DlOpen --
 *
 *	Load a dynamic library
 *
 * Results:
 *	An Ns_ModHandle, or NULL on failure.
 *
 * Side effects:
 *	See shl_load 
 *
 *----------------------------------------------------------------------
 */

static void *
DlOpen(char *file)
{
#ifdef WIN32
    return (void *) LoadLibrary(file);
#elif defined(USE_DLSHL)
    return (void *) shl_load(file, BIND_VERBOSE|BIND_IMMEDIATE|BIND_RESTRICTED, 0);
#elif defined(USE_DYLD)
    NSObjectFileImage		image;
    NSModule			linkedModule;
    NSObjectFileImageReturnCode	err;

    if (dyld_handlers_installed == 0) {
	NSLinkEditErrorHandlers handlers;

	handlers.undefined = dyld_undefined_symbol_handler;
	handlers.multiple  = dyld_multiple_symbol_handler;
	handlers.linkEdit  = dyld_linkEdit_symbol_handler;

	NSInstallLinkEditErrorHandlers(&handlers);
	dyld_handlers_installed = 1;
    }
    err = NSCreateObjectFileImageFromFile(file, &image);
    if (err != NSObjectFileImageSuccess) {
	switch (err) {
	    case NSObjectFileImageFailure:
		Ns_Log(Error, "dyld: general failure (%s)", file);
		break;
	    case NSObjectFileImageInappropriateFile:
		Ns_Log(Error, "dyld: inappropriate Mach-O file (%s)", file);
		break;
	    case NSObjectFileImageArch:
		Ns_Log(Error, "dyld: inappropriate Mach-O architecture (%s)",
		    file);
		break;
	    case NSObjectFileImageFormat:
		Ns_Log(Error, "dyld: invalid Mach-O file format (%s)", file);
		break;
	    case NSObjectFileImageAccess:
		Ns_Log(Error, "dyld: permission denied (%s)", file);
		break;
	    default:
		Ns_Log(Error, "dyld: unknown failure (%s)", file);
		break;
	}
        return NULL;
    }
    linkedModule = NSLinkModule(image, file, TRUE);
    return (void *) linkedModule;
#else
    return dlopen(file, RTLD_NOW|RTLD_GLOBAL);
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * DlSym --
 *
 *	Load a symbol from a shared object
 *
 * Results:
 *	A symbol pointer or null on error. 
 *
 * Side effects:
 *	See shl_findsym. 
 *
 *----------------------------------------------------------------------
 */

static void *
DlSym(void *handle, char *name)
{
    void *symbol;
#ifdef USE_DLSYMPREFIX
    Ns_DString ds;

    Ns_DStringInit(&ds);
    name = Ns_DStringVarAppend(&ds, "_", name, NULL);
#endif

#ifdef WIN32
    symbol =  (void *) GetProcAddress((HMODULE) handle, name);
#elif defined(USE_DLSHL)
    symbol = NULL;
    if (shl_findsym((shl_t *) &handle, name, TYPE_UNDEFINED,
		    &symbol) == -1) {
	symbol = NULL;
    }
#elif (USE_DYLD)
    symbol = (void *) NSAddressOfSymbol(NSLookupAndBindSymbol(name));
#else
    symbol = dlsym(handle, name);
#endif

#ifdef USE_DLSYMPREFIX
    Ns_DStringFree(&ds);
#endif
    return symbol;
}


/*
 *----------------------------------------------------------------------
 *
 * DlError --
 *
 *	Return the error code from trying to load a shared object
 *
 * Results:
 *	A string error. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static char *
DlError(void)
{
#ifdef WIN32
    return NsWin32ErrMsg(GetLastError());
#elif defined(USE_DLSHL)
    return strerror(errno);
#elif defined(USE_DYLD)
    return "Unknown dyld error";
#else
    return (char *) dlerror();
#endif
}

#if defined(USE_DYLD)

/*
 *----------------------------------------------------------------------
 *
 * dyld_undefined_symbol_handler --
 *
 *	Handle undefined symbol exceptions from dyld
 *
 * Results:
 *	Process is aborted.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */
static void
dyld_undefined_symbol_handler(const char *symbolName) 
{
    Ns_Fatal("dyld: no such symbol: %s.", symbolName);
}


/*
 *----------------------------------------------------------------------
 *
 * dyld_multiple_symbol_handler --
 *
 *	Handle multiple symbol exceptions from dyld
 *
 * Results:
 *	New symbol definition is used.  This is basically a memory leak,
 *	but is necessary because there's no way to unload a module.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static NSModule
dyld_multiple_symbol_handler(NSSymbol s, NSModule old, NSModule new)
{
#ifdef DEBUG
    Ns_Log(Warning, 
	"dyld: multiply-defined symbol '%s' in old '%s', new '%s'",
	NSNameOfSymbol(s),  NSNameOfModule(old), NSNameOfModule(new));
#endif

    return(new);
}


/*
 *----------------------------------------------------------------------
 *
 * dyld_linkEdit_symbol_handler --
 *
 *	Handle link editor errors from dyld.
 *
 * Results:
 *	Error is logged and process is aborted.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static void
dyld_linkEdit_symbol_handler(NSLinkEditErrors c, int errNumber,
    const char *filename, const char *errString)
{
    Ns_Log(Error, "dyld: file: '%s': link edit errors: '%s'; aborting.",
	filename, errString);
    abort();
}

#endif
