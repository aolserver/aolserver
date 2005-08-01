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
 * tclimg.c --
 *
 *	Commands for image files.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclimg.c,v 1.12 2005/08/01 20:29:24 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static int ChanGetc(Tcl_Channel chan);
static int JpegNextMarker(Tcl_Channel chan);
static int JpegSize(Tcl_Channel chan, int *wPtr, int *hPtr);
static int JpegRead2Bytes(Tcl_Channel chan);
static int AppendObjDims(Tcl_Interp *interp, int w, int h);


/*
 *----------------------------------------------------------------------
 *
 * NsTclGifSizeObjCmd --
 *
 *	Implements ns_gifsize, returning a list of width and height.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclGifSizeObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int fd;
    unsigned char  buf[0x300];
    int depth, colormap, dx, dy, status;
    char *file;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "gif");
        return TCL_ERROR;
    }
    file = Tcl_GetString(objv[1]);
    fd = open(file, O_RDONLY|O_BINARY);
    if (fd == -1) {
        Tcl_AppendResult(interp, "could not open \"", file, "\": ",
			 Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    status = TCL_ERROR;

    /*
     * Read the GIF version number
     */
    
    if (read(fd, buf, 6) != 6) {
readfail:
        Tcl_AppendResult(interp, "could not read \"", file, "\": ",
			 Tcl_PosixError(interp), NULL);
	goto done;
    }

    if (strncmp((char *) buf, "GIF87a", 6) && 
	strncmp((char *) buf, "GIF89a", 6)) {
badfile:
        Tcl_AppendResult(interp, "invalid gif file: ", file, NULL);
        goto done;
    }

    if (read(fd, buf, 7) != 7) {
	goto readfail;
    }

    depth = 1 << ((buf[4] & 0x7) + 1);
    colormap = (buf[4] & 0x80 ? 1 : 0);

    if (colormap) {
        if (read(fd, buf, (size_t)(3*depth)) != (3*depth)) {
            goto readfail;
        }
    }

  outerloop:
    if (read(fd, buf, 1) != 1) {
        goto readfail;
    }

    if (buf[0] == '!') {
        unsigned char count;
	
        if (read(fd, buf, 1) != 1) {
            goto readfail;
        }
      innerloop:
        if (read(fd, (char *) &count, 1) != 1) {
            goto readfail;
        }
        if (count == 0) {
            goto outerloop;
        }
        if (read(fd, buf, count) != count) {
            goto readfail;
        }
        goto innerloop;
    } else if (buf[0] != ',') {
        goto badfile;
    }

    if (read(fd,buf,9) != 9) {
        goto readfail;
    }

    dx = 0x100 * buf[5] + buf[4];
    dy = 0x100 * buf[7] + buf[6];
    if(AppendObjDims(interp, dx, dy) != TCL_OK) {
		return TCL_ERROR;
	};
    status = TCL_OK;

done:
    close(fd);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclJpegSizeObjCmd --
 *
 *	Implements ns_jpegsize as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclJpegSizeObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int   code, w = 0, h = 0;
    Tcl_Channel chan;
    char *file;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "file");
	return TCL_ERROR;
    }
    file = Tcl_GetString(objv[1]);
    chan = Tcl_OpenFileChannel(interp, file, "r", 0);
    if (chan == NULL) {
        /* Tcl function will leave error message in interp's result */
        return TCL_ERROR;
    }
    if (Tcl_SetChannelOption(interp, chan, "-translation", "binary") != TCL_OK) {
        /* Tcl function will leave error message in interp's result */
        return TCL_ERROR;
    }
    code = JpegSize(chan, &w, &h);
    Tcl_Close(interp, chan);
    if (code != TCL_OK) {
	Tcl_AppendResult(interp, "invalid jpeg file: ", file, NULL);
	return TCL_ERROR;
    }
    if(AppendObjDims(interp, w, h) != TCL_OK) {
	return TCL_ERROR;
    };
    return TCL_OK;
}

#define M_SOI   0xD8		/* Start Of Image (beginning of datastream) */
#define M_EOI   0xD9		/* End Of Image (end of datastream) */
#define M_SOS   0xDA		/* Start Of Scan (begins compressed data) */

static int
JpegSize(Tcl_Channel chan, int *wPtr, int *hPtr)
{
    unsigned int i, w, h;
    Tcl_WideInt  numbytes;

    if (ChanGetc(chan) == 0xFF && ChanGetc(chan) == M_SOI) {
	while (1) {
	    i = JpegNextMarker(chan);
	    if (i == EOF || i == M_SOS || i == M_EOI) {
	    	break;
	    }
            if (0xC0 <= i && i <= 0xC3) {
		if (JpegRead2Bytes(chan) != EOF && ChanGetc(chan) != EOF
		    && (h = JpegRead2Bytes(chan)) != EOF
		    && (w = JpegRead2Bytes(chan)) != EOF) {
		    *wPtr = w;
		    *hPtr = h;
		    return TCL_OK;
		}
		break;
	    }
	    numbytes = JpegRead2Bytes(chan);
	    if (numbytes < 2 || Tcl_Seek(chan, numbytes - 2, SEEK_CUR) == -1) {
	    	break;
	    }
	}
    }
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * JpegRead2Bytes --
 *
 *	Read 2 bytes, convert to unsigned int. All 2-byte quantities 
 *	in JPEG markers are MSB first. 
 *
 * Results:
 *	The two byte value, or -1 on error. 
 *
 * Side effects:
 *	Advances file pointer. 
 *
 *----------------------------------------------------------------------
 */

static int
JpegRead2Bytes(Tcl_Channel chan)
{
    int c1, c2;
    
    c1 = ChanGetc(chan);
    c2 = ChanGetc(chan);
    if (c1 == EOF || c2 == EOF) {
	return -1;
    }
    return (int)(((unsigned int) c1) << 8) + ((unsigned int) c2);
}


/*
 *----------------------------------------------------------------------
 *
 * JpegNextMarker --
 *
 *	Find the next JPEG marker and return its marker code. We 
 *	expect at least one FF byte, possibly more if the compressor 
 *	used FFs to pad the file. There could also be non-FF garbage 
 *	between markers. The treatment of such garbage is 
 *	unspecified; we choose to skip over it but emit a warning 
 *	msg. This routine must not be used after seeing SOS marker, 
 *	since it will not deal correctly with FF/00 sequences in the 
 *	compressed image data... 
 *
 * Results:
 *	The next marker code.
 *
 * Side effects:
 *	Will eat up any duplicate FF bytes.
 *
 *----------------------------------------------------------------------
 */

static int
JpegNextMarker(Tcl_Channel chan)
{
    int c;

    /*
     * Find 0xFF byte; count and skip any non-FFs.
     */
    
    c = ChanGetc(chan);
    while (c != EOF && c != 0xFF) {
	c = ChanGetc(chan);
    }
    if (c != EOF) {
	/*
	 * Get marker code byte, swallowing any duplicate FF bytes.
	 */
	
	do {
	    c = ChanGetc(chan);
	} while (c == 0xFF);
    }

    return c;
}


/*
 *----------------------------------------------------------------------
 *
 * ChanGetc --
 *
 *	Read a single unsigned char from a channel.
 *
 * Results:
 *	Character or EOF. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ChanGetc(Tcl_Channel chan)
{
    unsigned char buf[1];

    if (Tcl_Read(chan, (char *) buf, 1) != 1) {
	return EOF;
    }
    return (int) buf[0];
}


/*
 *----------------------------------------------------------------------
 *
 * AppendObjDims --
 *
 *	Format and append width and height dimensions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	List elements appended to interp result.
 *
 *----------------------------------------------------------------------
 */

static int
AppendObjDims(Tcl_Interp *interp, int w, int h)
{
    char buf[20];
    Tcl_Obj *result = Tcl_NewObj();

    sprintf(buf, "%d", w);
    if (Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj(buf, -1))) {
        return TCL_ERROR;
    }
    sprintf(buf, "%d", h);
    if (Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj(buf, -1))) {
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, result);

    return TCL_OK;
}
