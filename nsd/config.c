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
 * config.c --
 *
 *	Support for the configuration file
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/config.c,v 1.19 2005/08/01 20:48:08 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#define ISSLASH(c)      ((c) == '/' || (c) == '\\')

/*
 * Local procedures defined in this file.
 */

static Tcl_CmdProc SectionCmd;
static Tcl_CmdProc ParamCmd;
static Ns_Set     *GetSection(char *section, int create);
static char       *ConfigGet(char *section, char *key, int exact);

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable sections;


/*
 *----------------------------------------------------------------------
 *
 * NsInitConfig --
 *
 *	Initialize the config interface.
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
NsInitConfig(void)
{
    Tcl_InitHashTable(&sections, TCL_STRING_KEYS);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConfigGetValue --
 *
 *	Return a config file value for a given key 
 *
 * Results:
 *	ASCIIZ ptr to a value 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConfigGetValue(char *section, char *key)
{
    return ConfigGet(section, key, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConfigGetValueExact --
 *
 *	Case-sensitive version of Ns_ConfigGetValue 
 *
 * Results:
 *	See Ns_ConfigGetValue 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConfigGetValueExact(char *section, char *key)
{
    return ConfigGet(section, key, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConfigGetInt --
 *
 *	Fetch integer config values 
 *
 * Results:
 *	S_TRUE if it found an integer value; otherwise, it returns 
 *	NS_FALSE and sets the value to 0 
 *
 * Side effects:
 *	The integer value is returned by reference 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConfigGetInt(char *section, char *key, int *valuePtr)
{
    char           *s;

    s = Ns_ConfigGetValue(section, key);
    if (s == NULL || sscanf(s, "%d", valuePtr) != 1) {
        return NS_FALSE;
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConfigGetInt64 --
 *
 *	Like Ns_ConfigGetInt, but with INT64 data instead of 
 *	system-native int types. 
 *
 * Results:
 *	See Ns_ConfigGetInt 
 *
 * Side effects:
 *	See Ns_ConfigGetInt 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConfigGetInt64(char *section, char *key, INT64 *valuePtr)
{
    char           *s;

    s = Ns_ConfigGetValue(section, key);
    if (s == NULL || sscanf(s, NS_INT_64_FORMAT_STRING, valuePtr) != 1) {
        return NS_FALSE;
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConfigGetBool --
 *
 *	Get a boolean config value. There are many ways to represent 
 *	a boolean value. 
 *
 * Results:
 *	NS_TRUE/NS_FALSE
 *
 * Side effects:
 *	The boolean value is returned by reference 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConfigGetBool(char *section, char *key, int *valuePtr)
{
    char           *s;

    s = Ns_ConfigGetValue(section, key);
    if (s == NULL) {
        return NS_FALSE;
    }
    if (STREQ(s, "1") ||
	STRIEQ(s, "y") ||
	STRIEQ(s, "yes") ||
	STRIEQ(s, "on") ||
	STRIEQ(s, "t") ||
	STRIEQ(s, "true")) {

        *valuePtr = 1;
    } else if (STREQ(s, "0") ||
	STRIEQ(s, "n") ||
	STRIEQ(s, "no") ||
	STRIEQ(s, "off") ||
	STRIEQ(s, "f") ||
	STRIEQ(s, "false")) {

        *valuePtr = 0;
    } else if (sscanf(s, "%d", valuePtr) != 1) {
        return NS_FALSE;
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConfigGetPath --
 *
 *	Get the full name of a config file section if it exists. 
 *
 * Results:
 *	A pointer to an ASCIIZ string of the full path name, or NULL 
 *	if that path is not in the config file. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConfigGetPath(char *server, char *module, ...)
{
    va_list         ap;
    char           *s;
    Ns_DString      ds;
    Ns_Set         *set;

    Ns_DStringInit(&ds);
    Ns_DStringAppend(&ds, "ns");
    if (server != NULL) {
        Ns_DStringVarAppend(&ds, "/server/", server, NULL);
    }
    if (module != NULL) {
        Ns_DStringVarAppend(&ds, "/module/", module, NULL);
    }
    va_start(ap, module);
    while ((s = va_arg(ap, char *)) != NULL) {
        Ns_DStringAppend(&ds, "/");
        while (*s != '\0' && ISSLASH(*s)) {
            ++s;
        }
        Ns_DStringAppend(&ds, s);
        while (ISSLASH(ds.string[ds.length - 1])) {
            ds.string[--ds.length] = '\0';
        }
    }
    va_end(ap);

    set = Ns_ConfigGetSection(ds.string);
    Ns_DStringFree(&ds);

    return (set ? Ns_SetName(set) : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConfigGetSections --
 *
 *	Return a malloc'ed, NULL-terminated array of sets, each 
 *	corresponding to a config section 
 *
 * Results:
 *	An array of sets 
 *
 * Side effects:
 *	The result is malloc'ed memory 
 *
 *----------------------------------------------------------------------
 */

Ns_Set **
Ns_ConfigGetSections(void)
{
    Ns_Set        **sets;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  search;
    int     	    n;
    
    n = sections.numEntries + 1;
    sets = ns_malloc(sizeof(Ns_Set *) * n);
    n = 0;
    hPtr = Tcl_FirstHashEntry(&sections, &search);
    while (hPtr != NULL) {
    	sets[n++] = Tcl_GetHashValue(hPtr);
    	hPtr = Tcl_NextHashEntry(&search);
    }
    sets[n] = NULL;
    return sets;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConfigGetSection --
 *
 *	Return the Ns_Set of a config section called section. 
 *
 * Results:
 *	An Ns_Set, containing the section's parameters 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_ConfigGetSection(char *section)
{
    return (section ? GetSection(section, 0) : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetVersion
 *
 *	Get the major, minor, and patchlevel version numbers and
 *      the release type. A patch is a release type NS_FINAL_RELEASE
 *      with a patchLevel > 0.
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
Ns_GetVersion(majorV, minorV, patchLevelV, type)
    int *majorV;
    int *minorV;
    int *patchLevelV;
    int *type;
{
    if (majorV != NULL) {
        *majorV = NS_MAJOR_VERSION;
    }
    if (minorV != NULL) {
        *minorV = NS_MINOR_VERSION;
    }
    if (patchLevelV != NULL) {
        *patchLevelV = NS_RELEASE_SERIAL;
    }
    if (type != NULL) {
        *type = NS_RELEASE_LEVEL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsConfigRead --
 *
 *	Read a config file at startup.
 *
 * Results:
 *  	Pointer to the config buffer of an ns_malloc'ed string.
 *
 * Side Effects:
 *	Server aborts if file cannot be read for any reason.
 *
 *---------------------------------------------------------------------
 */

char *
NsConfigRead(char *file)
{
    struct stat st;
    int fd;
    char *buf;
    size_t n;

    if (stat(file, &st) != 0) {
	Ns_Fatal("config: stat(%s) failed: %s", file, strerror(errno));
    }
    if (S_ISREG(st.st_mode) == 0) {
	Ns_Fatal("config: not regular file: %s", file);
    }
    fd = open(file, O_RDONLY);
    if (fd < 0) {
	Ns_Fatal("config: open(%s) failed: %s", file, strerror(errno));
    }
    n = st.st_size;
    buf = ns_malloc(n + 1);
    n = read(fd, buf, n);
    if (n < 0) {
	Ns_Fatal("config: read(%s) failed: %s", file, strerror(errno));
    }
    buf[n] = '\0';
    close(fd);
    return buf;
}


/*
 *----------------------------------------------------------------------
 *
 * NsConfigEval --
 *
 *	Eval config script in a startup Tcl interp. 
 *
 * Results:
 *  	None.
 *
 * Side Effects:
 *      Various variables in the configInterp will be set as well as
 *      the sundry configuration hashes
 *
 *---------------------------------------------------------------------
 */

void
NsConfigEval(char *config, int argc, char **argv, int optind)
{
    char buf[20];
    Tcl_Interp *interp;
    Ns_Set     *set;
    int i;

    /*
     * Create an interp with a few config-related commands.
     */

    set = NULL;
    interp = Ns_TclCreateInterp();
    Tcl_CreateCommand(interp, "ns_section", SectionCmd, &set, NULL);
    Tcl_CreateCommand(interp, "ns_param", ParamCmd, &set, NULL);
    for (i = 0; argv[i] != NULL; ++i) {
	Tcl_SetVar(interp, "argv", argv[i], TCL_APPEND_VALUE|TCL_LIST_ELEMENT|TCL_GLOBAL_ONLY);
    }
    sprintf(buf, "%d", argc);
    Tcl_SetVar(interp, "argc", buf, TCL_GLOBAL_ONLY);
    sprintf(buf, "%d", optind);
    Tcl_SetVar(interp, "optind", buf, TCL_GLOBAL_ONLY);
    if (Tcl_Eval(interp, config) != TCL_OK) {
	Ns_TclLogError(interp);
	Ns_Fatal("config error");
    }
    Ns_TclDestroyInterp(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * ConfigGet --
 *
 *	Return the value for key in the config section. 
 *
 * Results:
 *	Pointer to value, or NULL 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static char *
ConfigGet(char *section, char *key, int exact)
{
    Ns_Set         *set;
    int             i;
    char           *s;

    s = NULL;
    if (section != NULL && key != NULL) {
        set = Ns_ConfigGetSection(section);
        if (set != NULL) {
	    if (exact) {
            	i = Ns_SetFind(set, key);
	    } else {
            	i = Ns_SetIFind(set, key);
	    }
            if (i >= 0) {
                s = Ns_SetValue(set, i);
            }
        }
    }
    return s;
}


/*
 *----------------------------------------------------------------------
 *
 * ParamCmd --
 *
 *	Add a single nsd.ini parameter; this command may only be run 
 *	from within an ns_section. 
 *
 * Results:
 *	Standard Tcl Result 
 *
 * Side effects:
 *	A tcl variable will be created with the name 
 *	ns_cfgdata(section,key), and a set entry will be added for 
 *	the current section. 
 *
 *----------------------------------------------------------------------
 */

static int
ParamCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv)
{
    Ns_Set *set;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		argv[0], " key value", NULL);
	return TCL_ERROR;
    }
    set = *((Ns_Set **) arg);
    if (set == NULL) {
	Tcl_AppendResult(interp, argv[0],
			 " not preceded by an ns_section command.", NULL);
	return TCL_ERROR;
    }
    Ns_SetPut(set, (char*)argv[1], (char*)argv[2]);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SectionCmd --
 *
 *	This creates a new config section and sets a shared variable
 *	to point at a newly-allocated set for holding config data.
 *	ns_param stores config data in the set.
 *
 * Results:
 *	Standard tcl result. 
 *
 * Side effects:
 *	Section set is created (if necessary).
 *
 *----------------------------------------------------------------------
 */

static int
SectionCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv)
{
    Ns_Set  **set;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 (char*)argv[0], " sectionname", NULL);
	return TCL_ERROR;
    }
    set = (Ns_Set **) arg;
    *set = GetSection((char*)argv[1], 1);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetSection --
 *
 *	Creates and/or gets a config section.
 *
 * Results:
 *	Pointer to new or existing Ns_Set for given section.
 *
 * Side effects:
 *	Section set created (if necessary).
 *
 *----------------------------------------------------------------------
 */

static Ns_Set *
GetSection(char *section, int create)
{
    Ns_DString ds;
    Tcl_HashEntry *hPtr;
    int new;
    Ns_Set *set;
    char *s;

    /*
     * Clean up section name to all lowercase, trimming space
     * and swapping silly backslashes.
     */

    Ns_DStringInit(&ds);
    s = section;
    while (isspace(UCHAR(*s))) {
	++s;
    }
    Ns_DStringAppend(&ds, s);
    s = ds.string;
    while (*s != '\0') {
	if (*s == '\\') {
	    *s = '/';
	} else if (isupper(UCHAR(*s))) {
	    *s = tolower(UCHAR(*s));
	}
	++s;
    }
    while (--s > ds.string && isspace(UCHAR(*s))) {
	*s = '\0';
    }
    section = ds.string;

    /*
     * Return config set, creating if necessary.
     */
 
    set = NULL;
    if (!create) {
	hPtr = Tcl_FindHashEntry(&sections, section);
    } else {
    	hPtr = Tcl_CreateHashEntry(&sections, section, &new);
    	if (new) {
	    set = Ns_SetCreate(section);
	    Tcl_SetHashValue(hPtr, set);
	}
    }
    if (hPtr != NULL) {
	set = Tcl_GetHashValue(hPtr);
    }
    Ns_DStringFree(&ds);
    return set;
}
