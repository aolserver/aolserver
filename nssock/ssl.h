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


#ifndef SSL_H
#define SSL_H

#include "aglobal.h"
#include "bsafe.h"

/*
 * SSL version information.
 */
#define SSL_PROTOCOL_VERSION "2"
#define SSL_SERVER_VERSION    2


/*
 * BSAFE Algorithm chooser.
 */
extern B_ALGORITHM_METHOD *ALGORITHM_CHOOSER[];
extern B_ALGORITHM_METHOD *DIGEST_CHOOSER[];


/*
 * SSL message types.
 */
#define SSL_MT_ERROR                         0
#define SSL_MT_CLIENT_HELLO                  1
#define SSL_MT_CLIENT_MASTER_KEY             2
#define SSL_MT_CLIENT_FINISHED_V2            3
#define SSL_MT_SERVER_HELLO                  4
#define SSL_MT_SERVER_VERIFY                 5
#define SSL_MT_SERVER_FINISHED_V2            6
#define SSL_MT_REQUEST_CERTIFICATE           7
#define SSL_MT_CLIENT_CERTIFICATE            8
#define SSL_MT_CLIENT_DH_KEY                 9
#define SSL_MT_CLIENT_SESSION_KEY            10
#define SSL_MT_CLIENT_FINISHED               11
#define SSL_MT_SERVER_FINISHED               12


/*
 * SSL version 2 ciphers.
 */
#define SSL_CK_RC4_128_WITH_MD5              0x01010080
#define SSL_CK_RC4_128_EXPORT40_WITH_MD5     0x01020080
#define SSL_CK_RC2_128_CBC_WITH_MD5          0x01030080
#define SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5 0x01040080
#define SSL_CK_IDEA_128_CBC_WITH_MD5         0x01050080
#define SSL_CK_DES_64_CBC_WITH_MD5           0x01060040
#define SSL_CK_DES_192_EDE3_CBC_WITH_MD5     0x010700C0
#define SSL_CK_NULL_WITH_MD5                 0x01000000
#define SSL_CK_DES_64_CBC_WITH_SHA           0x01060140
#define SSL_CK_DES_192_EDE3_WITH_SHA         0x010701C0

#define NSEEDS 4
#define SSL_SESSION_ID_LENGTH  16


/*
 * SSL certificate types.
 */
#define SSL_CT_X509_CERTIFICATE  1
#define SSL_CT_PKCS7_CERTIFICATE 2


/*
 * SSL error messages.
 */
#define SSL_PE_NO_CIPHER                    0x0001
#define SSL_PE_NO_CERTIFICATE               0x0002
#define SSL_PE_BAD_CERTIFICATE              0x0004
#define SSL_PE_UNSUPPORTED_CERTIFICATE_TYPE 0x0006


/*
 * SSL authentication Type Codes.
 */
#define SSL_AT_MD5_WITH_RSA_ENCRYPTION      0x01


/*
 * SSL data length limits.
 *  Note:  SSL_MAX_RECORD_LENGTH_2_BYTE_HEADER may be set to 32767.
 */
#define SSL_MAX_RECORD_LENGTH_2_BYTE_HEADER 16383
#define SSL_MAX_RECORD_LENGTH_3_BYTE_HEADER 16383
#define SSL_MACSIZE                         16
#define SSL_MAXRECSIZE                      32767
#define SSL_MAXPADDING                      8


#ifdef WIN32
#define EOLSTRING "\r\n"
#else
#define EOLSTRING "\n"
#endif


/*
 * SSLRecord
 *
 * The data to be encrypted/decrypted.
 */
typedef struct {
    int             nRecordLength;
    int             fIsEscape;
    int             nPadding;
    unsigned char  *mac;
    unsigned char  *data;
    unsigned char   macBuf[SSL_MACSIZE];
    unsigned char   input[3 + SSL_MAXRECSIZE];
    unsigned char   output[3 + SSL_MAXRECSIZE];
} SSLRecord;


/*
 * SSLServer
 *
 * RSA data for cert and key exchange.
 */
typedef struct {
    B_KEY_OBJ       privateKey;
    unsigned char  *certificate;
    int             certificateLength;
} SSLServer;


/*
 * SSLConn
 *
 * SSL connection context.
 */
typedef struct {
    SOCKET          socket;
    int		    timeout;
    SSLServer      *ctx;
    SSLRecord       rec;
    unsigned        nReadSequence;
    unsigned        nWriteSequence;
    int             fEncryptionActive;
    B_ALGORITHM_OBJ digester;
    B_ALGORITHM_OBJ encryptor;
    B_ALGORITHM_OBJ decryptor;
    unsigned char   challenge[32];
    int             challengeLength;
    unsigned char   connId[SSL_SESSION_ID_LENGTH];
    unsigned char   sessionId[SSL_SESSION_ID_LENGTH];
    int             cipherKind;
    unsigned char   masterKey[1024];
    int             masterKeyLength;
    unsigned char   readKeyArgData[8];
    unsigned char   writeKeyArgData[8];
    int             keyArgLength;
    unsigned char   readKey[24];
    B_KEY_OBJ       readKeyObj;
    unsigned char   writeKey[24];
    B_KEY_OBJ       writeKeyObj;
    unsigned        ReadWriteKeyLength;
    unsigned int    blockSize;
    unsigned int    macSize;
    unsigned char  *incomingNext;
    unsigned char   incoming[SSL_MAXRECSIZE];
    int             incomingLength;
    unsigned char   outgoing[SSL_MAXRECSIZE];
    int             outgoingLength;
}               SSLConn;


extern int
NsSSLGenerateKeypair(unsigned int modulusBits,
		     ITEM * publicExponent,
		     B_KEY_OBJ * publicKey,
		     B_KEY_OBJ * privateKey);


extern int
NsSSLInitialize(char *server, char *module);

extern void *
NsSSLCreateServer(char *cert, char *key);

extern void
NsSSLDestroyServer(void *server);

extern void *
NsSSLCreateConn(SOCKET socket, int timeout, void *server);

extern void
NsSSLDestroyConn(void *conn);

extern int
NsSSLSend(void *conn, void *vbuf, int towrite);

extern int
NsSSLRecv(void *conn, void *vbuf, int toread);

extern int
NsSSLFlush(void *conn);

extern void *
SSLCreateServer(char *cert, char *key);

extern void
SSLDestroyServer(void *server);

extern void *
SSLCreateConn(SOCKET sock, int timeout, void *server);

extern void
SSLDestroyConn(void *conn);

extern int
SSLFlush(void *conn);

extern int
SSLRecv(void *conn, void *vbuf, int toread);

extern int
SSLSend(void *conn, void *vbuf, int tosend);


#endif
