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
 * modload.c --
 *
 *	Load .so files into the server and initialize them. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/modload.c,v 1.17 2006/04/19 17:48:52 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#if defined(USE_DLSHL)
#include <dl.h>
#elif !defined(_WIN32)
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

/*
 * The following structure is used for static module loading.
 */

typedef struct Module {
    struct Module *nextPtr;
    char *name;
    Ns_ModuleInitProc *proc;
} Module;

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable modulesTable;
static void *DlOpen(char *file);
static void *DlSym(void *handle, char *name);
static void *DlSym2(void *handle, char *name);
static char *DlError(void);
static Module *firstPtr;


/*
 *----------------------------------------------------------------------
 *
 * NsInitModLoad --
 *
 *	Initialize module table.
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
NsInitModLoad(void)
{
#ifdef _WIN32
    Tcl_InitHashTable(&modulesTable, TCL_STRING_KEYS);
#else
    Tcl_InitHashTable(&modulesTable, FILE_KEYS);
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterModule --
 *
 *	Register a static module.  This routine can only be called from
 *	a Ns_ServerInitProc passed to Ns_Main or within the Ns_ModuleInit
 *	proc of a loadable module.  It registers a module callback for
 *	for the currently initializing server.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Proc will be called after dynamic modules are loaded. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_RegisterModule(char *name, Ns_ModuleInitProc *proc)
{
    Module *modPtr, **nextPtrPtr;

    modPtr = ns_malloc(sizeof(Module));
    modPtr->name = ns_strcopy(name);
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
 *	Load a module and initialize it.  The result code from modules
 *	without the version symbol are ignored.
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

    initProc = Ns_ModuleSymbol(file, init);
    if (initProc == NULL) {
	return NS_ERROR;
    }
    verPtr = Ns_ModuleSymbol(file, "Ns_ModuleVersion");
    status = (*initProc) (server, module);
    if (verPtr == NULL || *verPtr < 1) {
        status = NS_OK;
    } else if (status != NS_OK) {
	Ns_Log(Error, "modload: init %s of %s returned: %d", file, init, status);
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
    Ns_DString     ds;
    int		   new;
    void	  *module;
    void          *symbol;
    struct stat    st;
#ifndef _WIN32
    FileKey	   key;
#endif

    symbol = NULL;
    Ns_DStringInit(&ds);
    if (!Ns_PathIsAbsolute(file)) {
        file = Ns_HomePath(&ds, "bin", file, NULL);
    }
    if (stat(file, &st) != 0) {
	Ns_Log(Notice, "modload: stat(%s) failed: %s", file, strerror(errno));
	goto done;
    }
#ifdef _WIN32
    hPtr = Tcl_CreateHashEntry(&modulesTable, file, &new);
#else
    key.dev = st.st_dev;
    key.ino = st.st_ino;
    hPtr = Tcl_CreateHashEntry(&modulesTable, (char *) &key, &new);
#endif
    if (!new) {
        module = Tcl_GetHashValue(hPtr);
    } else {
    	Ns_Log(Notice, "modload: loading '%s'", file);
	module = DlOpen(file);
	if (module == NULL) {
            Ns_Log(Warning, "modload: could not load %s: %s", file, DlError());
            Tcl_DeleteHashEntry(hPtr);
	    goto done;
	}
        Tcl_SetHashValue(hPtr, module);
    }
    symbol = DlSym(module, name);
    if (symbol == NULL) {
	Ns_Log(Warning, "modload: could not find %s in %s", name, file);
    }
done:
    Ns_DStringFree(&ds);
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
 *	Load all modules for given server.
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
NsLoadModules(char *server)
{
    Ns_Set *modules;
    int     i;
    char   *file, *module, *init = NULL, *s, *e = NULL;
    Module *modPtr, *nextPtr;

    modules = Ns_ConfigGetSection(Ns_ConfigGetPath(server, NULL, "modules", NULL));
    for (i = 0; modules != NULL && i < Ns_SetSize(modules); ++i) {
	module = Ns_SetKey(modules, i);
        file = Ns_SetValue(modules, i);

	/*
	 * Check for specific module init after filename.
	 */

        s = strchr(file, '(');
        if (s == NULL) {
	    init = "Ns_ModuleInit";
	} else {
            *s = '\0';
            init = s + 1;
            e = strchr(init, ')');
            if (e != NULL) {
                *e = '\0';
            }
	}

	/*
	 * Load the module if it's not the reserved "tcl" name.
	 */
	    
       if (!STRIEQ(file, "tcl") && Ns_ModuleLoad(server, module, file, init) != NS_OK) {
	    Ns_Fatal("modload: failed to load module '%s'", file);
        }

	/*
	 * Add this module to the server Tcl init list.
	 */

        Ns_TclInitModule(server, module);

        if (s != NULL) {
            *s = '(';
            if (e != NULL) {
                *e = ')';
            }
        }
    }

    /*
     * Initialize the static modules (if any).  Note that a static
     * module could add a new static module and so the loop is
     * repeated until they're all gone.
     */

    while (firstPtr != NULL) {
    	modPtr = firstPtr;
	firstPtr = NULL;
    	while (modPtr != NULL) {
	    nextPtr = modPtr->nextPtr;
	    Ns_Log(Notice, "modload: initializing module '%s'", modPtr->name);
	    if ((*modPtr->proc)(server, modPtr->name) != NS_OK) {
	    	Ns_Fatal("modload: failed to initialize %s", modPtr->name);
	    }
	    ns_free(modPtr->name);
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
#if defined(_WIN32)
    return (void *) LoadLibrary(file);
#elif defined(USE_DLSHL)
    return (void *) shl_load(file, BIND_VERBOSE|BIND_IMMEDIATE|BIND_RESTRICTED, 0);
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
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void *
DlSym(void *handle, char *name)
{
    Ns_DString ds;
    void *symbol;

    symbol = DlSym2(handle, name);
    if (symbol == NULL) {

        /*
         * Some BSD platforms (e.g., OS/X) prepend an underscore
         * to all symbols.
         */

        Ns_DStringInit(&ds);
        Ns_DStringVarAppend(&ds, "_", name, NULL);
        symbol = DlSym2(handle, ds.string);
        Ns_DStringFree(&ds);
    }

    return symbol;
}

static void *
DlSym2(void *handle, char *name)
{
    void *symbol = NULL;

#if defined(USE_DLSHL)
    if (shl_findsym((shl_t *) &handle, name, TYPE_UNDEFINED, &symbol) == -1) {
        symbol = NULL;
    }
#elif defined(_WIN32)
    symbol =  (void *) GetProcAddress((HMODULE) handle, name);
#else
    symbol = dlsym(handle, name);
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
#if defined(USE_DLSHL)
    return strerror(errno);
#elif defined(_WIN32)
    return NsWin32ErrMsg(GetLastError());
#else
    return (char *) dlerror();
#endif
}
