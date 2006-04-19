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
 * Copyright (C) 2001-2003 Vlad Seryakov
 * All rights reserved.
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
 * nszlib.c -- Zlib API module
 *
 *  ns_zlib usage:
 *
 *    ns_zlib compress data
 *      Returns compressed string
 *
 *    ns_zlib uncompress data
 *       Uncompresses previously compressed string
 *
 *    ns_zlib gzip data
 *      Returns compressed string in gzip format, string can be saved in
 *      a file with extension .gz and gzip will be able to uncompress it
 *
 *     ns_zlib gzipfile file
 *      Compresses the specified file, creating a file with the
 *      same name but a .gz suffix appened
 *
 *    ns_zlib gunzip file
 *       Uncompresses gzip file and returns text
 *
 *
 * Authors
 *
 *     Vlad Seryakov vlad@crystalballinc.com
 */

#include "nszlib.h"

#define VERSION "4.5"

static char header[] = {
    037, 0213,  /* GZIP magic number. */
    010,        /* Z_DEFLATED */
    0,          /* flags */
    0,0,0,0,    /* timestamp */
    0,          /* xflags */
    03};        /* Unix OS_CODE */

static Ns_TclTraceProc ZlibTrace;
static Tcl_ObjCmdProc ZlibObjCmd;
static Ns_GzipProc ZlibGzip;


/*
 *----------------------------------------------------------------------
 *
 * Nszlib_Init--
 *
 *      Tcl load-command entry point.
 *
 * Results:
 *      TCL_OK.
 *
 * Side effects:
 *	Adds the ns_zlib command.
 *
 *----------------------------------------------------------------------
 */

int
Nszlib_Init(Tcl_Interp *interp)
{
    Tcl_CreateObjCommand(interp, "ns_zlib",ZlibObjCmd ,NULL, NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsZlibModInit--
 *
 *      AOLserver module entry point.
 *
 * Results:
 *      NS_OK.
 *
 * Side effects:
 *	Installs ZlibGzip as the Ns_Gzip proc and registers
 *	a trace to create ns_zlib command in all new interps.
 *
 *----------------------------------------------------------------------
 */

int
NsZlibModInit(char *server, char *module)
{
    Ns_Log(Notice,"nszlib: zlib module version %s started",VERSION);
    Ns_SetGzipProc(ZlibGzip);
    Ns_TclRegisterTrace(server, ZlibTrace, NULL, NS_TCL_TRACE_CREATE);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ZlibCompress --
 *
 *      Compress a string.
 *
 * Results:
 *	Pointer to ns_malloc's string of compressed data or NULL
 *	on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

unsigned char *
Ns_ZlibCompress(unsigned char *inbuf,unsigned long inlen,unsigned long *outlen)
{
    int rc;
    unsigned long crc;
    unsigned char *outbuf;

    *outlen = inlen*1.1+20;
    outbuf = ns_malloc(*outlen);
    *outlen = (*outlen)-8;
    rc = compress2(outbuf,outlen,inbuf,inlen,3);
    if(rc != Z_OK) {
	Ns_Log(Error,"Ns_ZlibCompress: error %d",rc);
	ns_free(outbuf);
	return 0;
    }
    crc = crc32(crc32(0,Z_NULL,0),inbuf,inlen);
    crc = htonl(crc);
    inlen = htonl(inlen);
    memcpy(outbuf+(*outlen),&crc,4);
    memcpy(outbuf+(*outlen)+4,&inlen,4);
    (*outlen) += 8;
    return outbuf;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ZlibUncompress --
 *
 *      Uncompress a string.
 *
 * Results:
 *      Pointer to ns_malloc'ed string of uncompressed data or NULL
 *	on error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned char *
Ns_ZlibUncompress(unsigned char *inbuf,unsigned long inlen,
		  unsigned long *outlen)
{
    int rc;
    unsigned long crc;
    unsigned char *outbuf;

    memcpy(outlen,&inbuf[inlen-4],4);
    *outlen = ntohl(*outlen);
    outbuf = ns_malloc((*outlen)+1);
    rc = uncompress(outbuf,outlen,inbuf,inlen-8);
    if(rc != Z_OK) {
	Ns_Log(Error,"Ns_ZlibUncompress: error %d",rc);
	ns_free(outbuf);
	return 0;
    }
    memcpy(&crc,&inbuf[inlen-8],4);
    crc = ntohl(crc);
    if(crc != crc32(crc32(0,Z_NULL,0),outbuf,*outlen)) {
	Ns_Log(Error,"Ns_ZlibUncompress: crc mismatch");
	ns_free(outbuf);
	return 0;
    }
    return outbuf;
}


/*
 *----------------------------------------------------------------------
 *
 * ZlibGzip --
 *
 *      Compress procedure for Ns_Gzip.
 *
 * Results:
 *      NS_OK if compression worked, NS_ERROR otherwise.
 *
 * Side effects:
 *      Will write compressed content to given Tcl_DString.
 *
 *----------------------------------------------------------------------
 */

static int
ZlibGzip(char *buf, int len, int level, Tcl_DString *dsPtr)
{
    Bytef *ubuf = (Bytef *) buf;
    uLong ulen = (uLong) len;
#define FOOTER_SIZE	8
    unsigned char *fp;
    uLongf glen;
    Bytef *gbuf;
    uLong crc;
    int skip;

    /*
     * Size the dstring to hold the header, footer, and max compressed output.
     */

    glen = (uLongf) compressBound(len) + sizeof(header) + FOOTER_SIZE;
    Tcl_DStringSetLength(dsPtr, (int) glen);

    /*
     * Compress output to the dstring starting 2-bytes from the end of
     * the where the header will be written and then write the header
     * at the start of the dstring.
     */

    gbuf = (Bytef *) dsPtr->string;
    skip = sizeof(header) - 2;
    glen -= skip;
    if (compress2(gbuf + skip, &glen, ubuf, ulen, level) != Z_OK) {
		  return NS_ERROR;
    }
    memcpy(gbuf, header, sizeof(header));

    /*
     * Adjust the size of the dstring to the actual size of compressed data,
     * header, and footer, less the 2-bytes overwritten by the header
     * and the 2-bytes to be overwritten by the footer.
     */

    glen = glen + sizeof(header) - 6;
    Tcl_DStringSetLength(dsPtr, (int) glen + FOOTER_SIZE);

    /*
     * Calculate the CRC and append it and length as the footer.
     */

    crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, ubuf, ulen);
    fp = (unsigned char *) dsPtr->string + glen;
    *fp++ = (crc & 0x000000ff);
    *fp++ = (crc & 0x0000ff00) >>  8;
    *fp++ = (crc & 0x00ff0000) >> 16;
    *fp++ = (crc & 0xff000000) >> 24;
    *fp++ = (len & 0x000000ff);
    *fp++ = (len & 0x0000ff00) >>  8;
    *fp++ = (len & 0x00ff0000) >> 16;
    *fp++ = (len & 0xff000000) >> 24;
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ZlibTrace --
 *
 *      AOLserver Tcl trace to add ns_zlib command.
 *
 * Results:
 *      NS_OK.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
ZlibTrace(Tcl_Interp *interp, void *ignored)
{
    Nszlib_Init(interp);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ZlibObjCmd --
 *
 *      Implements the ns_zlib command.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static
int ZlibObjCmd(ClientData ignored, Tcl_Interp *interp, int objc,
		Tcl_Obj * CONST objv[])
{
    Tcl_DString ds;
    int fd, rc, nread;
    Tcl_Obj *obj;
    char *ifile, *ofile, buf[32768];
    gzFile gin, gout;
    unsigned char *inbuf,*outbuf;
    unsigned long inlen,outlen;
    static CONST char *opts[] = {
	"compress", "uncompress", "gzip", "gunzip", "gzipfile",
	NULL
    };
    enum {
	ZCompIdx, ZUnCompIdx, ZGzipIdx, ZGunzipIdx, ZGzipFileIdx
    } opt;

    if (objc < 2) {
		  Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
		  return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
		                      (int *) &opt) != TCL_OK) {
		  return TCL_ERROR;
    }

    switch (opt) {
    case ZCompIdx:
	inbuf = Tcl_GetByteArrayFromObj(objv[2],(int*)&inlen);
	if(!(outbuf = Ns_ZlibCompress(inbuf,inlen,&outlen))) {
	    Tcl_AppendResult(interp,"nszlib: compress failed",0);
	     return TCL_ERROR;
	}
	Tcl_SetObjResult(interp,Tcl_NewByteArrayObj(outbuf,(int)outlen));
	ns_free(outbuf);
	break;

    case ZUnCompIdx:
	inbuf = Tcl_GetByteArrayFromObj(objv[2],(int*)&inlen);
	if(!(outbuf = Ns_ZlibUncompress(inbuf,inlen,&outlen))) {
	    Tcl_AppendResult(interp,"nszlib: uncompress failed",0);
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp,Tcl_NewStringObj((char *) outbuf,(int)outlen));
	ns_free(outbuf);
	break;

    case ZGzipIdx:
	inbuf = Tcl_GetByteArrayFromObj(objv[2],(int*)&inlen);
	obj = NULL;
	Tcl_DStringInit(&ds);
	if (ZlibGzip((char *) inbuf, inlen, 3, &ds) == NS_OK) {
	    obj = Tcl_NewByteArrayObj((unsigned char *) ds.string, ds.length);
	}
	Tcl_DStringFree(&ds);
	if (obj == NULL) {
	    Tcl_AppendResult(interp, "gzip failed", NULL);
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, obj);
	break;

     case ZGunzipIdx:
	gin  = gzopen(Tcl_GetString(objv[2]), "rb");
	if(!gin) {
	    Tcl_AppendResult(interp,"nszlib: gunzip: cannot open ",
			     Tcl_GetString(objv[2]),0);
	    return TCL_ERROR;
	}
	obj = Tcl_NewStringObj(0,0);
	for(;;) {
	    nread = gzread(gin, buf, sizeof(buf));
	    if (nread == 0) {
		break;
	    } else if (nread < 0) {
		Tcl_AppendResult(interp,"nszlib: gunzip: read error ",
				 gzerror(gin,&rc),0);
		Tcl_DecrRefCount(obj);
		gzclose(gin);
		return TCL_ERROR;
	    }
	    Tcl_AppendToObj(obj, buf, nread);
	}
	Tcl_SetObjResult(interp,obj);
	gzclose(gin);
	break;

    case ZGzipFileIdx:
	ifile = Tcl_GetString(objv[2]);
	fd = open(ifile, O_RDONLY|O_BINARY);
	if (fd < 0) {
	    Tcl_AppendResult(interp,"nszlib: gzipfile: cannot open ",
			     ifile, 0);
	    return TCL_ERROR;
	}
	obj = Tcl_NewStringObj(Tcl_GetString(objv[2]),-1);
	Tcl_AppendToObj(obj,".gz",3);
	ofile = Tcl_GetString(obj);
	if(!(gout = gzopen(ofile,"wb"))) {
	    Tcl_AppendResult(interp,"nszlib: gzipfile: cannot create ",
		ofile, 0);
	    Tcl_DecrRefCount(obj);
	    return TCL_ERROR;
	}
	for(;;) {
	    nread = read(fd, buf, sizeof(buf));
	    if (nread == 0) {
		break;
	    }
	    if (nread < 0) {
		Tcl_AppendResult(interp,"nszlib: gzipfile: read error ",
				 strerror(errno),0);
		goto err;
	    }
	    if(gzwrite(gout, buf, (unsigned) nread) != nread) {
		Tcl_AppendResult(interp,"nszlib: gunzip: write error ",
				 gzerror(gout,&rc),0);
err:
		close(fd);
		gzclose(gout);
		unlink(ofile);
		Tcl_DecrRefCount(obj);
		return TCL_ERROR;
	    }
	}
	close(fd);
	gzclose(gout);
	unlink(ifile);
	Tcl_SetObjResult(interp, obj);
	break;
    }
    return TCL_OK;
}
