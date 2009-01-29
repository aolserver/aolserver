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
 * EXPORT NOTICE 
 * 
 * This source code is subject to the U.S. Export Administration
 * Regulations and other U.S. law, and may not be exported or
 * re-exported to certain countries (currently Afghanistan
 * (Taliban-controlled areas), Cuba, Iran, Iraq, Libya, North Korea,
 * Serbia (except Kosovo), Sudan and Syria) or to persons or entities
 * prohibited from receiving U.S. exports (including Denied Parties,
 * Specially Designated Nationals, and entities on the Bureau of
 * Export Administration Entity List).
 */


/*
 * ssltcl.c --
 *
 *      Tcl bindings for various nsssl commands.
 *
 *      Note: This has three entry points -- one for nsd's loader and
 *      two for the loader in the Tcl interpreter used by keygen.tcl.
 *
 */

static const char *RCSID = "@(#): $Header: /Users/dossy/Desktop/cvs/aolserver/nsssl/ssltcl.c,v 1.2 2009/01/29 12:49:45 gneumann Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"
#include "ssl.h"
#include "ssltcl.h"
#include "x509.h"

#include <sys/stat.h>


/*
 * Private function prototypes
 */

static int
InitCommands(Tcl_Interp *interp);

static void *
SslReadFile(char *filename, int *length);

static int
SslWriteFile(char *filename, void *data, int length);

static int
OctetStringDecode(Ns_DString *ds, char *s);

static void
OctetStringEncode(Ns_DString *ds, char *data, int length);

static void
BitStringEncode(Ns_DString *ds, unsigned char *data, int lengthInBits);


/*
 * Tcl commands
 */
static Tcl_CmdProc DerDecodeCmd;

static Tcl_CmdProc DerEncodeCmd;

static Tcl_CmdProc OctetStringCmd;

static Tcl_CmdProc RsaKeyCmd;

static Tcl_CmdProc SignatureCmd;

static Tcl_CmdProc EncryptCmd;

static Tcl_CmdProc DecryptCmd;

static Tcl_CmdProc DigestCmd;

static Tcl_CmdProc InfoCmd;


/*
 * Static variables
 */

/* None at this time. */


/*
 * Public API functions
 */

/* None at this time. */


/*
 * Exported functions
 */



/* 
 *---------------------------------------------------------------------- 
 * 
 * Nsssle_Init --   ***EXPORT VERSION***
 * 
 *      Entry point for stand-alone mode (nsssle.so export version).
 *
 * Results: 
 *      NS_OK
 * 
 * Side effects: 
 *      None.
 * 
 *---------------------------------------------------------------------- 
 */ 
int
Nsssle_Init(Tcl_Interp *interp)
{
#ifdef SSL_EXPORT
    Ns_Log(Notice, "nsssl: "
	   "40-bit export encryption version starting in stand-alone mode");
    InitCommands(interp);
    return TCL_OK;
#else
    Ns_Log(Fatal, "nsssl: "
	   "128-bit domestic encryption not supported by this module");
    return TCL_ERROR;
#endif    
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * Nsssl_Init --   ***DOMESTIC VERSION***
 * 
 *      Entry point for stand-alone mode (nsssl.so domestic version).
 *
 * Results: 
 *      NS_OK
 * 
 * Side effects: 
 *      None.
 * 
 *---------------------------------------------------------------------- 
 */ 
int
Nsssl_Init(Tcl_Interp *interp)
{
#ifdef SSL_EXPORT
    Ns_Log(Fatal, "nsssl: "
	   "40-bit export encryption not supported by this module");
    return TCL_ERROR;
#else
    Ns_Log(Notice, "nsssl: "
	   "128-bit domestic encryption version starting in stand-alone mode");
    InitCommands(interp);
    return TCL_OK;
#endif
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLInterpInit
 * 
 *      Add Tcl commands for SSL functions.
 *
 *      Note: This is called in normal (non-stand-alone) mode.
 * 
 * Results: 
 *      NS_OK
 * 
 * Side effects: 
 *      Adds Tcl commands to the interp.
 * 
 *---------------------------------------------------------------------- 
 */ 
int
NsSSLInterpInit(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "ssl_info", InfoCmd,
		      NULL, NULL);
    
    return NS_OK;
}



/*
 * Private functions
 */



/* 
 *---------------------------------------------------------------------- 
 * 
 * InitCommands
 * 
 *      Add Tcl commands for SSL functions used by keygen.tcl
 *
 *      Note: This is called in stand-alone mode.
 * 
 * Results: 
 *      NS_OK
 * 
 * Side effects: 
 *      Adds Tcl commands to the interp.
 * 
 *---------------------------------------------------------------------- 
 */ 
static int
InitCommands(Tcl_Interp *interp)
{
    
    if (NsSSLInitialize(NULL, NULL) != NS_OK) {
	Ns_Log(Error, "nsssl: failed to initialize nsssl in stand-alone mode");
	return TCL_ERROR;
    }
    
    Tcl_CreateCommand(interp, "osi_der_decode",
		      DerDecodeCmd, NULL, NULL);
    
    Tcl_CreateCommand(interp, "osi_der_encode",
		      DerEncodeCmd, NULL, NULL);
    
    Tcl_CreateCommand(interp, "osi_octet_string",
		      OctetStringCmd, NULL, NULL);
    
    Tcl_CreateCommand(interp, "ssl_rsa_key",
		      RsaKeyCmd, NULL, NULL);
    
    Tcl_CreateCommand(interp, "ssl_signature",
		      SignatureCmd, NULL, NULL);

    Tcl_CreateCommand(interp, "ssl_encrypt",
		      EncryptCmd, NULL, NULL);

    Tcl_CreateCommand(interp, "ssl_decrypt",
		      DecryptCmd, NULL, NULL);
    
    Tcl_CreateCommand(interp, "ssl_digest",
		      DigestCmd, NULL, NULL);

    return TCL_OK;

}



/* 
 *---------------------------------------------------------------------- 
 * 
 * SslReadFile
 * 
 *      Returns the file as a block of allocated memory.
 * 
 * Results: 
 *      Sets length to bytes read.
 *      Returns NULL on error or a pointer to the block otherwise.
 * 
 * Side effects: 
 *      Allocates memory.
 * 
 *---------------------------------------------------------------------- 
 */ 
static void *
SslReadFile(char *filename, int *length)
{
    FILE *fp;
    void *data;
    int err;

    data = NULL;
    fp = NULL;
    err = 1;
    do {
        struct stat st;

        if (stat(filename, &st) != 0) {
            break;
        }

        *length = st.st_size;

        data = ckalloc(*length);

        fp = fopen(filename, "rb");
        if (fp == NULL) {
            break;
        }

        if (fread(data, 1, *length, fp) != (size_t) *length) {
            break;
        }

        err = 0;
    } while(0);

    if (fp != NULL) {
        fclose(fp);
    }

    if ((err) && (data != NULL)) {
        ckfree(data);
        data = NULL;
    }

    return data;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * SslWriteFile
 * 
 *      Write a block of ram to disk.
 * 
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      Does *not* free memory.
 * 
 *---------------------------------------------------------------------- 
 */ 
static int
SslWriteFile(char *filename, void *data, int length)
{
    FILE *fp;
    int status;

    fp = NULL;
    status = NS_ERROR;
    do {
        fp = fopen(filename, "wb");
        if (fp == NULL) {
            break;
        }

        if (fwrite(data, length, 1, fp) == 0) {
            break;
        }

        status = NS_OK;
    } while(0);

    if (fp != NULL) {
        fclose(fp);
    }

    return status;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * OctetStringDecode
 * 
 *      Decodes an octet string
 * 
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      The Ns_DString passed in expands.
 * 
 *---------------------------------------------------------------------- 
 */ 
static int
OctetStringDecode(Ns_DString *ds, char *s)
{
    if (strncasecmp(s, "octet-string ", 13) == 0) {
        s += 13;

        while ((*s != '\0') &&
	       isxdigit( (int) *s) &&
               (s[1] != '\0') &&
	       isxdigit( (int) s[1])) {
            unsigned int val;
            unsigned char c;

            sscanf(s, "%02x", &val);
            c = val;
            Ns_DStringNAppend(ds, (char *) &c, 1);
            s += 2;
        }
        return NS_OK;
    } else {
        return NS_ERROR;
    }
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * OctetStringEncode
 * 
 *      Encodes data as a proper octet string.
 * 
 * Results: 
 *      Ns_DString to the new octet string.
 * 
 * Side effects: 
 *      The Ns_DString passed in will expand.
 * 
 *---------------------------------------------------------------------- 
 */ 
static void
OctetStringEncode(Ns_DString *ds, char *data, int length)
{
    Ns_DStringAppend(ds, "OCTET-STRING ");
    
    while (length > 0) {
        Ns_DStringPrintf(ds, "%02x", (unsigned char) *data);
        data++;
        length--;
    }
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * BitStringEncode
 * 
 *      Encodes data as a bit string.
 * 
 * Results: 
 *      Ns_DString to the new octet string.
 * 
 * Side effects: 
 *      The Ns_DString passed in will expand.
 * 
 *---------------------------------------------------------------------- 
 */ 
static void
BitStringEncode(Ns_DString *ds, unsigned char *data, int lengthInBits)
{
    int paddingBits;
    int length;

    Ns_DStringAppend(ds, "BIT-STRING ");

    paddingBits = 8 - lengthInBits % 8;
    length = lengthInBits / 8;
    if (paddingBits == 8) {
        paddingBits = 0;
    } else {
        length++;
    }
    Ns_DStringPrintf(ds, "%02x", paddingBits);

    while (length > 0) {
        Ns_DStringPrintf(ds, "%02x", *data);
        data++;
        length--;
    }
}    



/* 
 *---------------------------------------------------------------------- 
 * 
 * DerDecodeCmd -- 
 *
 *      Tcl command that returns a string in an ASN.1-style format of
 *      the DER (Distinguished Encoding Rules) object.
 *
 *      Usage:
 *        DERDecode -option arg
 *          -file <filename>
 *          -octets <string containing octec string>
 * 
 * Results: 
 *      Ns_DString to the new DER object.
 * 
 * Side effects: 
 *      The string passed out is TCL_VOLATILE.
 * 
 *----------------------------------------------------------------------
 */
static int
DerDecodeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    unsigned char *der;
    Ns_DString ds;
    Ns_DString dsBuf;
    int status;

    status = TCL_ERROR;
    der = NULL;
    Ns_DStringInit(&ds);
    Ns_DStringInit(&dsBuf);
    do {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # of args: should be \"", argv[0],
			     " { -file filename | -octets octet-string }\"",
			     NULL);
            break;
        }

        if (strcasecmp(argv[1], "-file") == 0) {
            int length;

            der = SslReadFile(argv[2], &length);
            if (der == NULL) {
                Tcl_AppendResult(interp, "Could not read file '", argv[2],
                                 "'.", NULL);
                break;
            }

            if (DERDecode(&ds, der, length, 0) != NS_OK) {
                Tcl_AppendResult(interp, "Could not parse DER.", NULL);
                break;
            }

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
        } else if (strcasecmp(argv[1], "-octets") == 0) {
            if (OctetStringDecode(&dsBuf, argv[2]) != NS_OK) {
                Tcl_AppendResult(interp, "Could not parse OCTET-STRING.",
                                 NULL);
                break;
            }

            if (DERDecode(&ds, (unsigned char *) dsBuf.string,
			  dsBuf.length, 0) != NS_OK) {
                Tcl_AppendResult(interp, "Could not parse DER.", NULL);
                break;
            }

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
        } else {
            Tcl_AppendResult(interp, "usage: \"", argv[0],
                             " { -file filename | -octets octets }\"", NULL);
            break;
        }

        status = TCL_OK;
    } while (0);

    if (der != NULL) {
        ns_free(der);
    }

    Ns_DStringFree(&ds);
    Ns_DStringFree(&dsBuf);

    return status;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * DerEncodeCmd -- 
 *
 *      Tcl command that returns an octet string of the the DER
 *      (Distinguished Encoding Rules) object.
 *
 *      Usage:
 *        osi_der_encode option arg
 *          -octets <string containing octec string>
 *          -isprintablestring <string>
 * 
 * Results: 
 *      Ns_DString to the new DER object.
 * 
 * Side effects: 
 *      The string passed out is TCL_VOLATILE.
 * 
 *----------------------------------------------------------------------
 */
static int
DerEncodeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;
    Ns_DString dsBuf;
    int status;

    status = TCL_ERROR;
    Ns_DStringInit(&ds);
    Ns_DStringInit(&dsBuf);
    do {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # of args: should be \"",
			     argv[0], " option argument\"", NULL);
            break;
        }

        if (strcasecmp(argv[1], "octets") == 0) {
            if (DEREncode(&dsBuf, argv[2]) != NS_OK) {
                Tcl_AppendResult(interp, "Could not parse ASN.1 data.",
                                 NULL);
                break;
            }

            OctetStringEncode(&ds, dsBuf.string, dsBuf.length);

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
	} else if (strcasecmp(argv[1], "IsPrintableString") == 0) {
	    char *p;

	    for (p = argv[2]; *p != '\0'; p++) {
		char c = *p;

		if (!(((c >= 'A') && (c <= 'Z')) ||
		      ((c >= 'a') && (c <= 'z')) ||
		      ((c >= '0') && (c <= '9')) ||
		      (strchr(" '()+,-./:=?", c)!=NULL))) {
		    break;
		}
	    }
            Tcl_SetResult(interp, *p == '\0' ? "1" : "0", TCL_STATIC);

        } else {
            Tcl_AppendResult(interp, "usage: \"", argv[0],
                             " valid options are octets or "
			     "IsPrintableString\"", NULL);
            break;
        }

        status = TCL_OK;
    } while (0);

    Ns_DStringFree(&ds);
    Ns_DStringFree(&dsBuf);

    return status;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * OctetStringCmd
 *
 *      A candy-machine-style command to manipulate octet strings in
 *      Tcl.
 *
 *      Usage:  osi_octet_string command args
 *
 *      commands:
 *        tostring <string containing octet string>
 *          converts the octet string into a null terminated text string
 *        fromstring <string>
 *          returns the string as an octet string
 *        read <filename>
 *          returns the file as an octet string
 *        write <filename> <string containing octet string>
 *          writes the contents of the octet string to a file
 *        frombase64 <string containing data in base64>
 *          returns the data as an octet string
 *        tobase64 <string containing octet string>
 *          converts the octet string into base 64
 * 
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      Depends on command -- see x509.c.
 * 
 *---------------------------------------------------------------------- */
static int
OctetStringCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int status;
    Ns_DString ds;
    unsigned char *rawBytes;

    status = TCL_ERROR;
    rawBytes = NULL;
    Ns_DStringInit(&ds);
    do {
        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # of args: should be \"",
			     argv[0], " { tostring octets | fromstring string "
			     "| read filename "
			     "| write filename octets | frombase64 string "
			     "| tobase64 octets }\"", NULL);
            break;
        }

        if (strcasecmp(argv[1], "tostring") == 0) {
            if (OctetStringDecode(&ds, argv[2]) != NS_OK) {
                Tcl_AppendResult(interp, "Could not parse OCTET-STRING.",
                                 NULL);
                break;
            }

            Ns_DStringNAppend(&ds, "\0", 1);

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
        } else if (strcasecmp(argv[1], "fromstring") == 0) {
            OctetStringEncode(&ds, argv[2], strlen(argv[2]));

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
        } else if (strcasecmp(argv[1], "read") == 0) {
            int length;

            rawBytes = SslReadFile(argv[2], &length);
            if (rawBytes == NULL) {
                Tcl_AppendResult(interp, "Could not read file '", argv[2],
                                 "'.", NULL);
                break;
            }

            OctetStringEncode(&ds, (char *) rawBytes, length);

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
        } else if ((strcasecmp(argv[1], "write") == 0) && (argc == 4)) {
            if (OctetStringDecode(&ds, argv[3]) != NS_OK) {
                Tcl_AppendResult(interp, "Could not parse OCTET-STRING.",
                                 NULL);
                break;
            }

            if (SslWriteFile(argv[2], ds.string, ds.length) != NS_OK) {
                Tcl_AppendResult(interp, "Could not write file '", argv[2],
                                 "'.", NULL);
                break;
            }
        } else if (strcasecmp(argv[1], "frombase64") == 0) {
            B_ALGORITHM_OBJ asciiDecoder;
            Ns_DString dsBuf;
            unsigned int length;
            int err;

            Ns_DStringInit(&dsBuf);
            do {
                unsigned char buf[100];
                char *src;

                err = B_CreateAlgorithmObject(&asciiDecoder);
                if (err != 0) {
                    break;
                }

                err = B_SetAlgorithmInfo(asciiDecoder, AI_RFC1113Recode, NULL);
                if (err != 0) {
                    break;
                }

                err = B_DecodeInit(asciiDecoder);
                if (err != 0) {
                    break;
                }

                src = Ns_StrTrimLeft(argv[2]);
                while (*src != '\0') {
                    char *p;
                    char c;

                    p = strpbrk(src, "\r\n");
                    if (p != NULL) {
                        c = *p;
                        *p = '\0';
                    } else {
                        c = 0;
                        p = src+strlen(src);
                    }

                    err = B_DecodeUpdate(asciiDecoder, buf, &length,
                                         sizeof(buf), (unsigned char *) src,
                                         p-src);
                    if (err != 0) {
                        break;
                    }

                    Ns_DStringNAppend(&dsBuf, (char *) buf, length);

                    if (c != 0) {
                        *p = c;
                        src = Ns_StrTrimLeft(p+1);
                    } else {
                        src = p;
                    }
                }
                if (err != 0) {
                    break;
                }

                err = B_DecodeFinal(asciiDecoder, buf, &length, sizeof(buf));
                if (err != 0) {
                    break;

                }

                if (length > 0) {
                    Ns_DStringNAppend(&dsBuf, (char *) buf, length);
                }

                OctetStringEncode(&ds, dsBuf.string, dsBuf.length);

                Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
            } while(0);

            Ns_DStringFree(&dsBuf);

            B_DestroyAlgorithmObject(&asciiDecoder);

            if (err != 0) {
                Ns_Log(Error, "nsssl: 'osi_octet_string frombase64' failed, "
		       "bsafe error %d", err);
                break;
            }
        } else if (strcasecmp(argv[1], "tobase64") == 0) {
            B_ALGORITHM_OBJ asciiEncoder;
            Ns_DString dsBuf;
            int err;

            if (OctetStringDecode(&ds, argv[2]) != NS_OK) {
                break;
            }

            Ns_DStringInit(&dsBuf);
            do {
                unsigned char buf[100];
                char *src;
                unsigned int updateLength;
                unsigned int len;

                err = B_CreateAlgorithmObject(&asciiEncoder);
                if (err != 0) {
                    break;
                }

                err = B_SetAlgorithmInfo(asciiEncoder, AI_RFC1113Recode, NULL);
                if (err != 0) {
                    break;
                }

                err = B_EncodeInit(asciiEncoder);
                if (err != 0) {
                    break;
                }

                src = ds.string;
                len = ds.length;
                updateLength = 0;
                while (len > 0) {
                    int iToAdd;

                    iToAdd = len >= 48 ? 48 : len;
                    err = B_EncodeUpdate(asciiEncoder, buf, &updateLength,
                                         sizeof(buf), (unsigned char *) src,
                                         iToAdd);
                    if (err != 0) {
                        break;
                    }

                    src += iToAdd;
                    len -= iToAdd;

                    if ((updateLength > 0) && (len != 0)) {
                        /* NAB: I want the last line to get added to final */
                        buf[updateLength] = '\0';
                        Ns_DStringAppend(&dsBuf, (char *) buf);
                        Ns_DStringAppend(&dsBuf, EOLSTRING);
                    }
                }
                if (err != 0) {
                    break;
                }

                err = B_EncodeFinal(asciiEncoder, buf+updateLength, &len,
                                    sizeof(buf)-updateLength);
                if (err != 0) {
                    break;
                }
                len += updateLength;

                if (len > 0) {
                    buf[len] = '\0';
                    Ns_DStringAppend(&dsBuf, (char *) buf);
                    Ns_DStringAppend(&dsBuf, EOLSTRING);
                }

                Tcl_SetResult(interp, dsBuf.string, TCL_VOLATILE);
            } while(0);

            Ns_DStringFree(&dsBuf);

            B_DestroyAlgorithmObject(&asciiEncoder);

            if (err != 0) {
                Ns_Log(Error, "nsssl: 'osi_octet_string tobase64' failed, "
		       "bsafe error %d", err);
                break;
            }
        } else {
            Tcl_AppendResult(interp, "usage: \"", argv[0],
                             " { tostring octets | fromstring string "
                             "| read filename "
                             "| write filename octets "
                             "| frombase64 string "
                             "| tobase64 octets }\"", NULL);
            break;
        }

        status = TCL_OK;
    } while(0);

    if (rawBytes != NULL) {
        ns_free(rawBytes);
    }

    Ns_DStringFree(&ds);

    return status;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * RsaKeyCmd --
 *
 *      Tcl command that creates the keypair.
 *
 *      Usage:
 *        ssl_rsa_key generate <modulus>
 * 
 * Results: 
 *      Octet string of the new keypair from NsSSLGenerateKeypair.
 * 
 * Side effects: 
 *      The string passed out is TCL_VOLATILE.
 * 
 *----------------------------------------------------------------------
 */
static int
RsaKeyCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int status;
    int err;
    Ns_DString ds;
    B_KEY_OBJ publicKey;
    B_KEY_OBJ privateKey;

    status = TCL_ERROR;
    err = 0;
    publicKey = NULL;
    privateKey = NULL;
    Ns_DStringInit(&ds);
    do {
        unsigned int numofmodulusbits;
        ITEM *pItem;

        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # of args: should be \"",
                             argv[0], " command arg\"", NULL);
            break;
        }

        if (strcasecmp(argv[1], "generate") == 0) {
            numofmodulusbits = atoi(argv[2]);
#ifdef SSL_EXPORT
            if ((numofmodulusbits < 256) || (numofmodulusbits > 512)) {
                Tcl_AppendResult(interp, "Number of modulus bits must be "
				 "between 256 and 512 inclusive (export version).", NULL);
                break;
            }
#else
            if ((numofmodulusbits < 256) || (numofmodulusbits > 1024)) {
                Tcl_AppendResult(interp, "Number of modulus bits must be "
				 "between 256 and 1024 inclusive (non-export version).", NULL);
                break;
            }
#endif

            if (NsSSLGenerateKeypair(numofmodulusbits, NULL, &publicKey,
                                     &privateKey) != NS_OK) {
                Tcl_AppendResult(interp, "Unable to generate keys.", NULL);
                break;
            }

            err = B_GetKeyInfo((POINTER *) &pItem, privateKey,
                               KI_PKCS_RSAPrivateBER);
            if (err != 0) {
                break;
            }

            OctetStringEncode(&ds, (char *) pItem->data, pItem->len);

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
        } else if (strcasecmp(argv[1], "publickey") == 0) {
            ITEM item;

            if (OctetStringDecode(&ds, argv[2]) != NS_OK) {
                Tcl_AppendResult(interp, "Could not parse OCTET-STRING.",
                                 NULL);
                break;
            }

            err = B_CreateKeyObject(&privateKey);
            if (err != 0) {
                break;
            }

            item.data = (unsigned char *) ds.string;
            item.len = ds.length;
            err = B_SetKeyInfo(privateKey, KI_PKCS_RSAPrivateBER,
                               (POINTER) &item);
            if (err != 0) {
                break;
            }

            Ns_DStringTrunc(&ds, 0);
            
            err = B_GetKeyInfo((POINTER *) &pItem, privateKey,
                               KI_RSAPublicBER);
            if (err != 0) {
                break;
            }

            OctetStringEncode(&ds, (char *) pItem->data, pItem->len);

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
        }

        status = TCL_OK;
    } while (0);

    if (err != 0) {
        Ns_Log(Error, "nsssl: 'ssl_rsa_key %s' failed, bsafe error %d",
	       argv[1], err);
        Tcl_AppendResult(interp, "encountered BSAFE error", NULL);
    }

    Ns_DStringFree(&ds);

    if (publicKey != NULL) {
        B_DestroyKeyObject(&publicKey);
    }

    if (privateKey != NULL) {
        B_DestroyKeyObject(&privateKey);
    }

    return status;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * SignatureCmd --
 *
 *      Signs an octet string with the private key, suitable for
 *      signing a certificate request.
 *
 *      Usage:
 *        ssl_signature <octet string> <octet string containing private key>
 * 
 * Results: 
 *      Bit string containing the signature.
 * 
 * Side effects: 
 *      The string passed out is TCL_VOLATILE.
 * 
 *----------------------------------------------------------------------
 */
static int
SignatureCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int status;
    int err;
    Ns_DString ds;
    B_ALGORITHM_OBJ algObj;
    B_KEY_OBJ keyObj;

    status = TCL_ERROR;
    err = 0;
    Ns_DStringInit(&ds);
    algObj = NULL;
    keyObj = NULL;
    do {
        if (argc != 4) {
            Tcl_AppendResult(interp, "wrong # of args: should be \"",
                             argv[0], " sign data key\"", NULL);
            break;
        }

        if (strcasecmp(argv[1], "sign") == 0) {
            ITEM item;
            unsigned char buf[1024];
            unsigned int length;

            if (OctetStringDecode(&ds, argv[3]) != NS_OK) {
                Tcl_AppendResult(interp, "Could not parse OCTET-STRING.",
                                 NULL);
                break;
            }

            err = B_CreateKeyObject(&keyObj);
            if (err != 0) {
                break;
            }

            item.data = (unsigned char *) ds.string;
            item.len = ds.length;
            err = B_SetKeyInfo(keyObj, KI_PKCS_RSAPrivateBER,
                               (POINTER) &item);
            if (err != 0) {
                Tcl_AppendResult(interp, "Could not set private key.",
                                 NULL);
                break;
            }

            Ns_DStringTrunc(&ds, 0);

            err = B_CreateAlgorithmObject(&algObj);
            if (err != 0) {
                break;
            }

            err = B_SetAlgorithmInfo(algObj, AI_MD5WithRSAEncryption, NULL);
            if (err != 0) {
                break;
            }

            err = B_SignInit(algObj, keyObj, ALGORITHM_CHOOSER, NULL);
            if (err != 0) {
                break;
            }

            if (OctetStringDecode(&ds, argv[2]) != NS_OK) {
                Tcl_AppendResult(interp, "Could not parse OCTET-STRING.",
                                 NULL);
                break;
            }

            err = B_SignUpdate(algObj, (unsigned char *) ds.string,
                               ds.length, NULL);
            if (err != 0) {
                break;
            }

            err = B_SignFinal(algObj, buf, &length, sizeof(buf), NULL, NULL);
            if (err != 0) {
                break;
            }

            Ns_DStringTrunc(&ds, 0);

            BitStringEncode(&ds, buf, length*8);

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
        } else {
            Tcl_AppendResult(interp, "unknown command \"", argv[1],
                             "\": should be sign.", NULL);
            break;
        }

        status = TCL_OK;
    } while (0);

    if (err != 0) {
        Ns_Log(Error, "nsssl: 'ssl_signature' failed, bsafe error %d", err);
        Tcl_AppendResult(interp, "encountered BSAFE error", NULL);
    }

    Ns_DStringFree(&ds);

    if (algObj != NULL) {
        B_DestroyAlgorithmObject(&algObj);
    }

    if (keyObj != NULL) {
        B_DestroyKeyObject(&keyObj);
    }

    return status;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * EncryptCmd --
 *
 *      Encrypts data.
 *
 *      Usage:
 *        ssl_encrypt algorithm <data> <key>
 * 
 * Results: 
 *      Octet string representation of ciphertext.
 * 
 * Side effects: 
 *      The string passed out is TCL_VOLATILE.
 * 
 *----------------------------------------------------------------------
 */
static int
EncryptCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int status;
    int err;
    Ns_DString ds;
    Ns_DString dsBuf;
    B_ALGORITHM_OBJ encryptor;
    B_KEY_OBJ key;

    status = TCL_ERROR;
    err = 0;
    encryptor = NULL;
    key = NULL;
    Ns_DStringInit(&ds);
    Ns_DStringInit(&dsBuf);
    do {
        ITEM item;
        unsigned char buf[1024];
        unsigned char *data;
        unsigned int length, updateLength;

        if (argc != 4) {
            Tcl_AppendResult(interp, "wrong # of args: should be \"",
                             argv[0], " algorithm data key\"", NULL);
            break;
        }

        err = B_CreateAlgorithmObject(&encryptor);
        if (err != 0) {
            break;
        }

        err = B_CreateKeyObject(&key);
        if (err != 0) {
            break;
        }

        if (OctetStringDecode(&ds, argv[3]) != NS_OK) {
            Tcl_AppendResult(interp, "Could not parse key OCTET-STRING.",
                             NULL);
            break;
        }

        if (OctetStringDecode(&dsBuf, argv[2]) != NS_OK) {
            Tcl_AppendResult(interp, "Could not parse data OCTET-STRING.",
                             NULL);
            break;
        }

        if (strcasecmp(argv[1], "rc4") == 0) {
            item.data = (unsigned char *) ds.string;
            item.len = ds.length;
#ifdef SSL_EXPORT
            if (item.len > 5) {
                item.len = 5;
            }
#endif
            err = B_SetKeyInfo(key, KI_Item, (POINTER) &item);
            if (err != 0) {
                Tcl_AppendResult(interp, "Could not set key.",
                                 NULL);
                break;
            }

            Ns_DStringTrunc(&ds, 0);

            err = B_SetAlgorithmInfo(encryptor, AI_RC4, NULL);
            if (err != 0) {
                break;
            }

            err = B_EncryptInit(encryptor, key, ALGORITHM_CHOOSER, NULL);
            if (err != 0) {
                break;
            }

            data = (unsigned char *) dsBuf.string;
            length = dsBuf.length;
            while (length > 0) {
                int iToEncrypt;

                iToEncrypt = length > sizeof(buf) ? sizeof(buf) : length;
                err = B_EncryptUpdate(encryptor, buf, &updateLength,
                                      sizeof(buf), data, iToEncrypt,
                                      NULL, NULL);
                if (err != 0) {
                    break;
                }

                if (updateLength > 0) {
                    Ns_DStringNAppend(&ds, (char *) buf, updateLength);
                }

                data += iToEncrypt;
                length -= iToEncrypt;
            }

            if (err != 0) {
                break;
            }

            err = B_EncryptFinal(encryptor, buf, &updateLength, sizeof(buf),
                                 NULL, NULL);
            if (err != 0) {
                break;
            }

            if (updateLength > 0) {
                Ns_DStringNAppend(&ds, (char *) buf, updateLength);
            }

            Ns_DStringTrunc(&dsBuf, 0);

            OctetStringEncode(&dsBuf, ds.string, ds.length);

            Tcl_SetResult(interp, dsBuf.string, TCL_VOLATILE);
        } else {
            Tcl_AppendResult(interp, "unknown algorithm \"", argv[1],
                             "\": should be rc4.", NULL);
            break;
        }

        status = TCL_OK;
    } while (0);

    if (err != 0) {
        Ns_Log(Error, "nsssl: 'ssl_encrypt' failed, bsafe error %d", err);
        Tcl_AppendResult(interp, "encountered BSAFE error", NULL);
    }

    Ns_DStringFree(&ds);

    Ns_DStringFree(&dsBuf);

    if (encryptor != NULL) {
        B_DestroyAlgorithmObject(&encryptor);
    }

    if (key != NULL) {
        B_DestroyKeyObject(&key);
    }

    return status;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * DecryptCmd --
 *
 *      Decrypts data.
 *
 *      Usage:
 *        ssl_decrypt algorithm <data> <key>
 * 
 * Results: 
 *      Octet string representation of cleartext.
 * 
 * Side effects: 
 *      The string passed out is TCL_VOLATILE.
 * 
 *----------------------------------------------------------------------
 */
static int
DecryptCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int status;
    int err;
    Ns_DString ds;
    Ns_DString dsBuf;
    B_ALGORITHM_OBJ decryptor;
    B_KEY_OBJ key;

    status = TCL_ERROR;
    err = 0;
    decryptor = NULL;
    key = NULL;
    Ns_DStringInit(&ds);
    Ns_DStringInit(&dsBuf);
    do {
        ITEM item;
        unsigned char buf[1024];
        unsigned char *data;
        unsigned int length, updateLength;

        if (argc != 4) {
            Tcl_AppendResult(interp, "wrong # of args: should be \"",
                             argv[0], " algorithm data key\"", NULL);
            break;
        }

        err = B_CreateAlgorithmObject(&decryptor);
        if (err != 0) {
            break;
        }

        err = B_CreateKeyObject(&key);
        if (err != 0) {
            break;
        }

        if (OctetStringDecode(&ds, argv[3]) != NS_OK) {
            Tcl_AppendResult(interp, "Could not parse key OCTET-STRING.",
                             NULL);
            break;
        }

        if (OctetStringDecode(&dsBuf, argv[2]) != NS_OK) {
            Tcl_AppendResult(interp, "Could not parse data OCTET-STRING.",
                             NULL);
            break;
        }

        if (strcasecmp(argv[1], "rc4") == 0) {
            item.data = (unsigned char *) ds.string;
            item.len = ds.length;
#ifdef SSL_EXPORT
            if (item.len > 5) {
                item.len = 5;
            }
#endif
            err = B_SetKeyInfo(key, KI_Item, (POINTER) &item);
            if (err != 0) {
                Tcl_AppendResult(interp, "Could not set key.",
                                 NULL);
                break;
            }

            Ns_DStringTrunc(&ds, 0);

            err = B_SetAlgorithmInfo(decryptor, AI_RC4, NULL);
            if (err != 0) {
                break;
            }

            err = B_DecryptInit(decryptor, key, ALGORITHM_CHOOSER, NULL);
            if (err != 0) {
                break;
            }

            data = (unsigned char *) dsBuf.string;
            length = dsBuf.length;
            while (length > 0) {
                int iToDecrypt;

                iToDecrypt = length > sizeof(buf) ? sizeof(buf) : length;
                err = B_DecryptUpdate(decryptor, buf, &updateLength,
                                      sizeof(buf), data, iToDecrypt,
                                      NULL, NULL);
                if (err != 0) {
                    break;
                }

                if (updateLength > 0) {
                    Ns_DStringNAppend(&ds, (char *) buf, updateLength);
                }

                data += iToDecrypt;
                length -= iToDecrypt;
            }

            if (err != 0) {
                break;
            }

            err = B_DecryptFinal(decryptor, buf, &updateLength, sizeof(buf),
                                 NULL, NULL);
            if (err != 0) {
                break;
            }

            if (updateLength > 0) {
                Ns_DStringNAppend(&ds, (char *) buf, updateLength);
            }

            Ns_DStringTrunc(&dsBuf, 0);

            OctetStringEncode(&dsBuf, ds.string, ds.length);

            Tcl_SetResult(interp, dsBuf.string, TCL_VOLATILE);
        } else {
            Tcl_AppendResult(interp, "unknown algorithm \"", argv[1],
                             "\": should be rc4.", NULL);
            break;
        }

        status = TCL_OK;
    } while (0);

    if (err != 0) {
        Ns_Log(Error, "nsssl: 'ssl_decrypt' failed, bsafe error %d", err);
        Tcl_AppendResult(interp, "encountered BSAFE error", NULL);
    }

    Ns_DStringFree(&ds);

    Ns_DStringFree(&dsBuf);

    if (decryptor != NULL) {
        B_DestroyAlgorithmObject(&decryptor);
    }

    if (key != NULL) {
        B_DestroyKeyObject(&key);
    }

    return status;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * DigestCmd --
 *
 *      Generates an octet string containing a message digest (hash).
 *      Note that MD5 is the only digest supported.
 *
 *      Usage:
 *        ssl_digest algorithm <data>
 * 
 * Results: 
 *      Octet string representation of the message digest.
 * 
 * Side effects: 
 *      The string passed out is TCL_VOLATILE.
 * 
 *----------------------------------------------------------------------
 */
static int
DigestCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int status;
    int err;
    Ns_DString ds;
    B_ALGORITHM_OBJ digester;

    status = TCL_ERROR;
    err = 0;
    digester = NULL;
    Ns_DStringInit(&ds);
    do {
        unsigned char digest[16];
        unsigned int length;

        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # of args: should be \"",
                             argv[0], " algorithm data\"", NULL);
            break;
        }

        err = B_CreateAlgorithmObject(&digester);
        if (err != 0) {
            break;
        }

        if (OctetStringDecode(&ds, argv[2]) != NS_OK) {
            Tcl_AppendResult(interp, "Could not parse OCTET-STRING.",
                             NULL);
            break;
        }

        if (strcasecmp(argv[1], "md5") == 0) {
            err = B_SetAlgorithmInfo(digester, AI_MD5, NULL);
            if (err != 0) {
                break;
            }

            err = B_DigestInit(digester, NULL, ALGORITHM_CHOOSER, NULL);
            if (err != 0) {
                break;
            }

            err = B_DigestUpdate(digester, (unsigned char *) ds.string,
                                 ds.length, NULL);
            if (err != 0) {
                break;
            }

            err = B_DigestFinal(digester, digest, &length, 16, NULL);
            if ((err != 0) || (length != 16)) {
                break;
            }

            Ns_DStringTrunc(&ds, 0);

            OctetStringEncode(&ds, (char *) digest, 16);

            Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
        } else {
            Tcl_AppendResult(interp, "unknown algorithm \"", argv[1],
                             "\": should be md5.", NULL);
            break;
        }

        status = TCL_OK;
    } while (0);

    if (err != 0) {
        Ns_Log(Error, "nsssl: 'ssl_digest' failed, bsafe error %d", err);
        Tcl_AppendResult(interp, "encountered BSAFE error", NULL);
    }

    Ns_DStringFree(&ds);
            
    if (digester != NULL) {
        B_DestroyAlgorithmObject(&digester);
    }

    return status;
}



/*
 *----------------------------------------------------------------------
 *
 * InfoCmd --
 *
 *      Returns a proper Tcl list describing this nsssl module:
 *       {SSL_EXPORT} {SSL_PROTOCOL_VERSION}
 *
 * Results:
 *	Tcl string result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
InfoCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{

    Ns_DString ds;
    
    Ns_DStringInit(&ds);
    
#ifndef SSL_EXPORT
    Ns_DStringPrintf(&ds, "{%d} ", NS_FALSE);
#else
    Ns_DStringPrintf(&ds, "{%d} ", SSL_EXPORT);
#endif
    Ns_DStringPrintf(&ds, "{%s} ", SSL_PROTOCOL_VERSION);
    
    Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
    
    Ns_DStringFree(&ds);
    
    return NS_OK;
}

