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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adpparse.c,v 1.16 2005/01/15 23:53:26 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define SERV_STREAM	1
#define SERV_RUNAT	2
#define SERV_NOTTCL	4

#define TAG_ADP		1
#define TAG_PROC	2
#define TAG_OPROC	3

#define APPEND		"ns_adp_append "
#define APPEND_LEN	(sizeof(APPEND)-1)

#define LENSZ		(sizeof(int))

/*
 * The following structure maintains proc and adp registered tags.
 * String bytes directly follow the Tag struct in the same allocated
 * block.
 */

typedef struct {
    int		   type;   /* Type of tag, ADP or proc. */
    char          *tag;    /* The name of the tag (e.g., "netscape") */
    char          *endtag; /* The closing tag or null (e.g., "/netscape")*/
    char          *string; /* Proc (e.g., "ns_adp_netscape") or ADP string. */
} Tag;

/*
 * Local functions defined in this file
 */

static void AppendBlock(AdpCode *codePtr, char *s, char *e, int type);
static void Parse(AdpCode *codePtr, NsServer *servPtr, char *utf);
static int RegisterCmd(ClientData arg, Tcl_Interp *interp, int argc,
		char **argv, int type);
static void Blocks2Script(AdpCode *codePtr);
static void AppendLengths(AdpCode *codePtr, int *len);


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
 * NsTclRegisterTagCmd, NsTclRegisterAdpCmd --
 *
 *	Register an ADP proc or string tag.
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
NsTclRegisterTagCmd(ClientData arg, Tcl_Interp *interp, int argc,
		    char **argv)
{
    return RegisterCmd(arg, interp, argc, argv, TAG_OPROC);
}

int
NsTclAdpRegisterAdpCmd(ClientData arg, Tcl_Interp *interp, int argc,
		    char **argv)
{
    return RegisterCmd(arg, interp, argc, argv, TAG_ADP);
}

int
NsTclAdpRegisterProcCmd(ClientData arg, Tcl_Interp *interp, int argc,
		    char **argv)
{
    return RegisterCmd(arg, interp, argc, argv, TAG_PROC);
}

static int
RegisterCmd(ClientData arg, Tcl_Interp *interp, int argc,
		    char **argv, int type)
{
    NsInterp       *itPtr = arg;
    NsServer	   *servPtr = itPtr->servPtr;
    char           *string;
    Tcl_HashEntry  *hPtr;
    int             new, slen, elen;
    Tag            *tagPtr;
    
    if (argc != 4 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " tag ?endtag? ",
			 type == TAG_ADP ? "adp" : "proc", "\"", NULL);
	return TCL_ERROR;
    }
    string = argv[argc-1];
    slen = strlen(string) + 1;
    if (argc == 3) {
	elen = 0;
    } else {
	elen = strlen(argv[2]) + 1;
    }
    tagPtr = ns_malloc(sizeof(Tag) + slen + elen);
    tagPtr->type = type;
    tagPtr->string = (char *) tagPtr + sizeof(Tag);
    memcpy(tagPtr->string, string, (size_t) slen);
    if (argc == 3) {
	tagPtr->endtag = NULL;
    } else {
	tagPtr->endtag = tagPtr->string + slen;
	memcpy(tagPtr->endtag, argv[2], (size_t) elen);
    }
    Ns_RWLockWrLock(&servPtr->adp.taglock);
    hPtr = Tcl_CreateHashEntry(&servPtr->adp.tags, argv[1], &new);
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
    Tcl_DString hdr;
    char *s, *e;

    /*
     * Initialize the parse structure.
     */

    flags = servPtr->adp.flags | flags;
    Tcl_DStringInit(&codePtr->text);
    codePtr->nscripts = codePtr->nblocks = 0;
    Tcl_DStringInit(&hdr);
    codePtr->len.dPtr = &hdr;

    /*
     * Scan for <% ... %> sequences which take precedence over
     * other tags.
     */

    while ((s = strstr(utf, "<%")) && (e = strstr(s, "%>"))) {
	/*
	 * Parse text preceeding the script.
	 */

	*s = '\0';
	Parse(codePtr, servPtr, utf);
	*s = '<';
	if (!(flags & ADP_SAFE)) {
	    if (s[2] != '=') {
	        AppendBlock(codePtr, s + 2, e, 's');
	    } else {
	        AppendBlock(codePtr, s + 3, e, 'S');
	    }
	}
	utf = e + 2;
    }

    /*
     * Parse the remaining text.
     */

    Parse(codePtr, servPtr, utf);;

    /*
     * Complete the parse code structure.
     */

    AppendLengths(codePtr, (int *) hdr.string);
    Tcl_DStringFree(&hdr);

    /*
     * If configured, collapse blocks to a single script.
     */

    if (flags & ADP_SINGLE) {
    	Blocks2Script(codePtr);
    }
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
    codePtr->len.iPtr = NULL;
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
AppendBlock(AdpCode *codePtr, char *s, char *e, int type)
{
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
	Tcl_DStringAppend(codePtr->len.dPtr, (char *) &len, LENSZ);
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
AppendTag(AdpCode *codePtr, Tag *tagPtr, char *as, char *ae, char *se)
{
    Tcl_DString script;
    char save;

    Tcl_DStringInit(&script);
    Tcl_DStringAppend(&script, "ns_adp_append [", -1);
    if (tagPtr->type == TAG_ADP) {
	Tcl_DStringAppend(&script, "ns_adp_eval ", -1);
    }
    Tcl_DStringAppendElement(&script, tagPtr->string);
    if (tagPtr->type == TAG_PROC) {
    	ParseAtts(as, ae, NULL, &script, 0);
    }
    if (se > ae) {
	save = *se;
	*se = '\0';
	Tcl_DStringAppendElement(&script, ae + 1);
	*se = save;
    }
    if (tagPtr->type != TAG_PROC) {
    	Tcl_DStringAppend(&script, " [ns_set create", -1);
    	Tcl_DStringAppendElement(&script, tagPtr->tag);
    	ParseAtts(as, ae, NULL, &script, 1);
    	Tcl_DStringAppend(&script, "]", 1);
    }
    Tcl_DStringAppend(&script, "]", 1);
    AppendBlock(codePtr, script.string, script.string+script.length, 's');
    Tcl_DStringFree(&script);
}


/*
 *----------------------------------------------------------------------
 *
 * Parse --
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
Parse(AdpCode *codePtr, NsServer *servPtr, char *utf)
{
    Tag            *tagPtr = NULL;
    char           *ss = NULL, *se = NULL, *s = NULL, *e = NULL;
    char           *a = NULL, *as = NULL, *ae = NULL , *t = NULL;
    int             level = 0, state, stream, streamdone;
    Tcl_DString     tag;
    Tcl_HashEntry  *hPtr = NULL;

    Tcl_DStringInit(&tag);
    t = utf;
    streamdone = 0;
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

			AppendBlock(codePtr, t, s, 't');
			t = e + 1;
			AppendTag(codePtr, tagPtr, a, e, NULL);
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

		AppendBlock(codePtr, t, ss, 't');
		t = e + 1;
		if (stream && !streamdone) {
		    AppendBlock(codePtr, "ns_adp_stream", NULL, 's');
		    streamdone = 1;
		}
		AppendBlock(codePtr, se, s, 's');
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

		    AppendBlock(codePtr, t, ss, 't');
		    t = e + 1;
		    AppendTag(codePtr, tagPtr, as, ae, s);
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

    AppendBlock(codePtr, t, t + strlen(t), 't');
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
    int i, len;
    Tcl_DString tmp;

    Tcl_DStringInit(&tmp);
    utf = codePtr->text.string;
    for (i = 0; i < codePtr->nblocks; ++i) {
	len = codePtr->len.iPtr[i];
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
    codePtr->nscripts = codePtr->nblocks = 1;
    Tcl_DStringTrunc(&codePtr->text, 0);
    Tcl_DStringAppend(&codePtr->text, tmp.string, tmp.length);
    AppendLengths(codePtr, &tmp.length);
    Tcl_DStringFree(&tmp);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendLengths --
 *
 *	Append the array of length pointers to the end of the text
 * 	dstring.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates len.iPtr to point to start of lengths.
 *
 *----------------------------------------------------------------------
 */

static void
AppendLengths(AdpCode *codePtr, int *len)
{
    int start, ncopy;

    /* NB: Need to round up start of lengths array to next word. */
    start = ((codePtr->text.length / LENSZ) + 1) * LENSZ;
    ncopy = codePtr->nblocks * LENSZ;
    Tcl_DStringSetLength(&codePtr->text, start + ncopy);
    codePtr->len.iPtr = (int *) (codePtr->text.string + start);
    memcpy(codePtr->len.iPtr,  len, (size_t) ncopy);
}
