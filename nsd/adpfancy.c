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
 * adpfancy.c --
 *
 *	Support for registered tags within ADPs, the <script> tag,
 *      uncached ADPs, and streaming. This is a good example of
 *	the Ns_AdpRegisterParser() API call.
 */

/*
 * Hacked by Karl Goldstein and Jim Guggemos:
 * (1) Removed check for quotes so that registered tags can be placed
 * within quoted attributes (i.e. <a href="<mytag>">)
 * (2) Added BalancedEndTag function so that registered tags can be
 * nested (i.e. <mytag> ... <mytag> ... </mytag>  ... </mytag>
 *
 */
 
/* 4/19/00 : Hacked by mcazzell@lovemail.com [moe]
 * Removed check for comments so that server side includes
 * or similar constructs can be registered with fancy tags.
 */ 
static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/adpfancy.c,v 1.6 2000/10/17 14:26:27 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Config file stuff
 */
#define CONFIG_TAGLOCKS "TagLocks"

#define DEFAULT_TAGLOCKS NS_FALSE

/*
 * Types
 */

typedef struct {
    char          *tag;        /* The name of the tag (e.g., "netscape") */
    char          *endtag;     /* The closing tag or null (e.g., "/netscape")*/
    char          *procname;   /* TCL proc handler (e.g., "ns_adp_netscape") */
    char          *adpstring;  /* ADP to evaluate */
} RegTag;

/*
 * Local functions defined in this file
 */

static void      StartText(Ns_DString *dsPtr);
static void      StartScript(Ns_DString *dsPtr);
static void      NAppendChunk(Ns_DString *dsPtr, char *text, int length);
static void      AppendChunk(Ns_DString *dsPtr, char *text);
static void      EndChunk(Ns_DString *dsPtr);
static void      AddTextChunk(Ns_DString *dsPtr, char *text, int length);
static void      AppendTclEscaped(Ns_DString *ds, char *in);
static void      NAppendTclEscaped(Ns_DString *ds, char *in, int n);
static void      FancyParsePage(Ns_DString *outPtr, char *in);
static Ns_Set   *TagToSet(char *sTag);
static char     *ReadToken(char *in, Ns_DString *tagPtr);
static char     *BalancedEndTag(char *in, RegTag *rtPtr);
static RegTag   *GetRegTag(char *tag);

/*
 * Static variables
 */

static Tcl_HashTable htTags;
static Ns_RWLock     tlock;


/*
 *----------------------------------------------------------------------
 * NsAdpFancyInit --
 *
 *      Initialization function.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpFancyInit(char *hServer, char *path)
{
    Tcl_InitHashTable(&htTags, TCL_STRING_KEYS);

    /*
     * Register ourselves.
     */
    
    Ns_AdpRegisterParser("fancy", FancyParsePage);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterTagCmd --
 *
 *	Register an ADP tag.
 *	
 *
 * Results:
 *	Std tcl retval.
 *
 * Side effects:
 *	An ADP tag may be added to the hashtable.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRegisterTagCmd(ClientData ignored, Tcl_Interp *interp, int argc,
		    char **argv)
{
    char           *tag, *endtag, *proc;
    Tcl_HashEntry  *he;
    int             new;
    RegTag         *rtPtr;
    AdpData        *adPtr;
    
    if (argc != 4 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " tag ?endtag? proc\"", NULL);
	return TCL_ERROR;
    }

    if (argc == 3) {
	tag = argv[1];
	endtag = NULL;
	proc = argv[2];
    } else {
	tag = argv[1];
	endtag = argv[2];
	proc = argv[3];

    }
    adPtr = NsAdpGetData();
    if (nsconf.adp.taglocks) {
	/* LOCK */
	if (adPtr->depth > 0) {
	    /*
	     * Called from within an ADP, so unlock the read lock first
	     * to prevent deadlock
	     */
	    Ns_RWLockUnlock(&tlock);
	}
	Ns_RWLockWrLock(&tlock);
    }

    he = Tcl_CreateHashEntry(&htTags, tag, &new);
    if (new == 0) {
	Tcl_AppendResult(interp, "ADP tag \"", tag, "\" already registered.",
			 NULL);
	return TCL_ERROR;
    }
    rtPtr = ns_malloc(sizeof(RegTag));
    rtPtr->tag = ns_strdup(tag);
    rtPtr->endtag = endtag ? ns_strdup(endtag) : NULL;
    rtPtr->procname = ns_strdup(proc);
    rtPtr->adpstring = NULL;
    Tcl_SetHashValue(he, (void *) rtPtr);

    if (nsconf.adp.taglocks) {
	/* UNLOCK */
	Ns_RWLockUnlock(&tlock);
	if (adPtr->depth > 0) {
	    /*
	     * We were called from within an ADP, so re-lock the read
	     * lock now.
	     */
	    Ns_RWLockRdLock(&tlock);
	}
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterAdpCmd --
 *
 *	Register an ADP tag which is an ADP.
 *	
 *
 * Results:
 *	Std tcl retval.
 *
 * Side effects:
 *	An ADP tag may be added to the hashtable.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRegisterAdpCmd(ClientData ignored, Tcl_Interp *interp, int argc,
		    char **argv)
{
    char           *tag, *endtag, *adpstring;
    Tcl_HashEntry  *he;
    int             new;
    RegTag         *rtPtr;
    AdpData        *adPtr;
    
    if (argc != 4 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " tag ?endtag? adpstring\"", NULL);
	return TCL_ERROR;
    }

    if (argc == 3) {
	tag = argv[1];
	endtag = NULL;
	adpstring = argv[2];
    } else {
	tag = argv[1];
	endtag = argv[2];
	adpstring = argv[3];

    }
    adPtr = NsAdpGetData();
    if (nsconf.adp.taglocks) {
	/* LOCK */
	if (adPtr->depth > 0) {
	    /*
	     * Called from within an ADP, so unlock the read lock first
	     * to prevent deadlock
	     */
	    Ns_RWLockUnlock(&tlock);
	}
	Ns_RWLockWrLock(&tlock);
    }

    he = Tcl_CreateHashEntry(&htTags, tag, &new);
    if (new == 0) {
	Tcl_AppendResult(interp, "ADP tag \"", tag, "\" already registered.",
			 NULL);
	return TCL_ERROR;
    }
    rtPtr = ns_malloc(sizeof(RegTag));
    rtPtr->tag = ns_strdup(tag);
    rtPtr->endtag = endtag ? ns_strdup(endtag) : NULL;
    rtPtr->procname = NULL;
    rtPtr->adpstring = ns_strdup(adpstring);
    Tcl_SetHashValue(he, (void *) rtPtr);

    if (nsconf.adp.taglocks) {
	/* UNLOCK */
	Ns_RWLockUnlock(&tlock);
	if (adPtr->depth > 0) {
	    /*
	     * We were called from within an ADP, so re-lock the read
	     * lock now.
	     */
	    Ns_RWLockRdLock(&tlock);
	}
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * StartScript --
 *
 *	Start a chunk which is a script.
 *
 * Results:
 *	The passed-in dstring has a chunk header appended to it.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
StartScript(Ns_DString *dsPtr)
{
    Ns_DStringNAppend(dsPtr, "s", 1);
}


/*
 *----------------------------------------------------------------------
 *
 * StartText --
 *
 *	Start a chunk which is text.
 *
 * Results:
 *	The passed-in dstring has a chunk header appended to it.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
StartText(Ns_DString *dsPtr)
{
    Ns_DStringNAppend(dsPtr, "t", 1);
}


/*
 *----------------------------------------------------------------------
 *
 * NAppendChunk --
 *
 *	Append a string of length N to the chunk.
 *
 * Results:
 *	The passed-in dstring has a chunk appended to it.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
NAppendChunk(Ns_DString *dsPtr, char *text, int length)
{
    Ns_DStringNAppend(dsPtr, text, length);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendChunk --
 *
 *	Append a string to the chunk.
 *
 * Results:
 *	The passed-in dstring has a chunk appended to it.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AppendChunk(Ns_DString *dsPtr, char *text)
{
    Ns_DStringAppend(dsPtr, text);
}


/*
 *----------------------------------------------------------------------
 *
 * EndChunk --
 *
 *	End a chunk
 *
 * Results:
 *	The passed-in dstring has a null appended to it.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
EndChunk(Ns_DString *dsPtr)
{
    Ns_DStringNAppend(dsPtr, "", 1);
}


/*
 *----------------------------------------------------------------------
 *
 * AddTextChunk --
 *
 *	Given a string of HTML, convert it into a text chunk suitable
 *      for caching.
 *
 * Results:
 *	The passed-in dstring has a chunk appended to it.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AddTextChunk(Ns_DString *dsPtr, char *text, int length)
{
    StartText(dsPtr);
    NAppendChunk(dsPtr, text, length);
    EndChunk(dsPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendTclEscaped --
 *
 *	Append a string to a dstring after escaping it for Tcl so that
 *      it is suitable for putting inside "...". The translations are:
 *	" -> \"
 *      $ -> \$
 *      \ -> \\
 *      [ -> \[
 *
 * Results:
 *	The dstring will have stuff appended to it that is tcl-safe.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AppendTclEscaped(Ns_DString *ds, char *in)
{
    while (*in) {
	if (*in == '"'  ||
	    *in == '$'  ||
	    *in == '\\' ||
	    *in == '[') {
	    Ns_DStringAppend(ds, "\\");
	}
	Ns_DStringNAppend(ds, in, 1);
	in++;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NAppendTclEscaped --
 *
 *      Same as AppendTclEscaped, but only do up to N bytes.
 *
 * Results:
 *	See AppendTclEscaped.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
NAppendTclEscaped(Ns_DString *ds, char *in, int n)
{
    while (*in && (n-- > 0)) {
	if (*in == '"'  ||
	    *in == '$'  ||
	    *in == '\\' ||
	    *in == '[') {

	    Ns_DStringAppend(ds, "\\");
	}
	Ns_DStringNAppend(ds, in, 1);
	in++;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * FancyParsePage --
 *
 *	Takes an ADP as input and forms a chunky ADP as output.
 *
 * Results:
 *	A chunked ADP is put in outPtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
FancyParsePage(Ns_DString *outPtr, char *in)
{
    char       *top, *oldtop, *end;
    Ns_DString  tag;
    Ns_Set     *params;
    RegTag     *rtPtr;
    
    /*
     * Tags we care about:
     * <%
     * <script
     * <regtag
     */

    Ns_DStringInit(&tag);
    oldtop = top = in;
    
    if (nsconf.adp.taglocks) {
	Ns_RWLockRdLock(&tlock);
    }
    while ((top = ReadToken(top, &tag)) != NULL) {
	/*
	 * top points at the character after the tag.
	 * tag is a dstring containg either one tag or a bunch of text.
	 * oldtop points at where tag starts in the original string.
	 *
	 * <script language=tcl runat=server> ns_puts hi
	 * ^                                 ^
	 * oldtop                            top
	 *
	 * tag="<script language=tcl runat=server>"
	 */
	
	if (strncmp(tag.string, "<%", 2) == 0) {
	    /*
	     * Find %> tag and add script if there, otherwise spit out
	     * a warning and add as text.
	     */
	    end = strstr(top, "%>");
	    if (end == NULL) {
		Ns_Log(Warning, "adpfancy: unterminated script");
		AddTextChunk(outPtr, oldtop, strlen(oldtop));
		break;
	    } else {
		StartScript(outPtr);
		if (tag.string[2] == '=') {
		    NAppendChunk(outPtr, "ns_puts -nonewline ",
 				  sizeof("ns_puts -nonewline ")-1);
		}
		NAppendChunk(outPtr, top, end-top);
		EndChunk(outPtr);
		top = end + 2;
	    }
	} else if (strncasecmp(tag.string, "<script", 7) == 0) {
	    char *lang, *runat, *stream;
	    /*
	     * Get the paramters to the tag and then add the
	     * script chunk if appropriate, otherwise it's just
	     * text
	     */
	    
	    params = TagToSet(tag.string);
	    lang = Ns_SetIGet(params, "language");
	    stream = Ns_SetIGet(params, "stream");
	    runat = Ns_SetIGet(params, "runat");
	    if (runat != NULL &&
		strcasecmp(runat, "server") == 0 &&
		(lang == NULL || strcasecmp(lang, "tcl") == 0)) {

		/*
		 * This is a server-side script chunk!
		 * If there is an end tag, add it as a script, else
		 * spit out a warning and add as text.
		 */
		
		end = Ns_StrNStr(top, "</script>");
		if (end == NULL) {
		    Ns_Log(Warning, "adpfancy: unterminated script");
		    AddTextChunk(outPtr, oldtop, strlen(oldtop));
                    Ns_SetFree(params);
		    break;
		} else {
		    StartScript(outPtr);
		    if (stream != NULL && strcasecmp(stream, "on") == 0) {
			AppendChunk(outPtr, "ns_adp_stream\n");
		    }
		    NAppendChunk(outPtr, top, end-top);
		    EndChunk(outPtr);
		    top = end + 9;
		}
	    } else {
		/*
		 * Not a server-side script, so add as text.
		 */
		
		AddTextChunk(outPtr, tag.string, tag.length);
	    }
            Ns_SetFree(params);
	} else if (tag.string[0] == '<'
	    && (rtPtr = GetRegTag(tag.string + 1)) != NULL) {

	    /*
	     * It is a registered tag. In this case, we generate
	     * a bolus of tcl code that will call it.
	     */

	    int         i;
	    char       *end = NULL;

	    params = TagToSet(tag.string);

	    /*
	     * If it requires an endtag then ensure that there
	     * is one. If not, warn and spew text.
	     */
	    
	    if (rtPtr->endtag &&
		((end = BalancedEndTag(top, rtPtr)) == NULL)) {

		Ns_Log(Warning, "adpfancy: unterminated registered tag '%s'",
		       rtPtr->tag);
		AddTextChunk(outPtr, oldtop, strlen(oldtop));
                Ns_SetFree(params);
		break;
	    }

	    /*
	     * Write Tcl code to put all the parameters into a set, then
	     * call the proc with that set (and the input, if any).
	     */
	    
	    StartScript(outPtr);
	    AppendChunk(outPtr, "set _ns_tempset [ns_set create \"\"]\n");
	    for (i=0; i < Ns_SetSize(params); i++) {
		AppendChunk(outPtr, "ns_set put $_ns_tempset \"");
		AppendTclEscaped(outPtr, Ns_SetKey(params, i));
		AppendChunk(outPtr, "\" \"");
		AppendTclEscaped(outPtr, Ns_SetValue(params, i));
	        AppendChunk(outPtr, "\"\n");
	    }
	    AppendChunk(outPtr, "ns_puts -nonewline [");
	    if (rtPtr->procname) {
		/*
		 * This uses the old-style registered procedure
		 */
		AppendChunk(outPtr, rtPtr->procname);
	    } else {
		/*
		 * This uses the new and improved registered ADP.
		 */
		AppendChunk(outPtr, "ns_adp_eval \"");
		AppendTclEscaped(outPtr, rtPtr->adpstring);
		AppendChunk(outPtr, "\" ");
	    }
	    AppendChunk(outPtr, " ");

	    /*
	     * Backwards compatibility is broken here because a conn is
	     * never passed
	     */
	    if (end != NULL) {
		/*
		 * This takes an endtag, so pass it content (the text between
		 * the start and end tags).
		 */
		AppendChunk(outPtr, "\"");
		NAppendTclEscaped(outPtr, top, end-top-1);
		AppendChunk(outPtr, "\" ");
	    }
	    AppendChunk(outPtr, "$_ns_tempset]\n");
	    EndChunk(outPtr);

	    /*
	     * Advance top past the end of the close tag
	     * (if there is no closetag, top should already be
	     * properly advanced thanks get ReadToken)
	     */
	    if (end != NULL) {
		while (*end != '\0' && *end != '>') {
		    end++;
		}
		if (*end == '>') {
		    end++;
		}
		top = end;
	    }
            Ns_SetFree(params);
	} else {
	    /*
	     * It's just a chunk of text.
	     */

	    AddTextChunk(outPtr, tag.string, tag.length);
	}
	Ns_DStringTrunc(&tag, 0);

	oldtop = top;
    }
    if (nsconf.adp.taglocks) {
	Ns_RWLockUnlock(&tlock);
    }
    Ns_DStringFree(&tag);
}


/*
 *----------------------------------------------------------------------
 *
 * TagToSet --
 *
 *	Given a tag such as <script runat=server language=tcl> make
 *	an ns_set containing runat=server, language=tcl.
 *
 * Results:
 *	The above described set.
 *
 * Side effects:
 *	An Ns_Set is allocated.
 *
 *      NOTE: ALWAYS DO Ns_SetFree() THE RESULT OF THIS FUNCTION!
 *
 *----------------------------------------------------------------------
 */

static Ns_Set *
TagToSet(char *sTag)
{
    char *p, c;
    Ns_Set *set;

    /* Skip the opening '>' */
    p = ++sTag;
    /* Get the tag name */
    while (isspace(UCHAR(*sTag)) == 0 &&
	*sTag != '\0' &&
	*sTag != '>') {

	sTag++;
    }
    c = *sTag;
    *sTag = '\0';
    set = Ns_SetCreate(p);
    *sTag = c;
    /* Handle the attribute = value pairs */
    do {
	/* Skip blanks */
	while (*sTag != '\0' && isspace(UCHAR(*sTag))) {
	    sTag++;
	}
	if (*sTag == '>') {
	    break;
	}
	/* Get attr name */
	p = sTag;
	while (*sTag != '\0' &&
	    isspace(UCHAR(*sTag)) == 0 &&
	    *sTag != '=' && *sTag != '>') {

	    sTag++;
	}
	c = *sTag;
	*sTag = '\0';
	Ns_SetPut(set, p, NULL);
	*sTag = c;
	/* Skip blanks */
	while (*sTag != '\0' && isspace(UCHAR(*sTag))) {
	    sTag++;
	}
	if (*sTag == '=') {
	    /* get attr value */
	    sTag++;
	    /* Skip blanks */
	    while (*sTag != '\0' && isspace(UCHAR(*sTag))) {
		sTag++;
	    }
	    if (*sTag == '"') {
		/* get attr value in double quotes */
		sTag++;
		p = sTag;
		while (*sTag != '\0' && *sTag != '"') {
		    sTag++;
		}
		c = *sTag;
		*sTag = '\0';
		set->fields[set->size -1].value = ns_strcopy(p);
		*sTag = c;
		if (*sTag == '"') {
		    sTag++;
		}
	    } else {
		/* get attr value bounded by whitespace or end of tag */
		p = sTag;
		while (*sTag != '\0' &&
		    isspace(UCHAR(*sTag)) == 0 &&
		    *sTag != '>') {

		    sTag++;
		}
		c = *sTag;
		*sTag = '\0';
		set->fields[set->size -1].value = ns_strcopy(p);
		*sTag = c;
	    }
      } else {
	  set->fields[set->size-1].value = 
	    ns_strcopy(set->fields[set->size-1].name);
	}
    } while (*sTag != '\0' && *sTag != '>');

    return set;
}


/*
 *----------------------------------------------------------------------
 *
 * ReadToken --
 *
 *	Read the passed-in dstring until a token is found and return it
 *	in a dstring.
 *
 * Results:
 *	NULL on failure (including EOF), or a pointer to the next char
 *      in the input stream.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
ReadToken(char *in, Ns_DString *tagPtr)
{
    char c;
    int quoting = 0;
    
    if ((c = *in++) == '\0') {
	return NULL;
    }

    Ns_DStringNAppend(tagPtr, &c, 1);
    /* Starting to read a tag */
    if (c == '<') {
	if ((c = *in++) == '\0') {
	    in--;
	    goto done;
	}

	Ns_DStringNAppend(tagPtr, &c, 1);
	if (c == '%') {
	    if ((c = *in++) == '\0') {
		in--;
		goto done;
	    }
	    if (c == '=') {
		Ns_DStringNAppend(tagPtr, &c, 1);
		goto done;
	    } else {
		in--;
		goto done;
	    }
	} else {
	    /* 4/19/00: [moe] Removed code which ignores HTML Comments

	    if (c == '!') {
		if ((c = *in++) == '\0') {
		    in--;
		    goto done;
		}
		Ns_DStringNAppend(tagPtr, &c, 1);
		if (c == '-') {
		    if ((c = *in++) == '\0') {
			goto done;
		    }
		    Ns_DStringNAppend(tagPtr, &c, 1);
		    if (c == '-') {
			goto done;
		    }
		}
	    }

	    */

	    while (c != '>' && (c = *in++) != '\0') {
	      if (c == '<') {
		    in--;
		    goto done;
		}
		Ns_DStringNAppend(tagPtr, &c, 1);
	    }
	    if (c == '\0') {
		in--;
	    }
	    goto done;
	}
    } else if (c == '%') {
	if ((c = *in++) == '\0') {
	    in--;
	    goto done;
	}
	Ns_DStringNAppend(tagPtr, &c, 1);
	if (c == '>') {
	    goto done;
	}
	while ((c = *in++) != '\0') {
	    if ((c == '<') || (c == '%')) {
		in--;
		break;
	    }
	    Ns_DStringNAppend(tagPtr, &c, 1);
	}
	if (c == '\0') {
	    in--;
	}
    } else {
	while ((c = *in++) != '\0') {
	    if (c == '<' || c == '%') {
		in--;
		break;
	    }
	    Ns_DStringNAppend(tagPtr, &c, 1);
	}
	if (c == '\0') {
	    in--;
	}
    }

 done:
    return in;
}


/*
 *----------------------------------------------------------------------
 *
 * GetRegTag --
 *
 *	Looks up in The Hash Table the passed-in regtag and returns the
 *	regtag struct where available. The passed in tag should look like:
 *      "tag a=b c=d>" or "tag>"
 *
 * Results:
 *	Either a pointer to the requested regtag or null if none exists.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static RegTag *
GetRegTag(char *tag)
{
    RegTag        *rtPtr;
    char          *end, temp;
    Tcl_HashEntry *he;
    
    rtPtr = NULL;
    end = tag;
    /*
     * Locate the end of the tag
     */
    while (*end != '\0' && *end != '>' &&
	isspace(UCHAR(*end)) == 0) {

	end++;
    }
    if (*end == '\0') {
	goto done;
    }
    temp = *end;
    *end = '\0';
    he = Tcl_FindHashEntry(&htTags, tag);
    *end = temp;
    if (he == NULL) {
	goto done;
    }
    rtPtr = Tcl_GetHashValue(he);
    
 done:
    return rtPtr;
}

static char * 
BalancedEndTag(char *in, RegTag *rtPtr)
{

  int tag_depth = 1;
  int taglen, endlen;

  taglen = strlen(rtPtr->tag);
  endlen = strlen(rtPtr->endtag);

  while (tag_depth) {

    /*  
     * Scan ahead for a '<'
     */

    for ( ; *in && *in != '<'; ++in ) ;
    if (*in == '\0') return NULL;

    /* skip '<' */
    ++in;

    /*
     * The current parser seems to allow white space between < and the tag
     */

    for ( ; *in && isspace(UCHAR(*in)); ++in ) ;
    if (*in == '\0') return NULL;

    /*  
     * If the next word matches the close tag, then decrement tag_depth
     */

    if (! strncasecmp(in, rtPtr->endtag, endlen)) {
      tag_depth--;
    }

    /*  
     * else if the next word matches the open tag, then increment tag_depth
     */

    else if (! strncasecmp(in, rtPtr->tag, taglen)) {
      tag_depth++;
    }
  }

  return in;
}

