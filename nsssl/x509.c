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
 * x509.c --
 *
 *	x.509 Key and Certificate handling routines for SSL v2.
 *
 *      BSAFE provides all these functions even though they are
 *      re-implemented here.  Eventually, this file and its
 *      functions will disappear.
 *
 */

static const char *RCSID = "@(#): $Header: /Users/dossy/Desktop/cvs/aolserver/nsssl/x509.c,v 1.2 2001/04/24 19:43:26 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;


#include "ns.h"
#include "ssl.h"
#include "ssltcl.h"
#include "x509.h"

#include <ctype.h>

static int nsX509Initialized = NS_FALSE;
static B_ALGORITHM_OBJ asciiDecoder = (B_ALGORITHM_OBJ) NULL_PTR;
static Ns_Cs nsAsciiDecoderCS;

/*
 * Private function prototypes.
 */

static void X509Cleanup(void *ignore);
static int X509Initialize(void);


/* 
 *---------------------------------------------------------------------- 
 * 
 * GetPrivateKey --
 * 
 *      Get a BER-encoded PKCS rsa private key from a PEM format file.
 *
 * Results: 
 *      NS_OK or NS_ERROR.
 * 
 * Side effects: 
 *      Memory is allocated to hold the object in privateKey.
 *
 *----------------------------------------------------------------------
 */
int
GetPrivateKey(B_KEY_OBJ * privateKey, char *filename)
{
    unsigned char  *key;
    int             status;
    int             err;
    
    status = NS_ERROR;
    key = NULL;
    *privateKey = (B_KEY_OBJ) NULL_PTR;
    err = 0;
    do {
        int             length;
        ITEM            info;
	
        key = GetBerFromPEM(filename, SECTION_RSA_PRIVATE_KEY, &length);
        if (key == NULL) {
            break;
        }
	
        err = B_CreateKeyObject(privateKey);
        if (err != 0) {
            break;
        }
        info.data = key;
        info.len = length;
        err = B_SetKeyInfo(*privateKey,
			   KI_PKCS_RSAPrivateBER,
			   (unsigned char *) &info);
        if (err != 0) {
            break;
        }
        status = NS_OK;
    } while (0);
    
    if (status != NS_OK) {
        B_DestroyKeyObject(privateKey);
        *privateKey = (B_KEY_OBJ) NULL_PTR;
    }
    
    if (key != NULL) {
        ns_free(key);
    }
    
    if (err != 0) {
        Ns_Log(Error, "nsssl: failed to get private key, bsafe error %d", err);
    }
    return status;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * GetCertificate --
 * 
 *      Get a BER encoded X509 certificate from a PEM format file.
 *
 * Results: 
 *      NS_OK or NS_ERROR.
 * 
 * Side effects: 
 *      See function "GetBerFromPEM."
 *
 *----------------------------------------------------------------------
 */
int
GetCertificate(unsigned char **pCertificate,
	       int            *length,
	       char           *filename)
{
    *pCertificate = GetBerFromPEM(filename, SECTION_X509_CERTIFICATE,
				  length);
    if (*pCertificate == NULL) {
        *pCertificate = GetBerFromPEM(filename, SECTION_CERTIFICATE,
				      length);
    }
    return (*pCertificate == NULL) ? NS_ERROR : NS_OK;
}



/*
 * Key and Certificate Generation Functions.
 */



/* 
 *---------------------------------------------------------------------- 
 * 
 * PrivateKeyToPEM --
 * 
 *      Generate a PEM format BER encoded version of a private key.
 *
 * Results: 
 *
 * 
 * Side effects: 
 *      
 *
 *----------------------------------------------------------------------
 */
char *
PrivateKeyToPEM(B_KEY_OBJ privateKey)
{
    Ns_DString      ds;
    char           *berKey;
    int             err;
    
    Ns_DStringInit(&ds);
    
    berKey = NULL;
    err = 0;
    do {
        ITEM           *info;
        B_ALGORITHM_OBJ asciiEncoder = (B_ALGORITHM_OBJ) NULL_PTR;
        unsigned int    updateLength;
        unsigned int    bufLength;
        unsigned int    length;
	
        err = B_CreateAlgorithmObject(&asciiEncoder);
        if (err != 0) {
            break;
        }
        err = B_SetAlgorithmInfo(asciiEncoder,
				 AI_RFC1113Recode,
				 NULL_PTR);
        if (err != 0) {
            break;
        }
        err = B_EncodeInit(asciiEncoder);
        if (err != 0) {
            break;
        }
        err = B_GetKeyInfo((POINTER *) & info,
			   privateKey,
			   KI_PKCS_RSAPrivateBER);
        if (err != 0) {
            break;
        }
        bufLength = info->len * 2;
        berKey = ns_malloc(bufLength);
        err = B_EncodeUpdate(asciiEncoder,
			     (unsigned char *) berKey,
			     &updateLength,
			     bufLength,
			     info->data,
			     info->len);
        if (err != 0) {
            break;
        }
        length = updateLength;
	
        err = B_EncodeFinal(asciiEncoder,
			    (unsigned char *)
			    (berKey + updateLength),
			    &updateLength,
			    bufLength - updateLength);
        if (err != 0) {
            break;
        }
        length += updateLength;
        berKey[length] = '\0';
	
        Ns_DStringPrintf(&ds, "-----BEGIN %s-----" EOLSTRING,
			 SECTION_RSA_PRIVATE_KEY);
        Ns_DStringAppend(&ds, berKey);
        Ns_DStringPrintf(&ds, EOLSTRING "-----END %s-----" EOLSTRING,
			 SECTION_RSA_PRIVATE_KEY);
	
        ns_free(berKey);
	
        berKey = Ns_DStringExport(&ds);
    } while (0);
    
    Ns_DStringFree(&ds);
    
    if (err != 0) {
        Ns_Log(Error, "nsssl: failed to convert private key to PEM, "
	       "bsafe error %d", err);
        if (berKey != NULL) {
            ns_free(berKey);
            berKey = NULL;
        }
    }
    return berKey;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * GetBerFromPEM --
 * 
 *      Grabs a BER (Basic Encoding Rules) object from a PEM (Privacy
 *      Enchanced Mail) format file.  Returns NULL on error.
 *
 * Results: 
 *      Pointer to the BER object.
 * 
 * Side effects: 
 *      Memory is allocated to hold the object.
 *
 *----------------------------------------------------------------------
 */
unsigned char *
GetBerFromPEM(char *filename, char *section, int *length)
{
    FILE           *fp;
    unsigned char  *chunk;
    int             err = 0;
    
    if (X509Initialize() != NS_OK) {
        return NULL;
    }
    Ns_CsEnter(&nsAsciiDecoderCS);

    chunk = NULL;
    do {
        char            buf[100];
        Ns_DString      ds;
        int             posBegin, posEnd;
        int             i;
        unsigned int    updateLength;

        fp = fopen(filename, "rb");
        if (fp == NULL) {
            Ns_Log(Error, "nsssl: unable to open pem file '%s'", filename);
            break;
        }
        posBegin = -1;
        Ns_DStringInit(&ds);
        Ns_DStringPrintf(&ds, "-----BEGIN %s-----", section);
        while (fgets(buf, sizeof(buf), fp) != NULL) {
            if ((*buf == '-') &&
		(strncasecmp(buf, ds.string, ds.length) == 0)) {
                posBegin = ftell(fp);
                break;
            }
        }
	
        Ns_DStringFree(&ds);
	
        if (posBegin == -1) {
            if (strstr(section, "CERTIFICATE") == NULL) { 
                Ns_Log(Error, "nsssl: error parsing pem file '%s': "
		       "'-----BEGIN %s-----' not found", filename, section);
            }
            break;
        }
        posEnd = -1;
        Ns_DStringPrintf(&ds, "-----END %s-----", section);
        while (fgets(buf, sizeof(buf), fp) != NULL) {
            i = ftell(fp);
            if ((*buf == '-') &&
		(strncasecmp(buf, ds.string, ds.length) == 0)) {
                posEnd = i;
                break;
            }
        }
	
        Ns_DStringFree(&ds);

        i = posEnd - posBegin;
        if ((posEnd == -1) || (i == 0)) {
            Ns_Log(Error, "nsssl: error parsing pem file '%s'", filename);
            break;
        }
        if (fseek(fp, posBegin, SEEK_SET) == -1) {
            Ns_Log(Error, "nsssl: failed to rewind pem file '%s'", filename);
            break;
        }
        /* 
	 * A little wasteful since it will actually be .75i, but short
	 * term.
	 */
        chunk = ns_malloc(i);
        if (chunk == NULL) {
            break;
        }
        *length = 0;
        while ((fgets(buf, sizeof(buf), fp) != NULL) &&
	       (ftell(fp) < posEnd)) {
            char           *p = Ns_StrTrim(buf);
	    
            if (*p != '\0') {
                err = B_DecodeUpdate(asciiDecoder, chunk + *length,
				     &updateLength, i - *length,
				     (unsigned char *) p,
				     strlen(p));
                if (err != 0) {
                    ns_free(chunk);
                    chunk = NULL;
                    break;
                }
                *length += updateLength;
            }
        }
        if (chunk == NULL) {
            break;
        }
        err = B_DecodeFinal(asciiDecoder,
			    chunk + *length,
			    &updateLength,
			    i - *length);
        if (err != 0) {
            ns_free(chunk);
            chunk = NULL;
            break;
        }
        *length += updateLength;
    } while (0);
    
    Ns_CsLeave(&nsAsciiDecoderCS);
    
    if (fp != NULL) {
        fclose(fp);
    }
    if (err != 0) {
        Ns_Log(Error, "nsssl: failed decoding ber, bsafe error %d", err);
    }
    return (unsigned char *) chunk;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * AddLengthCount --
 * 
 *      Big Endian Length counts for DER (Distinguished Encoding
 *      Rules) objects.
 *
 * Results: 
 *
 * 
 * Side effects: 
 *      
 *
 *----------------------------------------------------------------------
 */
void
AddLengthCount(Ns_DString *ds, unsigned int length)
{
    unsigned char   c;
    
    if (length <= 127) {
        /*
	 * Short length.
	 */
        c = length;
        Ns_DStringNAppend(ds, (char *) &c, 1);
	
    } else {
        /*
	 * Long length.
	 */
        unsigned char   buf[126];
        int             i, l;
	
        l = (int) floor(log((double) length) / log(256.0));
	/*
	 * Note: ceil fails if length == 1
	 */
        l++;
        c = ((unsigned char) l) | 0x80;
        Ns_DStringNAppend(ds, (char *) &c, 1);
	
        i = l;
        while (i) {
            buf[--i] = length & 0xff;
            length >>= 8;
        }
	
        Ns_DStringNAppend(ds, (char *) buf, l);
    }
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * GetLengthCount --
 * 
 *      
 *
 * Results: 
 *
 * 
 * Side effects: 
 *      
 *
 *----------------------------------------------------------------------
 */
int
GetLengthCount(unsigned char *der, unsigned int *length)
{
    unsigned int    len;
    unsigned char  *p;
    
    p = der;
    if (*p & 0x80) {
        /*
	 * Long-form.
	 */
        int i;
	
        len = 0;
        for (i = *p & 0x7f; i; i--) {
            len <<= 8;
            len |= *(++p);
        }
        p++;
    } else {
        /*
	 * Short-form.
	 */
        len = *p & 0x7f;
        p++;
    }
    
    *length = len;
    
    return p - der;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * SetOf --
 * 
 *      Determines where der is a SET OF by checking that every item
 *      is a SET.
 *
 * Results: 
 *      NS_TRUE or NS_FALSE.
 * 
 * Side effects: 
 *      None.
 *
 *----------------------------------------------------------------------
 */
int
SetOf(unsigned char *der, int length)
{
    while (length > 0) {
        if (*der == 0x31) {
            /* constructed SET */
            unsigned int    len;
            int             l;
	    
            der++;
            length--;
	    
            l = GetLengthCount(der, &len);
            der += l + len;
            length -= l + len;
        } else {
            break;
        }
    }
    
    return length == 0 ? NS_TRUE : NS_FALSE;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * DecodeSetOf --
 * 
 *      Decodes a SET OF by looking into each SET element and decoding
 *      its content.
 *
 * Results: 
 *      NS_OK or NS_ERROR.
 * 
 * Side effects: 
 *      None.
 *
 *----------------------------------------------------------------------
 */
int
DecodeSetOf(Ns_DString *ds, unsigned char *der, int length, int indent)
{
    while (length > 0) {
        if (*der == 0x31) {
            /*
	     * Constructed SET.
	     */
            unsigned int    len;
            int             l;
	    
            der++;
            length--;
	    
            l = GetLengthCount(der, &len);
            der += l;
            length -= l;
	    
            if (DERDecode(ds, der, len, indent) != NS_OK) {
                break;
            }
            der += len;
            length -= len;
        } else {
            break;
        }
    }
    
    return length == 0 ? NS_OK : NS_ERROR;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * RecodeAsSetOf --
 * 
 *      Sorts the items in the der in dsSrc, then wraps them in SETs
 *      and returns them in dsDest (not for STRICT_DER_RULES).
 *
 * Results: 
 *      
 * 
 * Side effects: 
 *      
 *
 *----------------------------------------------------------------------
 */
void
RecodeAsSetOf(Ns_DString *dsSrc, Ns_DString *dsDest)
{
    unsigned char  *der;
    unsigned int    length;
    
    der = (unsigned char *) dsSrc->string;
    length = dsSrc->length;
    while (length > 0) {
        unsigned int    len;
        unsigned char  *p;
        unsigned char   c;
        int             l;
	
        p = der;
        if ((*der & 0x1f) ^ 0x1f) {
            /*
	     * Low-tag-number form.
	     */
            der++;
        } else {
            /*
	     * High-tag-number form.
	     */
            do {
                der++;
            } while (*der & 0x80);
            der++;
        }
	
        l = GetLengthCount(der, &len) + (der - p);
        len += l;
        der = p + len;
        length -= len;
	
        c = 0x31;                       /* constructed SET */
        Ns_DStringNAppend(dsDest, (char *) &c, 1);

        AddLengthCount(dsDest, len);
	
        Ns_DStringNAppend(dsDest, (char *) p, (int) len);
    }
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * DERDecode --
 * 
 *      Decode a der object.
 *
 * Results: 
 *      NS_OK or NS_ERROR.
 * 
 * Side effects: 
 *      This is a recursive function.
 *
 *----------------------------------------------------------------------
 */
int
DERDecode(Ns_DString *ds, unsigned char *der, int length, int indent)
{
    while (length > 0) {
        unsigned char   class;
        char           *name;
        unsigned int    tag;
        unsigned int    len;
        int             fConstructed;
        int             i;
	
        class = ((unsigned)(*der & 0xc0)) >> 6;
        fConstructed = *der & 0x20;
        if ((*der & 0x1f) ^ 0x1f) {
            /*
	     * Low-tag-number form.
	     */
            tag = *der & 0x1f;
            der++;
            length--;
        } else {
            /*
	     * High-tag-number form.
	     */
            tag = 0;
            do {
                tag <<= 7;
                tag |= *(++der) & 0x7f;
                length--;
            } while (*der & 0x80);
            der++;
            length--;
        }
	
        i = GetLengthCount(der, &len);
        der += i;
        length -= i;
	
        if (class == 0) {
            /*
	     * Universal class.
	     */
            switch (tag) {
            case 2:
                name = "INTEGER";
                break;
            case 3:
                name = "BIT-STRING";
                break;
            case 4:
                name = "OCTET-STRING";
                break;
            case 5:
                name = "NULL";
                break;
            case 6:
                name = "OBJECT-IDENTIFIER";
                break;
            case 16:
                name = "SEQUENCE";
                break;
            case 17:
                name = "SET";
                break;
            case 19:
                name = "PrintableString";
                break;
            case 20:
                name = "T61String";
                break;
            case 22:
                name = "IA5String";
                break;
            case 23:
                name = "UTCTime";
                break;
            default:
                name = NULL;
            }
            if (name == NULL) {
                Ns_Log(Debug, "nsssl: unknown tag type '%u'", tag);
                break;
            }
        }
        for (i = 0; i < indent; i++) {
            Ns_DStringAppend(ds, "  ");
        }
	
        if (len > 0) {
            if (class == 0) {
                Ns_DStringPrintf(ds, "%s ", name);
            } else {
                Ns_DStringPrintf(ds, ":%1x%04x ", class, tag);
            }
	    
            if (fConstructed) {
                Ns_DStringAppend(ds, "{" EOLSTRING);
		
                if (!SetOf(der, len)) {
                    if (DERDecode(ds, der, len, indent + 1) != NS_OK) {
                        break;
                    }
                } else {
                    for (i = 0; i <= indent; i++) {
                        Ns_DStringAppend(ds, "  ");
                    }
                    Ns_DStringAppend(ds, "SET-OF {" EOLSTRING);
                    if (DecodeSetOf(ds, der, len, indent + 2) != NS_OK) {
                        break;
                    }
                    for (i = 0; i <= indent; i++) {
                        Ns_DStringAppend(ds, "  ");
                    }
                    Ns_DStringAppend(ds, "}" EOLSTRING);
                }
		
                for (i = 0; i < indent; i++) {
                    Ns_DStringAppend(ds, "  ");
                }
                Ns_DStringAppend(ds, "}" EOLSTRING);
                der += len;
                length -= len;
            } else {
                switch (tag) {
                case 2:
                case 3:
                case 4:
                    while (len > 0) {
                        Ns_DStringPrintf(ds, "%02x", *der++);
                        len--;
                        length--;
                    }
                    Ns_DStringAppend(ds, EOLSTRING);
                    break;
                case 19:
                case 20:
                case 22:
                case 23:
                    Ns_DStringAppend(ds, "\"");
                    Ns_DStringNAppend(ds, (char *) der, len);
                    Ns_DStringAppend(ds, "\"" EOLSTRING);
                    der += len;
                    length -= len;
                    len = 0;
                    break;
                case 6:
                    Ns_DStringAppend(ds, "( ");
                    Ns_DStringPrintf(ds, "%d ", *der / 40);
                    Ns_DStringPrintf(ds, "%d ", *der % 40);
                    der++;
                    length--;
                    len--;
                    if (len > 0) {
                        unsigned int    val;
			
                        val = 0;
                        while (len > 0) {
                            val |= *der & 0x7f;
                            if (!(*der & 0x80)) {
                                Ns_DStringPrintf(ds, "%d ", val);
                                val = 0;
                            } else {
                                val <<= 7;
                            }
                            der++;
                            length--;
                            len--;
                        }
                    }
                    Ns_DStringAppend(ds, ")" EOLSTRING);
                    break;
                default:
                    break;
                }
                if (len > 0) {
                    break;
                }
            }
        } else {
            if (class == 0) {
                Ns_DStringPrintf(ds, "%s" EOLSTRING, name);
            } else {
                Ns_DStringPrintf(ds, ":%1x%04x:" EOLSTRING, class, tag);
            }
        }
    }
    
    return length == 0 ? NS_OK : NS_ERROR;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * DEREncode --
 * 
 *      Encode a der object.
 *
 *      Note: asn1 must be a writable string.
 *
 * Results: 
 *      NS_OK or NS_ERROR.
 * 
 * Side effects: 
 *      This is a recursive function.
 *
 *----------------------------------------------------------------------
 */
int
DEREncode(Ns_DString *ds, char *asn1)
{
    Ns_DString      dsBuf;
    int             status;
    
    status = NS_OK;
    Ns_DStringInit(&dsBuf);
    asn1 = Ns_StrTrimLeft(asn1);
    while (*asn1 != '\0') {
        char           *p;
        unsigned char   tag;
        unsigned char   class;
        unsigned char   c;
        int             fConstructed;
        int             fHasArg;
        int             fSetOf;
        int             fInsertRaw;
	
        fHasArg = 1;
        fConstructed = 0;
        fSetOf = 0;
        fInsertRaw = 0;
        if (*asn1 == ':') {
            /*
	     * Application, context-specific or private class.
	     */
            unsigned int    val1, val2;
	    
            if (sscanf(++asn1, "%1x%04x", &val1, &val2) != 2) {
                Ns_Log(Error, "nsssl: unable to parse class/tag id");
                break;
            }
            asn1 += 5;
            if (*asn1 == ':') {
                fHasArg = 0;
                asn1++;
            }
            class = val1;
            tag = val2;
        } else {
            /*
	     * Universal class.
	     */
	    
            class = 0;
            if (strncasecmp(asn1, "integer", 7) == 0) {
                asn1 += 7;
                tag = 2;
            } else if (strncasecmp(asn1, "bit-string", 10) == 0) {
                asn1 += 10;
                tag = 3;
            } else if (strncasecmp(asn1, "octet-string", 12) == 0) {
                asn1 += 12;
                tag = 4;
            } else if (strncasecmp(asn1, "null", 4) == 0) {
                asn1 += 4;
                tag = 5;
                fHasArg = 0;
            } else if (strncasecmp(asn1, "object-identifier", 17) == 0) {
                asn1 += 17;
                tag = 6;
            } else if (strncasecmp(asn1, "sequence", 8) == 0) {
                asn1 += 8;
                tag = 16;
            } else if (strncasecmp(asn1, "set-of", 6) == 0) {
                asn1 += 6;
                tag = 17;
                fSetOf = 1;
                fInsertRaw = 1;
            } else if (strncasecmp(asn1, "set", 3) == 0) {
                asn1 += 3;
                tag = 17;
            } else if (strncasecmp(asn1, "PrintableString", 15) == 0) {
                asn1 += 15;
                tag = 19;
            } else if (strncasecmp(asn1, "T61String", 9) == 0) {
                asn1 += 9;
                tag = 20;
            } else if (strncasecmp(asn1, "IA5String", 9) == 0) {
                asn1 += 9;
                tag = 22;
            } else if (strncasecmp(asn1, "UTCTime", 7) == 0) {
                asn1 += 7;
                tag = 23;
            } else if (strncasecmp(asn1, "INSERT-OCTET-STRING", 19) == 0) {
                asn1 += 19;
                tag = 4;
                fInsertRaw = 1;
            } else {
                Ns_Log(Error, "nsssl: unknown tag type");
                break;
            }
        }
	
        if (fHasArg) {
            asn1 = Ns_StrTrimLeft(asn1);
            if (*asn1 == '\0') {
                Ns_Log(Error, "nsssl: tag type %d needs an arg", tag);
                status = NS_ERROR;
                break;
            }
            if (*asn1 == '{') {
                int             i;
		
                asn1++;
                fConstructed = 1;
		
                i = 0;
                p = asn1;
                while ((p = strpbrk(p, "{}")) != NULL) {
                    if (*p == '}') {
                        if (i == 0) {
                            break;
                        }
                        i--;
                    } else {
                        i++;
                    }
                    p++;
                }
                if (p == NULL) {
                    Ns_Log(Error, "nsssl: unmatched parenthesis");
                    break;
                }
                *p = '\0';
                status = DEREncode(&dsBuf, asn1);
                *p = '}';
                if (status != NS_OK) {
                    break;
                }
                asn1 = p + 1;
                if (fSetOf) {
                    /*
		     * SET OF gets inlined as individual SETs.
		     */
                    RecodeAsSetOf(&dsBuf, ds);
                    Ns_DStringTrunc(&dsBuf, 0);
                }
            } else {
                if (class == 0) {
                    switch (tag) {
		    case 2:
		    case 3:
		    case 4:
                        while ((*asn1 != '\0') &&
			       isxdigit( (int)*asn1 ) &&
			       (asn1[1] != '\0') &&
			       isxdigit( (int)asn1[1] )) {
                            unsigned int    val;
			    
                            sscanf(asn1, "%02x", &val);
                            c = val;
                            Ns_DStringNAppend(&dsBuf, (char *) &c, 1);
                            asn1 += 2;
                        }
                        break;
		    case 19:
		    case 20:
		    case 22:
		    case 23:
                        p = strchr(asn1 + 1, '\"');
                        if ((*asn1 != '\"') || (p == NULL)) {
                            Ns_Log(Error, "nsssl: expected quoted string");
                            break;
                        }
                        if (p != (asn1+1)) {
                            char *s;
                            int i, j;
			    
                            s = asn1 + 1;
                            j = p - asn1 - 1;
			    
                            if (tag == 19) {
                                for (i=0; i<j; i++) {
                                    char c = s[i];
				    
                                    if (!(((c >= 'A') && (c <= 'Z')) ||
                                          ((c >= 'a') && (c <= 'z')) ||
                                          ((c >= '0') && (c <= '9')) ||
                                          (strchr(" '()+,-./:=?", c)!=NULL))) {
                                        Ns_Log(Error, "nsssl: "
					       "illegal char '%c'", c);
                                        status = NS_ERROR;
                                        break;
                                    }
                                }
                            }
			    
                            Ns_DStringNAppend(&dsBuf, s, j);
                        }
                        asn1 = p + 1;
                        break;
		    case 6:
                        p = strchr(asn1 + 1, ')');
                        if ((*asn1 != '(') || (p == NULL)) {
                            Ns_Log(Error, "nsssl: expected list of integers");
                            break;
                        } else {
                            unsigned int    val1, val2;
			    
                            asn1 = Ns_StrTrimLeft(asn1 + 1);
                            p = strpbrk(asn1, " )");
                            if (*p == ')') {
                                Ns_Log(Error, "nsssl: too few integers "
				       "for OBJECT IDENTIFIER");
                                break;
                            }
                            *p = '\0';
                            val1 = atoi(asn1);
                            *p = ' ';
			    
                            asn1 = Ns_StrTrimLeft(p + 1);
                            p = strpbrk(asn1, " )");
                            c = *p;
                            *p = '\0';
                            val2 = atoi(asn1);
                            *p = c;
			    
                            c = 40 * val1 + val2;
                            Ns_DStringNAppend(&dsBuf, (char *) &c, 1);
			    
                            asn1 = Ns_StrTrimLeft(p);
                            while (*asn1 != ')') {
                                unsigned char   buf[6];
                                int             i, l;
				
                                p = strpbrk(asn1, " )");
                                c = *p;
                                *p = '\0';
                                val1 = atoi(asn1);
				
                                l = (int) floor(log((double) val1)
						/ log(128.0));
				/*
				 * Note: ceil fails if val1 == 1.
				 */
                                l++;
                                i = l;
                                while (i) {
                                    buf[--i] = (val1 & 0x7f) | 0x80;
                                    val1 >>= 7;
                                }
                                buf[l - 1] ^= 0x80;
                                Ns_DStringNAppend(&dsBuf, (char *) buf, l);
				
                                *p = c;
                                asn1 = Ns_StrTrimLeft(p);
                            }
                            asn1++;
                        }
                        break;
		    default:
                        break;
                    }
		    
                    if (status == NS_ERROR) {
                        break;
                    }
                } else {
                    while ((*asn1 != '\0') &&
			   isxdigit( (int)*asn1 ) &&
			   (asn1[1] != '\0') &&
			   isxdigit( (int)asn1[1] )) {
                        unsigned int    val;
			
                        sscanf(asn1, "%02x", &val);
                        c = val;
                        Ns_DStringNAppend(&dsBuf, (char *) &c, 1);
                        asn1 += 2;
                    }
                }
		
                if (dsBuf.length == 0) {
                    break;
                }
            }
        }
        if (!fInsertRaw) {
            /*
	     * We only generate low tags.
	     */
            c = tag | (class << 6);
            if (fConstructed) {
                c |= 0x20;
            }
            Ns_DStringNAppend(ds, (char *) &c, 1);
	    
            AddLengthCount(ds, dsBuf.length);
        }
        if (dsBuf.length > 0) {
            Ns_DStringNAppend(ds, dsBuf.string, dsBuf.length);
            Ns_DStringTrunc(&dsBuf, 0);
        }
        asn1 = Ns_StrTrimLeft(asn1);
    }
    
    Ns_DStringFree(&dsBuf);
    
    if (*asn1 != '\0') {
        status = NS_ERROR;
    }
    return status;
}



/*
 * Private functions.
 */



/* 
 *---------------------------------------------------------------------- 
 * 
 * X509Initialize --
 * 
 *      Set up the ASCII decoder, its critical section, and register
 *      shutdown callback to clean up when the server shuts down.
 *
 * Results: 
 *      NS_OK
 * 
 * Side effects: 
 *      Global variable "nsX509Initialized" is set to NS_TRUE.
 *
 *----------------------------------------------------------------------
 */
static int
X509Initialize(void)
{
    if (nsX509Initialized != NS_TRUE) {
        int             status = NS_ERROR;

        do {
            if (B_CreateAlgorithmObject(&asciiDecoder) != 0) {
                break;
            }
            if (B_SetAlgorithmInfo(asciiDecoder, AI_RFC1113Recode, NULL_PTR)
                != 0) {
                break;
            }
            if (B_DecodeInit(asciiDecoder) != 0) {
                break;
            }
            nsX509Initialized = NS_TRUE;

            status = NS_OK;
        } while (0);

        if (status == NS_OK) {
            Ns_RegisterShutdown(X509Cleanup, NULL);
        } else {
            X509Cleanup(NULL);
        }

        return status;
    }
    return NS_OK;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * X509Cleanup --
 * 
 *      Burn and free BSAFE objects and critical section.
 *
 * Results: 
 *      None.
 * 
 * Side effects: 
 *      Global variable "nsX509Initialized" is set to NS_FALSE.
 *
 *----------------------------------------------------------------------
 */
static void
X509Cleanup(void *ignore)
{
    B_DestroyAlgorithmObject(&asciiDecoder);
    nsX509Initialized = NS_FALSE;
    Ns_CsDestroy(&nsAsciiDecoderCS);
}
