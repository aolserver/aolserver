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
 * str.c --
 *
 *	Functions that deal with strings. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/str.c,v 1.6 2004/06/08 19:28:57 rcrittenden0569 Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"


/*
 *----------------------------------------------------------------------
 *
 * Ns_StrTrim --
 *
 *	Trim leading and trailing white space from a string. 
 *
 * Results:
 *	A pointer to the trimmed string, which will be in the original 
 *	string. 
 *
 * Side effects:
 *	May modify passed-in string. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_StrTrim(char *string)
{
    return Ns_StrTrimLeft(Ns_StrTrimRight(string));
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_StrTrimLeft --
 *
 *	Trim leading white space from a string. 
 *
 * Results:
 *	A pointer to the trimmed string, which will be in the 
 *	original string. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_StrTrimLeft(char *string)
{
    if (string == NULL) {
	return NULL;
    }
    while (isspace(UCHAR(*string))) {
        ++string;
    }
    return string;
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_StrTrimRight --
 *
 *	Trim trailing white space from a string. 
 *
 * Results:
 *	A pointer to the trimmed string, which will be in the 
 *	original string. 
 *
 * Side effects:
 *	The string will be modified. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_StrTrimRight(char *string)
{
    int len;

    if (string == NULL) {
	return NULL;
    }
    len = strlen(string);
    while ((--len >= 0) &&
	   (isspace(UCHAR(string[len])) ||
	    string[len] == '\n')) {
	
        string[len] = '\0';
    }
    return string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_StrToLower --
 *
 *	All alph. chars in a string will be made to be lowercase. 
 *
 * Results:
 *	Same string as passed in. 
 *
 * Side effects:
 *	Will modify string. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_StrToLower(char *string)
{
    char *s;

    s = string;
    while (*s != '\0') {
        if (isupper(UCHAR(*s))) {
            *s = tolower(UCHAR(*s));
        }
        ++s;
    }
    return string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_StrToUpper --
 *
 *	All alph. chars in a string will be made to be uppercase. 
 *
 * Results:
 *	Same string as pssed in. 
 *
 * Side effects:
 *	Will modify string. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_StrToUpper(char *string)
{
    char *s;

    s = string;
    while (*s != '\0') {
        if (islower(UCHAR(*s))) {
            *s = toupper(UCHAR(*s));
        }
        ++s;
    }
    return string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_Match --
 *
 *	Compare the beginnings of two strings, case insensitively. 
 *	The comparison stops when the end of the shorter string is 
 *	reached. 
 *
 * Results:
 *	NULL if no match, b if match. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_Match(char *a, char *b)
{
    if (a != NULL && b != NULL) {
        while (*a != '\0' && *b != '\0') {
            char            c1, c2;

            c1 = islower(UCHAR(*a)) ? *a : tolower(UCHAR(*a));
            c2 = islower(UCHAR(*b)) ? *b : tolower(UCHAR(*b));
            if (c1 != c2) {
                return NULL;
            }
            a++;
            b++;
        }
    }
    return (char *) b;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_NextWord --
 *
 *	Return a pointer to first character of the next word in a 
 *	string; words are separated by white space. 
 *
 * Results:
 *	A string pointer in the original string. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_NextWord(char *line)
{
    while (*line != '\0' && !isspace(UCHAR(*line))) {
        ++line;
    }
    while (*line != '\0' && isspace(UCHAR(*line))) {
        ++line;
    }
    return line;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_StrCaseStr --
 *
 *	Search for first substring within string, case insensitive. 
 *
 * Results:
 *	A pointer to where substring starts or NULL.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_StrNStr(char *string, char *substring)
{
    return Ns_StrCaseFind(string, substring);
}

char *
Ns_StrCaseFind(char *string, char *substring)
{
    if (strlen(string) > strlen(substring)) {
    	while (*string != '\0') {
	    if (Ns_Match(string, substring)) {
	        return string;
	    }
	    ++string;
	}
    }
    return NULL;
}
