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
 * config.c --
 *
 *	Support for the configuration file
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/config.c,v 1.2 2000/05/02 14:39:30 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#define ISSLASH(c)      ((c) == '/' || (c) == '\\')

/*
 * Local procedures defined in this file.
 */

static Tcl_CmdProc SectionCmd;
static Tcl_CmdProc ParamCmd;
static Ns_Set     *GetSection(char *section, int create);
static char       *MakeSection(Ns_DString *pds, char *string);
static void        ParseConfig(char *file, char *config);
static void	   ParseAuxConfig(void);
static char       *ConfigGet(char *section, char *key,
			     int (*findproc) (Ns_Set *, char *));

/*
 * Global variables defined in this file.
 */

static Tcl_HashTable configSections;
static int initialized;


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
    return ConfigGet(section, key, Ns_SetIFind);
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
    return ConfigGet(section, key, Ns_SetFind);
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

    s = Ns_ConfigGet(section, key);
    if (s == NULL) {
        return NS_FALSE;
    } else if (sscanf(s, "%d", valuePtr) != 1) {
        Ns_Log(Warning, "config: could not convert [%s]%s=\"%s\" to int",
            	  section, key, s);
        return NS_FALSE;
    }

    return NS_TRUE;
}

#ifndef WIN32

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

    s = Ns_ConfigGet(section, key);
    if (s == NULL) {
        return NS_FALSE;
    } else if (sscanf(s, NS_INT_64_FORMAT_STRING, valuePtr) != 1) {
        Ns_Log(Warning, "config: could not convert [%s]%s=\"%s\" to int64",
            	  section, key, s);
        return NS_FALSE;
    }

    return NS_TRUE;
}
#endif


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

    s = Ns_ConfigGet(section, key);
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
        Ns_Log(Warning, "config: could not convert [%s]%s=\"%s\" to bool",
            	  section, key, s);
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
    Ns_Set         *setPtr;

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

    setPtr = Ns_ConfigSection(ds.string);
    Ns_DStringFree(&ds);

    return (setPtr ? Ns_SetName(setPtr) : NULL);
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
    Ns_Set        **setPtrPtr;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  search;
    int     	    n;
    
    if (!initialized) {
	setPtrPtr = ns_calloc(1, sizeof(Ns_Set *));
    } else {
	n = configSections.numEntries + 1;
        setPtrPtr = ns_malloc(sizeof(Ns_Set *) * n);
	n = 0;
        hPtr = Tcl_FirstHashEntry(&configSections, &search);
    	while (hPtr != NULL) {
    	    setPtrPtr[n++] = Tcl_GetHashValue(hPtr);
    	    hPtr = Tcl_NextHashEntry(&search);
        }
        setPtrPtr[n] = NULL;
    }
    return setPtrPtr;
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
    off_t n;

    if (stat(file, &st) != 0) {
	Ns_Fatal("config: stat(%s) failed: %s", file, strerror(errno));
    }
    if (S_ISREG(st.st_mode) == 0) {
	Ns_Fatal("config: not regular file: %s", file);
    }
    fd = open(file, O_RDONLY|O_TEXT);
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
NsConfigEval(char *config)
{
    char *err;
    Tcl_Interp *interp;
    Ns_Set     *setPtr;

    /*
     * Create an interp with a few config-related commands.
     */

    setPtr = NULL;
    interp = Tcl_CreateInterp();
    Tcl_CreateCommand(interp, "ns_log", NsTclLogCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "ns_info", NsTclInfoCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "ns_config", NsTclConfigCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "ns_section", SectionCmd, &setPtr, NULL);
    Tcl_CreateCommand(interp, "ns_param", ParamCmd, &setPtr, NULL);

    if (Tcl_Eval(interp, config) != TCL_OK) {
	err = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
	if (err == NULL) {
	    err = interp->result;
	}
	Ns_Fatal("config: script error: %s", err);
    }
    Tcl_DeleteInterp(interp);
    ParseAuxConfig();
}


/*
 *----------------------------------------------------------------------
 *
 * NsConfigParse --
 *
 *	The public interface to read the configuration file. 
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *	Initial config parsed.
 *
 *----------------------------------------------------------------------
 */

void
NsConfigParse(char *file, char *config)
{
    ParseConfig(file, config);
    ParseAuxConfig();
}


/*
 *----------------------------------------------------------------------
 * ParseAuxConfig --
 *
 *	Read just the aux ini configuration files, if any.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *	Additional config may be parsed.
 *
 *----------------------------------------------------------------------
 */

static void
ParseAuxConfig(void)
{
    char        *auxdir, *config, *file;

    /*
     * Read any auxiliary config files.
     */

    auxdir = Ns_ConfigGetValue(NS_CONFIG_PARAMETERS, "auxconfigdir");
    if (auxdir != NULL) {
	struct stat     st;
	DIR            *dp;
	struct dirent  *ent;
	Ns_List        *files, *m;
	Ns_DString	ds1, ds2;
	char	       *p;

    	Ns_DStringInit(&ds1);
	Ns_DStringInit(&ds2);
	
	/*
    	 * Locate and/or verify the AuxConfigDir.
	 */
	 
	if (Ns_PathIsAbsolute(auxdir) == 0) {
    	    p = Ns_ConfigGetValue(NS_CONFIG_PARAMETERS, "home");
	    if (p == NULL) {
	    	Ns_Fatal("config: cannot locate auxconfigdir: %s", auxdir);
	    }
    	    auxdir = Ns_MakePath(&ds2, p, auxdir, NULL);
	}
	if (stat(auxdir, &st) != 0) {
	    Ns_Fatal("config: stat(%s) failed: %s", auxdir, strerror(errno));
	}
	if (S_ISDIR(st.st_mode) == 0) {
	    Ns_Fatal("config: not a directory: %s", auxdir);
	}
	
	/*
	 * Read and sort all files ending in .ini in the AuxConfigDir.
	 */
	 
	dp = opendir(auxdir);
	if (dp == NULL) {
	    Ns_Fatal("config: opendir(%s) failed: %s",
		     auxdir, strerror(errno));
	}
    	files = NULL;
	while ((ent = ns_readdir(dp)) != NULL) {

	    /*
	     * Ignore files with a dot or hash as first character.
	     */
	    if ( (ent->d_name[0] == '.')
		 || (ent->d_name[0] == '#') ) {
		continue;
	    }
	    
	    p = strrchr(ent->d_name, '.');
	    if (p != NULL && STREQ(p, ".ini")) {
		Ns_DStringTrunc(&ds1,0);
	    	Ns_MakePath(&ds1, auxdir, ent->d_name, NULL);
		Ns_ListPush(Ns_DStringExport(&ds1), files);
	    }
	}
	files = Ns_ListSort(files, (Ns_SortProc *) strcmp);
	closedir(dp);

    	/*
	 * Open and parse each .ini file.
	 */
	 
	for (m = files; m != NULL; m = Ns_ListRest(m)) {
	    file = Ns_ListFirst(m);
	    config = NsConfigRead(file);
	    ParseConfig(file, config);
	    ns_free(config);
	}
	
	Ns_ListFree(files, ns_free);
	Ns_DStringFree(&ds1);
	Ns_DStringFree(&ds2);
    }
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
ConfigGet(char *section, char *key, int (*findproc) (Ns_Set *, char *))
{
    Ns_Set         *setPtr;
    int             i;
    char           *s;

    s = NULL;
    if (section != NULL && key != NULL) {
        setPtr = Ns_ConfigSection(section);
        if (setPtr != NULL) {
            i = (*findproc) (setPtr, key);
            if (i >= 0) {
                s = Ns_SetValue(setPtr, i);
            }
        }
    }

    return s;
}


/*
 *----------------------------------------------------------------------
 *
 * MakeSection --
 *
 *	Make sure section and key names are trimmed and lowercase and 
 *	all backslashes (\) are converted to forward slashses (/) 
 *
 * Results:
 *	Pointer to fixed up string 
 *
 * Side effects:
 *	The string will be trimmed, lowercased, and slashes pointed 
 *	forwards. 
 *
 *----------------------------------------------------------------------
 */

static char *
MakeSection(Ns_DString *dsPtr, char *string)
{
    char           *start;
    register char  *s;

    Ns_DStringAppend(dsPtr, string);
    start = Ns_StrTrim(dsPtr->string);
    for (s = start; *s != '\0'; ++s) {
        if (*s == '\\') {
            *s = '/';
        } else if (isupper(UCHAR(*s))) {
            *s = tolower(UCHAR(*s));
        }
    }

    return start;
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
ParamCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Set *setPtr;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		argv[0], " key value", NULL);
	return TCL_ERROR;
    }
    setPtr = *((Ns_Set **) arg);
    if (setPtr == NULL) {
	Tcl_AppendResult(interp, argv[0],
			 " not preceded by an ns_section command.", NULL);
	return TCL_ERROR;
    }
    Ns_SetPut(setPtr, argv[1], argv[2]);

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
SectionCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Set  **setPtrPtr;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " sectionname", NULL);
	return TCL_ERROR;
    }
    setPtrPtr = (Ns_Set **) arg;
    *setPtrPtr = GetSection(argv[1], 1);

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
    Ns_Set *setPtr;

    if (!initialized) {
	Tcl_InitHashTable(&configSections, TCL_STRING_KEYS);
	initialized = 1;
    }

    Ns_DStringInit(&ds);
    setPtr = NULL;
    MakeSection(&ds, section);    
    if (!create) {
	hPtr = Tcl_FindHashEntry(&configSections, ds.string);
    } else {
    	hPtr = Tcl_CreateHashEntry(&configSections, ds.string, &new);
    	if (new) {
	    setPtr = Ns_SetCreate(section);
	    Tcl_SetHashValue(hPtr, setPtr);
	}
    }
    if (hPtr != NULL) {
	setPtr = Tcl_GetHashValue(hPtr);
    }
    Ns_DStringFree(&ds);
    return setPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * ParseConfig --
 *
 *	Parse config lines in .ini format.
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	Ns_Sets will be created for each section and linked to from 
 *	the configSections hash table.
 *
 *----------------------------------------------------------------------
 */

static void
ParseConfig(char *file, char *config)
{
    Ns_DString	    ds;
    Ns_Set         *setPtr;
    char    	   *line, *nextLine, *s, *e;

    Ns_DStringInit(&ds);
    setPtr = NULL;

    for (line = config; line != NULL; line = nextLine) {

    	/*
	 * Locate and trim next line.
	 */
	 
	nextLine = strchr(line, '\n');
	if (nextLine != NULL) {
	    *nextLine++ = '\0';
	}
	line = Ns_StrTrim(line);

	switch (*line) {
	    case ';':
	    case '\0':

		/*
		 * Skip comments and blank lines.
		 */

		break;

	    case '[':

		/*
		 * Parse section name and begin new section.
		 */
		 
		s = line + 1;
		e = strchr(s, ']');
		if (e == NULL) {
		    Ns_Log(Warning, "config: invalid section name: %s", line);
		} else {
		    *e = '\0';
		    if (*s == '\0') {
			Ns_Log(Warning, "config: null section name");
		    } else {
			setPtr = GetSection(s, 1);
		    }
		}
		break;

	    default:

		/*
		 * Parse a section entry.
		 */
		 
		if (setPtr == NULL) {
		    Ns_Log(Warning, "config: entry before section: %s", line);
		} else {
		    e = strchr(line, '=');
		    if (e == NULL) {
			Ns_Log(Warning, "config: invalid entry: %s", line);
		    } else {
			*e++ = '\0';
			e = Ns_StrTrim(e);
			s = Ns_StrTrim(line);
			Ns_SetPut(setPtr, s, e);
		    }
		}
		break;
	}
    }
    Ns_DStringFree(&ds);
}

