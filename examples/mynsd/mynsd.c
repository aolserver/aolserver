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
 * mynsd.c --
 *
 *	Example AOLserver main() startup routine using static-build
 *	initialization approach.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/examples/mynsd/mynsd.c,v 1.1 2005/08/08 12:14:30 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"

static Ns_ServerInitProc ServerInit;
extern Ns_ModuleInitProc NsSock_ModInit;
extern Ns_ModuleInitProc NsLog_ModInit;
extern Ns_ModuleInitProc NsCgi_ModInit;
extern Ns_ModuleInitProc NsPerm_ModInit;

/*
 * The following structure is a static-build equivalent of the
 * ns/server/{server}/modules config section for a static
 * build which directly links the module code.  Be sure to comment
 * out the the config section as the server will still attempt
 * to load and initialize listed modules resulting in confusion
 * of the static and dynamic code.
 */

static struct {
    char *module;
    Ns_ModuleInitProc *proc;
} mods[] = {
	{"nssock", NsSock_ModInit},
	{"nslog", NsLog_ModInit},
	{"nscgi", NsCgi_ModInit},
	{"nsperm", NsPerm_ModInit},
	{NULL, NULL}
};


/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *	AOLserver startup routine which simply calls Ns_Main().  
 *	Ns_Main() will later call ServerInit() if not NULL.
 *
 * Results:
 *	Result of Ns_Main.
 *
 * Side effects:
 *	Server runs.
 *
 *----------------------------------------------------------------------
 */

int
main(int argc, char **argv)
{
    return Ns_Main(argc, argv, ServerInit);
}


/*
 *----------------------------------------------------------------------
 *
 * ServerInit --
 *
 *	Initialize the staticly linked modules.
 *
 * Results:
 *	NS_OK.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ServerInit(char *server)
{
    int i;

    for (i = 0; mods[i].proc != NULL; ++i) {
	if (((*mods[i].proc)(server, mods[i].module)) != NS_OK) {
	    return NS_ERROR;
	}
    }
    return NS_OK;
}
