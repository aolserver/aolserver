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
 * tclinvoke.cpp --
 *
 *	Directly invoke a Tcl command or procedure.
 */


#include <ns.h>

#if defined(__STDC__) || defined(HAS_STDARG)
#include <stdarg.h>
#else
#include <varargs.h>
#endif

/*----------------------------------------------------------------------
 *
 * Tcl_Invoke -- Directly invoke a Tcl command or procedure
 *
 *	Call Tcl_Invoke somewhat like Tcl_VarEval:
 *
 *	result = Tcl_Invoke(interp, cmdName, arg1, arg2, ..., NULL);
 *
 *	Each arg becomes one argument to the command, with no further
 *	substitutions or parsing.
 *
 * Result:
 *	The code returned by the invoked command, either TCL_OK or
 *	TCL_ERROR.  The result of the invoked command is returned in
 *	interp->result.  If command does not exist (or is handled by
 *	the Tcl unknown command if installed), TCL_ERROR is returned
 *	with string "unknown command ..." stored in interp->result.
 *	Use one of the procedures Tcl_GetObjResult() or Tcl_GetStringResult()
 *	to read the result.
 *
 * Side Effects:
 *	Like Tcl_VarArg.  The value of the arguments may be overwritten
 *	by the invoked command.
 *	The return value of a Tcl procedure is incorrect of there is
 *	no return statement inside the Tcl procedure.
 *
 *----------------------------------------------------------------------
 */
	 /* VARARGS2 *//* ARGSUSED */

int
Tcl_Invoke TCL_VARARGS_DEF (Tcl_Interp *, arg1) {
    va_list argList;
    Tcl_Interp *interp;
    char *cmd;			/* Command name */
    char *arg;			/* Command argument */
    char **argv;		/* String vector for arguments */
    int argc, i, max;		/* Number of arguments */
    Tcl_CmdInfo info;		/* Info about command procedures */
    int result;			/* TCL_OK or TCL_ERROR */
#define NUM_ARGS 20
    char *(argvStack[NUM_ARGS]);	/* Initial argument vector */

    interp = TCL_VARARGS_START (Tcl_Interp *, arg1, argList);
    Tcl_ResetResult (interp);

    /*
     * Map from the command name to a C procedure
     */
    cmd = va_arg (argList, char *);
    if (!Tcl_GetCommandInfo (interp, cmd, &info)) {
	  Tcl_AppendResult (interp, "unknown command \"", cmd, "\"", NULL);
	  va_end (argList);
	  return TCL_ERROR;
      }

    max = NUM_ARGS;

#if TCL_MAJOR_VERSION > 7
    /*
     * Check whether the object interface is preferred for this command
     */
    if (info.isNativeObjectProc) {
	  Tcl_Obj **objv;	/* Object vector for arguments */
	  int objc;

	  objv = (Tcl_Obj **) ckalloc (max * sizeof (Tcl_Obj *));
	  objv[0] = Tcl_NewStringObj (cmd, strlen (cmd));
	  Tcl_IncrRefCount (objv[0]);	/* ref count is one now */
	  objc = 1;

	  /*
	   * Build a vector out of the rest of the arguments
	   */
	  while (1) {
		arg = va_arg (argList, char *);
		if (arg == (char *) NULL)
		  {
		      objv[objc] = (Tcl_Obj *) NULL;
		      break;
		  }
		objv[objc] = Tcl_NewStringObj (arg, strlen (arg));
		Tcl_IncrRefCount (objv[objc]);
		objc++;
		if (objc >= max)
		  {
		      /* allocate a bigger vector and copy old one */
		      Tcl_Obj **oldv = objv;
		      max *= 2;
		      objv = (Tcl_Obj **) ckalloc (max * sizeof (Tcl_Obj *));
		      for (i = 0; i < objc; i++)
			{
			    objv[i] = oldv[i];
			}
		      ckfree ((char *) oldv);
		  }
	    }
	  va_end (argList);

	  /*
	   * Invoke the C procedure
	   */
	  result = (*info.objProc) (info.objClientData, interp, objc, objv);

	  /*
	   * Make sure the string value of the result is valid
	   * and release our references to the arguments
	   */
	  (void) Tcl_GetStringResult (interp);
	  for (i = 0; i < objc; i++)
	    {
		Tcl_DecrRefCount (objv[i]);
	    }
	  ckfree ((char *) objv);

	  return result;
      }
#endif
    argv = argvStack;
    argv[0] = cmd;
    argc = 1;

    /*
     * Build a vector out of the rest of the arguments
     */
    while (1)
      {
	  arg = va_arg (argList, char *);
	  argv[argc] = arg;
	  if (arg == (char *) NULL)
	    {
		break;
	    }
	  argc++;
	  if (argc >= max)
	    {
		/* allocate a bigger vector and copy old one */
		char **oldv = argv;
		max *= 2;
		argv = (char **) ckalloc (max * sizeof (char *));
		for (i = 0; i < argc; i++)
		  {
		      argv[i] = oldv[i];
		  }
		if (oldv != argvStack)
		  {
		      ckfree ((char *) oldv);
		  }
	    }
      }
    va_end (argList);

    /*
     * Invoke the C procedure
     */
    result = (*info.proc) (info.clientData, interp, argc, argv);

    /*
     * Release the arguments iff allocated dynamically
     */
    if (argv != argvStack)
      {
	  ckfree ((char *) argv);
      }
    return result;

#undef NUM_ARGS
}

