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
 * adpparse.c --
 *
 *	ADP parser.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adpparse.c,v 1.20 2006/04/13 19:06:09 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define SERV_STREAM	1
#define SERV_RUNAT	2
#define SERV_NOTTCL	4

#define TAG_ADP		1
#define TAG_PROC	2
#define TAG_SCRIPT	3

#define APPEND		"ns_adp_append "
#define APPEND_LEN	(sizeof(APPEND)-1)

#define LENSZ		(sizeof(int))

/*
 * The following structure maintains proc and adp registered tags.
 * String bytes directly follow the Tag struct in the same allocated
 * block.
 */

typedef struct Tag {
    int		   type;   /* Type of tag, ADP or proc. */
    char          *tag;    /* The name of the tag (e.g., "netscape") */
    char          *endtag; /* The closing tag or null (e.g., "/netscape")*/
    char          *string; /* Proc (e.g., "ns_adp_netscape") or ADP string. */
} Tag;

typedef struct Parse {
    AdpCode	  *codePtr;
    int		   line;
    Tcl_DString	   lens;
    Tcl_DString    lines;
} Parse;

/*
 * Local functions defined in this file
 */

static void AppendBlock(Parse *parsePtr, char *s, char *e, int type);
static void AppendTag(Parse *parsePtr, Tag *tagPtr, char *as, char *ae, char *se);
static void ParseBlock(Parse *parsePtr, NsServer *servPtr, char *utf);
static int RegisterObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		       Tcl_Obj **objv, int type);
static void Blocks2Script(AdpCode *codePtr);
static void AppendLengths(AdpCode *codePtr, int *lens, int *lines);


/*
 *----------------------------------------------------------------------
 *
 * Ns_AdpRegisterParser --
 *
 *	Register an ADP parser (no longer supported).
 *
 * Results:
 *	NS_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_AdpRegisterParser(char *extension, Ns_AdpParserProc *proc)
{
    return NS_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpRegisterAdpObjCmd, NsTclAdpRegisterProcObjCmd,
 * NsTclAdpRegisterScriptObjCmd --
 *
 *	Register an proc, script, are ADP string tag.
 *	
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	An ADP tag may be added to the hashtable.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAdpRegisterAdpObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			  Tcl_Obj **objv)
{
    return RegisterObjCmd(arg, interp, objc, objv, TAG_ADP);
}

int
NsTclAdpRegisterProcObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			   Tcl_Obj **objv)
{
    return RegisterObjCmd(arg, interp, objc, objv, TAG_PROC);
}

int
NsTclAdpRegisterScriptObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		       Tcl_Obj **objv)
{
    return RegisterObjCmd(arg, interp, objc, objv, TAG_SCRIPT);
}


static int
RegisterObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		    Tcl_Obj **objv, int type)
{
    NsInterp       *itPtr = arg;
    NsServer	   *servPtr = itPtr->servPtr;
    char           *string, *end, *tag;
    Tcl_HashEntry  *hPtr;
    int             new, slen, elen;
    Tag            *tagPtr;
    
    if (objc != 4 && objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "tag ?endtag? [adp|proc]");
	return TCL_ERROR;
    }
    string = Tcl_GetStringFromObj(objv[objc-1], &slen);
    ++slen;
    if (objc == 3) {
	end = NULL;
	elen = 0;
    } else {
	end = Tcl_GetStringFromObj(objv[2], &elen);
	++elen;
    }
    tagPtr = ns_malloc(sizeof(Tag) + slen + elen);
    tagPtr->type = type;
    tagPtr->string = (char *) tagPtr + sizeof(Tag);
    memcpy(tagPtr->string, string, (size_t) slen);
    if (end == NULL) {
	tagPtr->endtag = NULL;
    } else {
	tagPtr->endtag = tagPtr->string + slen;
	memcpy(tagPtr->endtag, end, (size_t) elen);
    }
    tag = Tcl_GetString(objv[1]);
    Ns_RWLockWrLock(&servPtr->adp.taglock);
    hPtr = Tcl_CreateHashEntry(&servPtr->adp.tags, tag, &new);
    if (!new) {
	ns_free(Tcl_GetHashValue(hPtr));
    }
    Tcl_SetHashValue(hPtr, tagPtr);
    tagPtr->tag = Tcl_GetHashKey(&servPtr->adp.tags, hPtr);
    Ns_RWLockUnlock(&servPtr->adp.taglock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpParse --
 *
 *	Parse a string of ADP.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Given Parse structure filled in with copy of parsed ADP.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpParse(AdpCode *codePtr, NsServer *servPtr, char *utf, int flags)
{
    Parse parse;
    char *s, *e;

    /*
     * Initialize the code and parse structure.
     */

    Tcl_DStringInit(&codePtr->text);
    codePtr->nscripts = codePtr->nblocks = 0;
    parse.line = 0;
    parse.codePtr = codePtr;
    Tcl_DStringInit(&parse.lens);
    Tcl_DStringInit(&parse.lines);

    /*
     * Scan for <% ... %> sequences which take precedence over
     * other tags.
     */

    while ((s = strstr(utf, "<%")) && (e = strstr(s, "%>"))) {
	/*
	 * Parse text preceeding the script.
	 */

	*s = '\0';
	ParseBlock(&parse, servPtr, utf);
	*s = '<';
	if (!(flags & ADP_SAFE)) {
	    if (s[2] != '=') {
	        AppendBlock(&parse, s + 2, e, 's');
	    } else {
	        AppendBlock(&parse, s + 3, e, 'S');
	    }
	}
	utf = e + 2;
    }

    /*
     * Parse the remaining text.
     */

    ParseBlock(&parse, servPtr, utf);;

    /*
     * Complete the parse code structure.
     */

    AppendLengths(codePtr, (int *) parse.lens.string, (int *) parse.lines.string);

    /*
     * If configured, collapse blocks to a single script.
     */

    if (flags & ADP_SINGLE) {
    	Blocks2Script(codePtr);
    }

    Tcl_DStringFree(&parse.lens);
    Tcl_DStringFree(&parse.lines);
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpFreeCode --
 *
 *	Free internal AdpCode storage.
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
NsAdpFreeCode(AdpCode *codePtr)
{
    Tcl_DStringFree(&codePtr->text);
    codePtr->nblocks = codePtr->nscripts = 0;
    codePtr->len = codePtr->line = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendBlock --
 *
 *	Add a text or script block to the output buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AppendBlock(Parse *parsePtr, char *s, char *e, int type)
{
    AdpCode *codePtr = parsePtr->codePtr;
    int len;

    if (s < e) {
	++codePtr->nblocks;
	len = e - s;
	if (type == 'S') {
	    len += APPEND_LEN;
	    Tcl_DStringAppend(&codePtr->text, APPEND, APPEND_LEN);
	}
	Tcl_DStringAppend(&codePtr->text, s, e - s);
	if (type != 't') {
	    ++codePtr->nscripts;
	    len = -len;
	}
	Tcl_DStringAppend(&parsePtr->lens, (char *) &len, LENSZ);
	Tcl_DStringAppend(&parsePtr->lines, (char *) &parsePtr->line, LENSZ);
	while (s < e) {
	    if (*s++ == '\n') {
	    	++parsePtr->line;
	    }
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * GetTag --
 *
 *	Copy tag name in lowercase to given dstring.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Start of att=val pairs, if any, are set is aPtr if not null.
 *
 *----------------------------------------------------------------------
 */

static void
GetTag(Tcl_DString *dsPtr, char *s, char *e, char **aPtr)
{
    char *t;

    ++s;
    while (s < e && isspace(UCHAR(*s))) {
	++s;
    }
    t = s;
    while (s < e  && !isspace(UCHAR(*s))) {
	++s;
    }
    Tcl_DStringTrunc(dsPtr, 0);
    Tcl_DStringAppend(dsPtr, t, s - t);
    if (aPtr != NULL) {
	while (s < e && isspace(UCHAR(*s))) {
	    ++s;
	}
	*aPtr = s;
    }
    dsPtr->length = Tcl_UtfToLower(dsPtr->string);
}


/*
 *----------------------------------------------------------------------
 *
 * ParseAtts --
 *
 *	Parse tag attributes, either looking for known <script>
 *	pairs or copying cleaned up pairs to given dstring.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Flags in given servPtr are updated and/or data copied to given
 *	dstring.
 *
 *----------------------------------------------------------------------
 */

static void
ParseAtts(char *s, char *e, int *servPtr, Tcl_DString *attsPtr, int atts)
{
    char *vs = NULL, *ve = NULL, *as = NULL, *ae = NULL;
    char end = 0, vsave = 0, asave = 0;
    
    if (servPtr != NULL) {
	*servPtr = 0;
    }
    while (s < e) {
	/*
	 * Trim attribute name.
	 */

	while (s < e && isspace(UCHAR(*s))) {
	    ++s;
	}
	if (s == e) {
	    break;
	}
	as = s;

        if (*s != '\'' && *s != '"') {
            while (s < e && !isspace(UCHAR(*s)) && *s != '=') {
                ++s;
            }
        } else {
            ++s;
            while (s < e && *s != *as) {
                ++s;
            }
            ++s;
        }

	ae = s;
	while (s < e && isspace(UCHAR(*s))) {
	    ++s;
	}
	if (*s != '=') {
	    /*
	     * Use attribute name as value.
	     */

	    vs = as;
	} else {
	    /*
	     * Trim spaces and/or quotes from value.
	     */

	    do {
		++s;
	    } while (s < e && isspace(UCHAR(*s)));
	    vs = s;

            if (*s != '"' && *s != '\'') {
                while (s < e && !isspace(UCHAR(*s))) {
                    ++s;
                }
            } else {
                ++s;
                while (s < e && *s != *vs) {
                    ++s;
                }
                ++s;
            }
            
	    ve = s;
	    end = *vs;
	    if (end != '=' && end != '"' && end != '\'') {
		end = 0;
	    }
	    if (end && ve > vs && ve[-1] == end) {
		++vs;
		--ve;
	    }
	    vsave = *ve;
	    *ve = '\0';
	}
	asave = *ae;
	*ae = '\0';

	/*
	 * Append attributes or scan for special <script> pairs.
	 */

	if (attsPtr != NULL) {
	    if (atts) {
	    	Tcl_DStringAppendElement(attsPtr, as);
	    }
	    Tcl_DStringAppendElement(attsPtr, vs);
	}
	if (servPtr != NULL && vs != as) {
	    if (STRIEQ(as, "runat") && STRIEQ(vs, "server")) {
		*servPtr |= SERV_RUNAT;
	    } else if (STRIEQ(as, "language") && !STRIEQ(vs, "tcl")) {
		*servPtr |= SERV_NOTTCL;
	    } else if (STRIEQ(as, "stream") && STRIEQ(vs, "on")) {
		*servPtr |= SERV_STREAM;
	    }
	}

	/*
	 * Restore strings.
	 */

	*ae = asave;
	if (vs != as) {
	    *ve = vsave;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * IsServer --
 *
 *	Parse attributes for known <script> attributes.
 *
 * Results:
 *	1 if attributes indicate valid server-side script, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
IsServer(char *tag, char *as, char *ae, int *streamPtr)
{
    int serv;

    if (as < ae && STREQ(tag, "script")) {
	ParseAtts(as, ae, &serv, NULL, 1);
	if ((serv & SERV_RUNAT) && !(serv & SERV_NOTTCL)) {
	    *streamPtr = (serv & SERV_STREAM);
	    return 1;
	}
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendTag --
 *
 *	Append tag script block.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AppendTag(Parse *parsePtr, Tag *tagPtr, char *as, char *ae, char *se)
{
    Tcl_DString script;
    char save;

    Tcl_DStringInit(&script);
    Tcl_DStringAppend(&script, "ns_adp_append [", -1);
    if (tagPtr->type == TAG_ADP) {
	/* NB: String will be an ADP fragment to evaluate. */
	Tcl_DStringAppend(&script, "ns_adp_eval ", -1);
    }
    Tcl_DStringAppendElement(&script, tagPtr->string);
    if (tagPtr->type == TAG_PROC) {
	/* NB: String was a procedure, append tag attributes. */
    	ParseAtts(as, ae, NULL, &script, 0);
    }
    if (se > ae) {
	/* NB: Append enclosing text as argument to eval or proc. */
	save = *se;
	*se = '\0';
	Tcl_DStringAppendElement(&script, ae + 1);
	*se = save;
    }
    if (tagPtr->type == TAG_SCRIPT || tagPtr->type == TAG_ADP) {
	/* NB: Append code to create set with tag attributes. */
    	Tcl_DStringAppend(&script, " [ns_set create", -1);
    	Tcl_DStringAppendElement(&script, tagPtr->tag);
    	ParseAtts(as, ae, NULL, &script, 1);
    	Tcl_DStringAppend(&script, "]", 1);
    }
    /* NB: Close ns_adp_append subcommand. */
    Tcl_DStringAppend(&script, "]", 1);
    AppendBlock(parsePtr, script.string, script.string+script.length, 's');
    Tcl_DStringFree(&script);
}


/*
 *----------------------------------------------------------------------
 *
 * ParseBlock --
 *
 *	Parse UTF text for <script> and/or registered tags.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Blocks will be appended to the given codePtr.
 *
 *----------------------------------------------------------------------
 */

static void
ParseBlock(Parse *parsePtr, NsServer *servPtr, char *utf)
{
    Tag            *tagPtr = NULL;
    char           *ss = NULL, *se = NULL, *s = NULL, *e = NULL;
    char           *a = NULL, *as = NULL, *ae = NULL , *t = NULL;
    int             level = 0, state, stream, streamdone;
    Tcl_DString     tag;
    Tcl_HashEntry  *hPtr = NULL;

    Tcl_DStringInit(&tag);
    t = utf;
    streamdone = stream = 0;
    state = 0;
    Ns_RWLockRdLock(&servPtr->adp.taglock);
    while ((s = strchr(utf, '<')) && (e = strchr(s, '>'))) {
	/*
	 * Process the tag depending on the current state.
	 */

	switch (state) {
	case 0:
	    /*
	     * Look for possible <script> or <tag>.
	     */

	    GetTag(&tag, s, e, &a);
	    if (IsServer(tag.string, a, e, &stream)) {
		/*
		 * Record start of script.
		 */

		ss = s;
		se = e + 1;
		state = 1;
	    } else {
    		hPtr = Tcl_FindHashEntry(&servPtr->adp.tags, tag.string);
    		if (hPtr != NULL) {
		    tagPtr = Tcl_GetHashValue(hPtr);
		    if (tagPtr->endtag == NULL) {
			/*
			 * Output simple no-end registered tag.
			 */

			AppendBlock(parsePtr, t, s, 't');
			t = e + 1;
			AppendTag(parsePtr, tagPtr, a, e, NULL);
		    } else {
			/*
			 * Record start of registered tag.
			 */

			ss = s;
			as = a;
			ae = e;
			level = 1;
			state = 2;
		    }
		}
	    }
	    break;

	case 1:
	    GetTag(&tag, s, e, NULL);
	    if (STREQ(tag.string, "/script")) {
		/*
		 * Output end of script.
		 */

		AppendBlock(parsePtr, t, ss, 't');
		t = e + 1;
		if (stream && !streamdone) {
		    AppendBlock(parsePtr, "ns_adp_stream", NULL, 's');
		    streamdone = 1;
		}
		AppendBlock(parsePtr, se, s, 's');
		state = 0;
	    }
	    break;

	case 2:
	    GetTag(&tag, s, e, NULL);
	    if (STRIEQ(tag.string, tagPtr->tag)) {
		/*
		 * Increment register tag nesting level.
		 */

		++level;
	    } else if (STRIEQ(tag.string, tagPtr->endtag)) {
		--level;
		if (level == 0) {
		    /*
		     * Dump out registered tag.
		     */

		    AppendBlock(parsePtr, t, ss, 't');
		    t = e + 1;
		    AppendTag(parsePtr, tagPtr, as, ae, s);
		    state = 0;
		}
	    }
	    break;
	}
	utf = s + 1;
    }
    Ns_RWLockUnlock(&servPtr->adp.taglock);

    /*
     * Append the remaining text block.
     */

    AppendBlock(parsePtr, t, t + strlen(t), 't');
    Tcl_DStringFree(&tag);
}


/*
 *----------------------------------------------------------------------
 *
 * Blocks2Script --
 *
 *	Collapse text/script blocks in a parse structure into a single
 *	script.  This enables a complete scripts to be made up of
 *	multiple blocks, e.g., <% if $true { %> Text <% } %>.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Parse structure is updated to a single script block.
 *
 *----------------------------------------------------------------------
 */

static void
Blocks2Script(AdpCode *codePtr)
{
    char *utf, save;
    int i, len, line;
    Tcl_DString tmp;

    Tcl_DStringInit(&tmp);
    utf = codePtr->text.string;
    for (i = 0; i < codePtr->nblocks; ++i) {
	len = codePtr->len[i];
	if (len < 0) {
	    len = -len;	
	    Tcl_DStringAppend(&tmp, utf, len);
	} else {
	    Tcl_DStringAppend(&tmp, "ns_adp_append", -1);
	    save = utf[len];
	    utf[len] = '\0';
	    Tcl_DStringAppendElement(&tmp, utf);
	    utf[len] = save;
	}
	Tcl_DStringAppend(&tmp, "\n", 1);
	utf += len;
    }
    Tcl_DStringTrunc(&codePtr->text, 0);
    Tcl_DStringAppend(&codePtr->text, tmp.string, tmp.length);
    codePtr->nscripts = codePtr->nblocks = 1;
    line = 0;
    len = -tmp.length;
    AppendLengths(codePtr, &len, &line);
    Tcl_DStringFree(&tmp);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendLengths --
 *
 *	Append the block length and line numbers to the given
 *	parse code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AppendLengths(AdpCode *codePtr, int *len, int *line)
{
    Tcl_DString *textPtr = &codePtr->text;
    int start, ncopy;

    /* NB: Need to round up start of lengths array to next word. */
    start = ((textPtr->length / LENSZ) + 1) * LENSZ;
    ncopy = codePtr->nblocks * LENSZ;
    Tcl_DStringSetLength(textPtr, start + (ncopy * 2));
    codePtr->len = (int *) (textPtr->string + start);
    codePtr->line = (int *) (textPtr->string + start + ncopy);
    memcpy(codePtr->len,  len, (size_t) ncopy);
    memcpy(codePtr->line, line, (size_t) ncopy);
}
