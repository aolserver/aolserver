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
 * ssl.c --
 *
 *      Encryption and I/O routines for SSL v2.
 *
 */

static const char *RCSID = "@(#): $Header: /Users/dossy/Desktop/cvs/aolserver/nsssl/ssl.c,v 1.2 2001/04/24 17:10:07 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"
#include "ssl.h"
#include "ssltcl.h"
#include "x509.h"

#include <ctype.h>


/*
 * Constants and macros
 */

#define ATOU16(X) (((((unsigned char*)X)[0])<<8)|(((unsigned char*)X)[1]))
#define ATOU24(X) ((((unsigned char*)X)[0]<<16)|ATOU16(((unsigned char*)X)+1))


/*
 * BSAFE algorithm chooser
 *
 */

B_ALGORITHM_METHOD *ALGORITHM_CHOOSER[] = {
#ifndef SSL_EXPORT
    &AM_DES_CBC_DECRYPT,
    &AM_DES_CBC_ENCRYPT,
    &AM_DES_EDE3_CBC_DECRYPT,
    &AM_DES_EDE3_CBC_ENCRYPT,
#endif
    &AM_MD5,
    &AM_MD5_RANDOM,
    &AM_MD,
    &AM_RC2_CBC_DECRYPT,
    &AM_RC2_CBC_ENCRYPT,
    &AM_RC4_DECRYPT,
    &AM_RC4_ENCRYPT,
#ifdef HAVE_SWIFT
    &AM_SwiftRSA_CRT_DECRYPT,
    &AM_SwiftRSA_CRT_ENCRYPT,
#endif
    &AM_RSA_CRT_DECRYPT,
    &AM_RSA_CRT_ENCRYPT,
    &AM_RSA_KEY_GEN,
    (B_ALGORITHM_METHOD *) NULL
};

/*
 * BSAFE message digest chooser
 */
B_ALGORITHM_METHOD *DIGEST_CHOOSER[] = {
    &AM_MD5,
    (B_ALGORITHM_METHOD *) NULL
};

/*
 * ClientHello data structure
 *
 * Used for setting up the client's side of the connection.
 */
typedef struct {
    unsigned char   msg;
    unsigned char   clientVersion[2];
    unsigned char   cipherSpecsLength[2];
    unsigned char   sessionIdLength[2];
    unsigned char   challengeLength[2];
    unsigned char   data;
} ClientHello;

/*
 * ServerHello data structure
 *
 * Used for setting up the server's side of the connection.
 */

typedef struct {
    unsigned char   msg;
    unsigned char   sessionIdHit;
    unsigned char   certificateType;
    unsigned char   serverVersion[2];
    unsigned char   certificateLength[2];
    unsigned char   cipherSpecsLength[2];
    unsigned char   connectionIdLength[2];
    unsigned char   data;
} ServerHello;

/*
 * ClientMasterKey data structure
 *
 * Used to hold and protect the server's private RSA key.
 */

typedef struct {
    unsigned char   msg;
    unsigned char   cipherKind[3];
    unsigned char   clearKeyLength[2];
    unsigned char   encryptedKeyLength[2];
    unsigned char   keyArgLength[2];
    unsigned char   data;
} ClientMasterKey;

/*
 * surrenderCtx callback
 *
 * The random object has to be shared to keep it as well seeded as
 * possible.  However, some of the functions that call it take a long
 * time to run, such as key generation.  The surrenderCtx allows these
 * functions to let another function get some work done too.  We
 * register the function Surrender so that BSAFE yields the thread
 * occasionally.
 */

static A_SURRENDER_CTX surrenderCtx;
static Ns_Cs csRandom;
static B_ALGORITHM_OBJ randomObject = NULL;

/*
 * Static functions defined in this file.
 */

static int Recv(SSLConn *cPtr, void *vbuf, int toread);
static int Decrypt(SSLConn * con);
static int Encrypt(SSLConn * con);
static int RecvRecord(SSLConn * con);
static int SendRecord(SSLConn * con);
static int SetupDigester(SSLConn * con);
static int KeyMaterial(SSLConn * con, unsigned char *dest, char *num);
static int DetermineSessionKeys(SSLConn * con);
static int VerifyKeyArgs(SSLConn * con);
static int SetupEncryption(SSLConn * con);
static int EncryptInit(SSLConn * con, int fRead);
static void EncryptFinal(SSLConn * con, int fRead, unsigned char *data, int length);
static void DescribeError(unsigned char *errorcode);
static int Surrender(POINTER ignored);
static void RandomCleanup(void *ignored);
static int CheckForAlgorithm(int trialck);
static char *DescribeAlgorithm(int trialck);
static int GenerateRandomBytes(unsigned char *output, int outputLength);
static int NewSessionID(unsigned char *buf);
static void U32TOA(unsigned u, unsigned char *dest);
static void U24TOA(unsigned u, unsigned char *dest);
static void U24TOA(unsigned u, unsigned char *dest);
static void U16TOA(unsigned u, unsigned char *dest);

static unsigned char f4Data[3] = {0x01, 0x00, 0x01};


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLGenerateKeypair --
 * 
 *      Generates an RSA key pair of modulusBits size.  If
 *      publicExponent!=NULL it will use it instead of the normal F4
 *      prime.
 *
 *       input: &publicKey, &privateKey (uninitialized key objects)
 *
 *      Note: This is used in stand-alone mode (keygen.tcl) only.  See
 *      readme.txt for more information.
 *
 * Results: 
 *      A keypair is created and placed in publicKey, privateKey.
 * 
 * Side effects: 
 *      Memory is allocated to hold the publicKey, privateKey.
 *
 *----------------------------------------------------------------------
 */

int
NsSSLGenerateKeypair(unsigned int modulusBits,
		     ITEM *       publicExponent,
		     B_KEY_OBJ *  publicKey,
		     B_KEY_OBJ *  privateKey)
{
    int             status;
    B_ALGORITHM_OBJ keypairGenerator;
    int             err;
    
    status = NS_ERROR;
    keypairGenerator = (B_ALGORITHM_OBJ) NULL;
    err = 0;
    *publicKey = (B_KEY_OBJ) NULL;
    *privateKey = (B_KEY_OBJ) NULL;
    do {
        A_RSA_KEY_GEN_PARAMS params;
	
        if ((modulusBits < 256) || (modulusBits > 1024)) {
            Ns_Log(Error, "nsssl: invalid key size");
            break;
        }
        params.modulusBits = modulusBits;
	
        if (publicExponent != NULL) {
            memcpy(&params.publicExponent, publicExponent, sizeof(ITEM));
        } else {
            params.publicExponent.data = f4Data;
            params.publicExponent.len = 3;
        }
	
        err = B_CreateAlgorithmObject(&keypairGenerator);
        if (err != 0) {
            break;
        }
        err = B_SetAlgorithmInfo(keypairGenerator, AI_RSAKeyGen,
				 (POINTER) & params);
        if (err != 0) {
            break;
        }
        err = B_GenerateInit(keypairGenerator, ALGORITHM_CHOOSER,
			     (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
            break;
        }
        err = B_CreateKeyObject(publicKey);
        if (err != 0) {
            break;
        }
        err = B_CreateKeyObject(privateKey);
        if (err != 0) {
            break;
        }
	
        Ns_CsEnter(&csRandom);
        err = B_GenerateKeypair(keypairGenerator, *publicKey, *privateKey,
				randomObject, &surrenderCtx);
        Ns_CsLeave(&csRandom);
	
        if (err != 0) {
            break;
        }
        status = NS_OK;
    } while (0);
    
    if (err != 0) {
        Ns_Log(Error, "nsssl: bsafe error %d", err);
        B_DestroyKeyObject(publicKey);
        B_DestroyKeyObject(privateKey);
    }
    B_DestroyAlgorithmObject(&keypairGenerator);
    
    return status;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLInitialize --
 * 
 *     Set up SSL data structures at server startup.
 *     Random number is generated, shutdown handle is registered, Tcl
 *     commands (if any) are registered.
 *
 * Results: 
 *     NS_OK
 * 
 * Side effects: 
 *      See Ns_GenSeeds, RandomCleanup, and NSSSLInterpInit.
 *
 *      Variable "initialized" is global.
 *
 *----------------------------------------------------------------------
 */

int
NsSSLInitialize(char *server, char *module)
{
    unsigned long seeds[NSEEDS];
    static int initialized;
    
    if (!initialized) {
	Ns_GenSeeds(seeds, NSEEDS);
        if (B_CreateAlgorithmObject(&randomObject) != 0 ||
    	    B_SetAlgorithmInfo(randomObject, AI_MD5Random, NULL) != 0 ||
	    B_RandomInit(randomObject, ALGORITHM_CHOOSER, NULL) != 0 ||
    	    B_RandomUpdate(randomObject, (unsigned char *) seeds, 
                           NSEEDS * (sizeof(long)), NULL) != 0) {
    	    return NS_ERROR;
	}
        Ns_CsInit(&csRandom);
        surrenderCtx.Surrender = Surrender;
        surrenderCtx.handle = NULL;
	if (server != NULL) {
	    Ns_RegisterShutdown(RandomCleanup, NULL);
	} else {
	    Ns_Log(Notice, "nsssl: running in stand-alone mode");
	}
        initialized = 1;
    }
    if (server != NULL) {
	Ns_TclInitInterps(server, NsSSLInterpInit, NULL);
    }
    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLCreateServer --
 * 
 *      Sets up SSL data structures at server startup.
 *      The server's private key and certificate are loaded
 *
 * Results: 
 *      The SSLServer context (sPtr).
 * 
 * Side effects: 
 *      SSLServer context is created.
 *
 *----------------------------------------------------------------------
 */

void *
NsSSLCreateServer(char *cert, char *key)
{
    SSLServer *sPtr;
    
    sPtr = ns_calloc(1, sizeof(SSLServer));
    if (key != NULL &&
	GetPrivateKey(&sPtr->privateKey, key) != NS_OK) {
	
        Ns_Log(Error, "nsssl: "
	       "failed to fetch private key from file '%s'", key);
    	ns_free(sPtr);
	return NULL;
    }
    
    if (GetCertificate(&sPtr->certificate,
		       &sPtr->certificateLength, cert) != NS_OK) {
        Ns_Log(Error, "nsssl: "
	       "failed to fetch server certificate from file '%s'", cert);
	ns_free(sPtr);
	return NULL;
    }

    return (void *) sPtr;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLDestroyServer --
 * 
 *      Cleans up after the server and frees up the SSLServer
 *      context.  This is only called when AOLserver is shutting down.
 *
 * Results: 
 *      None.
 * 
 * Side effects: 
 *      SSLServer context is freed.
 *
 *----------------------------------------------------------------------
 */

void
NsSSLDestroyServer(void *server)
{
    SSLServer * ctx = server;

    if (ctx->privateKey != NULL) {
        B_DestroyKeyObject(&ctx->privateKey);
    }
    ns_free(ctx->certificate);
    ns_free(ctx);

}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLCreateConn --
 * 
 *      Create the ubiquitous conn structure for a new SSL connection.
 *      In order to create a conn for SSL, lots of work is done for
 *      public key exchange, private session key exchange, and random
 *      number generation.  Note that RSA is only used for the private
 *      session key exchange, after which a conventional cipher with
 *      a message digest (if requested) is used.
 *
 * Results: 
 *      Pointer to the conn.
 * 
 * Side effects: 
 *      The conn is created.
 *      Encryption objects are created and (hopefully) destroyed.
 *
 *----------------------------------------------------------------------
 */

void *
NsSSLCreateConn(SOCKET socket, int timeout, void *server)
{
    B_ALGORITHM_OBJ  rsaDecryptor;
    SSLRecord       *rec;
    int              err;
    SSLConn         *con;
    SSLServer       *ctx = server;
    

    con = ns_calloc(1, sizeof(SSLConn));
    con->timeout = timeout;
    con->socket = socket;
    con->ctx = ctx;
    
    err = 0;
    rsaDecryptor = NULL;
    rec = &con->rec;
    do {
        unsigned char   common_ciphers[3 * 25];
        int             num_common;
        ServerHello    *serverHello;
        int             i;
        unsigned char  *p;
	/*
	 * At the moment we don't do client certificates.
	 *  (I think that's why this variable is disabled.)
	 */
        /* unsigned char   certificateChallengeData[32]; */
	
        /*
	 * Get CLIENT-HELLO.
	 */
        if (RecvRecord(con) != NS_OK) {
            Ns_Log(Debug, "nsssl: "
		   "client dropped connection before CLIENT-HELLO");
	    /*
	     * This is not something to get concerned about.
	     */
            break;
	    
        } else {
            ClientHello    *clientHello = (ClientHello *) rec->data;
            int             iClientVersion;
            int             iCipherSpecsLength;
            int             iSessionIdLength;
            int             iChallengeLength;
            int             i;
	    
            iClientVersion = ATOU16(clientHello->clientVersion);
            iCipherSpecsLength = ATOU16(clientHello->cipherSpecsLength);
            iSessionIdLength = ATOU16(clientHello->sessionIdLength);
            iChallengeLength = ATOU16(clientHello->challengeLength);
	    
            if (clientHello->msg == SSL_MT_ERROR) {
                DescribeError((unsigned char *) (&clientHello->msg + 1));
                break;
            }
            if ((clientHello->msg != SSL_MT_CLIENT_HELLO) ||
                ((iCipherSpecsLength % 3) != 0) ||
                ((iSessionIdLength != 0) && (iSessionIdLength != 16)) ||
                (iChallengeLength < 16) || (iChallengeLength > 32)) {
		Ns_Log(Debug, "nsssl: client sent malformed CLIENT-HELLO");
		/*
		 * This is not something to get worried about.  If the
		 * browser is that messed up it will drop the
		 * connection, anyway.
		 */
                break;
            }

            num_common = 0;
            for (i = 0; i < iCipherSpecsLength; i += 3) {
                int ck = ATOU24(&clientHello->data + i) | 0x01000000;
		
                if ( CheckForAlgorithm(ck) == NS_OK) {
                    U24TOA(ck, &common_ciphers[num_common * 3]);
                    num_common++;
                }
            }
	    
            if (num_common == 0) {
                Ns_Log(Warning, "nsssl: "
		       "client and server failed to agree on cipher");
    		rec = &con->rec;
    		rec->input[0] = SSL_MT_ERROR;
    		U16TOA(SSL_PE_NO_CIPHER, &(rec->input)[1]);
    		rec->nRecordLength = 3;
    		rec->fIsEscape = 0;
    		(void) SendRecord(con);
                break;
            }
            con->challengeLength = iChallengeLength;
            memcpy(con->challenge,
		   &clientHello->data + iCipherSpecsLength + iSessionIdLength,
		   iChallengeLength);
        }
	
	/*
	 * Get a new session id (a random number).
	 */
        NewSessionID(con->connId);
	
        /*
	 * Make the SERVER-HELLO.
	 */
        serverHello = (ServerHello *) rec->input;
        serverHello->msg = SSL_MT_SERVER_HELLO;
        serverHello->sessionIdHit = 0;
        serverHello->certificateType = SSL_CT_X509_CERTIFICATE;
	
        U16TOA(SSL_SERVER_VERSION, serverHello->serverVersion);
        U16TOA(ctx->certificateLength, serverHello->certificateLength);
        i = num_common * 3;
	
        U16TOA(i, serverHello->cipherSpecsLength);
        U16TOA(SSL_SESSION_ID_LENGTH, serverHello->connectionIdLength);
        p = &serverHello->data;
	
        memcpy(p, ctx->certificate, ctx->certificateLength);
        p += ctx->certificateLength;
	
        memcpy(p, common_ciphers, i);
        p += i;
	
        memcpy(p, con->connId, SSL_SESSION_ID_LENGTH);
        rec->nRecordLength = (sizeof(ServerHello) - 1) +
            ctx->certificateLength + i + SSL_SESSION_ID_LENGTH;
        rec->fIsEscape = 0;
	
        /*
	 * Send the SERVER-HELLO.
	 */
        if (SendRecord(con) != NS_OK) {
	    Ns_Log(Debug, "nsssl: "
		   "client failed to receive SERVER-HELLO");
            break;
        }
	
        /*
	 * Get the CLIENT-MASTER-KEY.
	 */
        if (RecvRecord(con) != NS_OK) {
	    Ns_Log(Debug, "nsssl: "
		   "client failed to send CLIENT-MASTER-KEY");
            break;

        } else {
	    /*
	     * Decode CLIENT-MASTER-KEY.
	     */
            ClientMasterKey *clientMasterKey =
		(ClientMasterKey *) rec->data;
            int             iClearKeyLength;
            int             iEncryptedKeyLength;
            int             iKeyArgLength;
            unsigned int    outputLenUpdate;
            unsigned int    outputLenFinal;
            unsigned char  *out;
            unsigned int    outMax;
	    
            con->cipherKind = ATOU24(clientMasterKey->cipherKind)
                | 0x01000000;
            iClearKeyLength = ATOU16(clientMasterKey->clearKeyLength);
            iEncryptedKeyLength = ATOU16(clientMasterKey->encryptedKeyLength);
            iKeyArgLength = ATOU16(clientMasterKey->keyArgLength);
	    
            if (clientMasterKey->msg == SSL_MT_ERROR) {
                DescribeError(&clientMasterKey->msg + 1);
                break;
            }
	    
            if ( (clientMasterKey->msg != SSL_MT_CLIENT_MASTER_KEY) ||
		 (CheckForAlgorithm(con->cipherKind) != NS_OK) ) {
                Ns_Log(Debug, "nsssl: "
		       "client sent malformed CLIENT-MASTER-KEY");
                break;
            }
	    
            p = &(clientMasterKey->data);
            memcpy(con->masterKey, p, iClearKeyLength);
            p += iClearKeyLength;
	    
            /*
	     * Decrypt secret session key.  You know the BSAFE drill by now.
	     */
            err = B_CreateAlgorithmObject(&rsaDecryptor);
            if (err != 0) {
                break;
            }
            err = B_SetAlgorithmInfo(rsaDecryptor,
				     AI_PKCS_RSAPrivate, NULL);
            if (err != 0) {
                break;
            }
            err = B_DecryptInit(rsaDecryptor,
				ctx->privateKey,
				ALGORITHM_CHOOSER,
				(A_SURRENDER_CTX *) NULL);
            if (err != 0) {
		break;
	    }
	    
            out = (unsigned char *) (con->masterKey + iClearKeyLength);
            outMax = sizeof(con->masterKey) - iClearKeyLength;
            err = B_DecryptUpdate(rsaDecryptor,
				  out, &outputLenUpdate,
				  outMax, p,
				  iEncryptedKeyLength, NULL,
				  (A_SURRENDER_CTX *) NULL);
            if (err != 0) {
                break;
            }

            out += outputLenUpdate;
            outMax -= outputLenUpdate;
	    
            err = B_DecryptFinal(rsaDecryptor,
				 out, &outputLenFinal,
				 outMax, NULL,
				 (A_SURRENDER_CTX *) NULL);
            if (err != 0) {
		Ns_Log(Debug, "nsssl: "
		       "failed to decrypt secret session key");
                break;
            }
	    
            outputLenFinal += outputLenUpdate;

            con->masterKeyLength = iClearKeyLength + outputLenFinal;
            p += iEncryptedKeyLength;
	    
            /*
	     * Get the KEY-ARG-DATA if there is any.
	     */
            con->keyArgLength = iKeyArgLength;
            if (VerifyKeyArgs(con) != NS_OK) {
                break;
            } else if (iKeyArgLength > 0) {
                memcpy(con->readKeyArgData, p, iKeyArgLength);
                memcpy(con->writeKeyArgData, p, iKeyArgLength);
            }
            if ( (SetupDigester(con) != NS_OK) ||
		 (DetermineSessionKeys(con) != NS_OK) ||
		 (SetupEncryption(con) != NS_OK) ) {
                break;
            }
	    
            /*
	     * Figure out the cipher block size.
	     */
            switch (con->cipherKind) {
            case SSL_CK_RC4_128_WITH_MD5:
            case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
                con->blockSize = 0;
                break;
            case SSL_CK_RC2_128_CBC_WITH_MD5:
            case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
            case SSL_CK_DES_64_CBC_WITH_MD5:
            case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
                con->blockSize = 8;
                break;
            case SSL_CK_NULL_WITH_MD5:
            default:
                con->blockSize = 0;
            }
	    
            con->fEncryptionActive = 1;
        }

        /*
	 * Send the SERVER-VERIFY message.
	 */
        p = rec->input;
        *p++ = SSL_MT_SERVER_VERIFY;
        memcpy(p, con->challenge, con->challengeLength);
	
        rec->nRecordLength = con->challengeLength + 1;
        rec->fIsEscape = 0;
	
        if (SendRecord(con) != NS_OK) {
	    Ns_Log(Debug, "nsssl: "
		   "client did not receive SERVER-VERIFY");
	    break;
        }
	
        /*
	 * Get the CLIENT-FINISHED-V2 or CLIENT-FINISHED message.
	 */
        if (RecvRecord(con) != NS_OK) {
	    Ns_Log(Debug, "nsssl: "
		   "client did not receive CLIENT-FINISHED");
            break;
	    
        } else {
            p = rec->data;
            if (*p == SSL_MT_ERROR) {
                DescribeError(p + 1);
                break;
            }
            if ((*p != SSL_MT_CLIENT_FINISHED) &&
                (*p != SSL_MT_CLIENT_FINISHED_V2)) {
                Ns_Log(Debug, "nsssl: "
		       "client sent malformed CLIENT-FINISHED");
                break;
            }
	    
            /*
	     * We probably should verify that the CLIENT-FINISHED
	     * message is correct at this point but we don't because it
	     * doesn't really matter once the connection was closed.
	     */
        }
	
	/*
	 * Create a new session ID.
	 */
        NewSessionID(con->sessionId);
	
        /*
	 * Send the SERVER-FINISHED-V2 message.
	 */
        rec->input[0] = SSL_MT_SERVER_FINISHED_V2;
        memcpy(&rec->input[1], con->sessionId, SSL_SESSION_ID_LENGTH);
        rec->nRecordLength = SSL_SESSION_ID_LENGTH + 1;
        rec->fIsEscape = 0;

        if (SendRecord(con) != NS_OK) {
            Ns_Log(Debug, "nsssl: "
		   "client did not receive SERVER-FINISHED-V2");
            break;
        }
	
    } while (0);
    
    /*
     * Clean up the RSA object if it hasn't been cleaned up already.
     */
    if (rsaDecryptor != NULL) {
        B_DestroyAlgorithmObject(&rsaDecryptor);
    }
    
    if (err != 0) {
	Ns_Log(Error, "nsssl: "
	       "ssl connection failed, bsafe error %d", err);
	NsSSLDestroyConn(con);
	con = NULL;
    }
    
    return (void *) con;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLDestroyConn --
 * 
 *      Destroy the ubiquitous conn structure for the SSL connection.
 *      This code enters a critical section at the beginning to update
 *      the random number so it isn't used again in another session.
 *
 * Results: 
 *      None
 * 
 * Side effects: 
 *      The conn and all its data structures are (hopefully) freed.
 *      Encryption objects are created and (hopefully) destroyed.
 *
 *----------------------------------------------------------------------
 */

void
NsSSLDestroyConn(void *conn)
{
    SSLConn *cPtr = conn;

    /*
     * Make a new random number.
     */
    Ns_CsEnter(&csRandom);
    (void) B_RandomUpdate(randomObject,
			  (unsigned char *) cPtr,
			  sizeof(SSLConn),
			  &surrenderCtx);
    Ns_CsLeave(&csRandom);

    /*
     * Burn all the algorithm objects for the digester, encryptor, and
     * decryptor if they haven't been destroyed already.
     */
    if (cPtr->digester != NULL) {
        B_DestroyAlgorithmObject(&cPtr->digester);
    }
    if (cPtr->encryptor != NULL) {
        B_DestroyAlgorithmObject(&cPtr->encryptor);
    }
    if (cPtr->decryptor != NULL) {
        B_DestroyAlgorithmObject(&cPtr->decryptor);
    }

    /*
     * Burn the key objects so they can't be found in memory again if
     * something hasn't already done so.
     */
    if (cPtr->readKeyObj != NULL) {
        B_DestroyKeyObject(&cPtr->readKeyObj);
    }
    if (cPtr->writeKeyObj != NULL) {
        B_DestroyKeyObject(&cPtr->writeKeyObj);
    }

    ns_free(cPtr);

}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLSend --
 * 
 *      Send data out to the connection.
 *
 * Results: 
 *      The number of bytes sent.
 *
 *      Notice:  -1 is returned on error.
 * 
 * Side effects: 
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsSSLSend(void *conn, void *vbuf, int towrite)
{
    int             nwrote, ncopy;
    unsigned char  *buf = vbuf;
    SSLConn        *con = conn;

    nwrote = towrite;
    while (towrite) {

        ncopy = SSL_MAX_RECORD_LENGTH_2_BYTE_HEADER -
            con->outgoingLength - con->macSize;

        if (con->blockSize > 0) {
            /*
	     * Note: macSize % blockSize == 0
	     */
            ncopy -= (ncopy + con->outgoingLength) % con->blockSize;
        }

        if (towrite < ncopy) {
            memcpy(&(con->outgoing)[con->outgoingLength], buf, towrite);
            con->outgoingLength += towrite;
            towrite = 0;

        } else {
            memcpy(con->rec.input, con->outgoing, con->outgoingLength);
            memcpy(&(con->rec.input)[con->outgoingLength], buf, ncopy);
            con->rec.nRecordLength = con->outgoingLength + ncopy;
            con->rec.fIsEscape = 0;
            con->outgoingLength = 0;
            towrite -= ncopy;
            buf += ncopy;
            if (SendRecord(con) != NS_OK) {
		Ns_Log(Debug, "nsssl: "
		       "failed sending record to client");
		/*
		 * Note that we're returning -1 here.
		 */
	    	return -1;
	    }
        }
    }
  
    return nwrote;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLRecv --
 * 
 *      Read data from the connection.
 *
 * Results: 
 *      The number of bytes received.
 *
 *      Notice: -1 is returned on error.
 * 
 * Side effects: 
 *      vbuf contains the data read.
 *
 *----------------------------------------------------------------------
 */

int
NsSSLRecv(void *conn, void *vbuf, int toread)
{
    int             ncopy;
    SSLConn *cPtr = conn;
    unsigned char  *buf  = vbuf;

    if (cPtr->incomingLength == 0) {
        if ((NsSSLFlush(conn) != NS_OK) ||
	    (RecvRecord(cPtr) != NS_OK)) {
	    Ns_Log(Debug, "nsssl: "
		   "failed receiving record from client");
	    /*
	     * Note that we're returning -1 here.
	     */
	    return -1;
	}
        cPtr->incomingLength = cPtr->rec.nRecordLength;
        cPtr->incomingNext = cPtr->incoming;
        memcpy(cPtr->incoming, cPtr->rec.data, cPtr->incomingLength);
    }
    ncopy = cPtr->incomingLength;
    if (ncopy > toread) {
	ncopy = toread;
    }
    memcpy(buf, cPtr->incomingNext, ncopy);
    cPtr->incomingLength -= ncopy;
    cPtr->incomingNext += ncopy;
    return ncopy;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLFlush --
 * 
 *      Flush data waiting to be sent.
 *
 * Results: 
 *      NS_OK or NS_ERROR.
 * 
 * Side effects: 
 *
 *
 *----------------------------------------------------------------------
 */

int
NsSSLFlush(void *conn)
{
    int             towrite;
    SSLConn *cPtr = conn;
    
    while (cPtr->outgoingLength > 0) {
        towrite = cPtr->outgoingLength;
        if (cPtr->blockSize > 0 &&
	    ((towrite + cPtr->macSize) >
	     (SSL_MAX_RECORD_LENGTH_3_BYTE_HEADER - SSL_MAXPADDING))) {
            /*
	     * Note: macSize % blockSize == 0
	     */
            towrite -= towrite % cPtr->blockSize;
        }
	
        cPtr->rec.nRecordLength = towrite;
        cPtr->rec.fIsEscape = 0;
	
        memcpy(cPtr->rec.input, cPtr->outgoing, towrite);
	
        if (towrite != cPtr->outgoingLength) {
            memcpy(cPtr->outgoing, &(cPtr->outgoing)[towrite],
		   cPtr->outgoingLength - towrite);
        }
	
        cPtr->outgoingLength -= towrite;
	
        if (SendRecord(cPtr) != NS_OK) {
	    Ns_Log(Debug, "nsssl: failed flushing record to client");
	    return NS_ERROR;
	}
    }
    
    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * Recv -- 
 * 
 *      Reads data from the connection using Ns_SockRecv.
 * 
 * Results: 
 *      NS_OK
 * 
 * Side effects: 
 *      *vbuf contains the data.
 * 
 *---------------------------------------------------------------------- 
 */ 

static int
Recv(SSLConn *cPtr, void *vbuf, int toread)
{
    char       *buf = (char *) vbuf;
    int		tocopy;

    while (toread > 0) {
	if (cPtr->cnt > 0) {
	    if (toread < cPtr->cnt) {
		tocopy = toread;
	    } else {
		tocopy = cPtr->cnt;
	    }
	    memcpy(buf, cPtr->base, tocopy);
	    cPtr->base  += tocopy;
	    cPtr->cnt   -= tocopy;
	    buf		+= tocopy;
	    toread	-= tocopy;
	}
	if (toread > 0) {
	    cPtr->base = cPtr->buf;
    	    cPtr->cnt  = Ns_SockRecv(cPtr->socket, cPtr->base,
		sizeof(cPtr->buf), cPtr->timeout);
	    if (cPtr->cnt <= 0) {
		/* 
		 * This happens when a user drops the connection by
		 * hitting "stop" or rejects the server's master key
		 * and/or certificate.
		 */
		Ns_Log(Debug, "nsssl: client dropped connection");
		return NS_ERROR;
	    }
	}
    }
    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * Decrypt -- 
 * 
 *      Takes rec->nRecordLength bytes with rec->nPadding padding from
 *      rec->input * and decrypt it into rec->output.  It also checks
 *      the MAC and sets * rec->data to point to the actual data.
 *      rec->nRecordLength is adjusted, if * there was padding or a
 *      MAC.
 * 
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      rec->output contains the decrypted data.
 * 
 *----------------------------------------------------------------------
 */

static int
Decrypt(SSLConn * con)
{
    int             status;
    int             err;
    unsigned char  *out;
    unsigned int    outMax;
    unsigned int    length;
    unsigned int    updateLength;
    SSLRecord      *rec;
    
    if (EncryptInit(con, 1) != NS_OK) {
	Ns_Log(Error, "nsssl: encryptinit failed");
    	return NS_ERROR;
    }
    
    rec = &con->rec;
    status = NS_ERROR;
    err = 0;

    /*
     * Decryption step.
     */
    switch (con->cipherKind) {
	
    case SSL_CK_NULL_WITH_MD5:
	/*
	 * No encryption with just a message digest.
	 */
        rec->data = rec->input;
        status = NS_OK;
        break;
	
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
    case SSL_CK_DES_64_CBC_WITH_MD5:
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	/*
	 * Encryption with a message digest.
	 */
        out = rec->data = rec->output;
        outMax = SSL_MAXRECSIZE;
        err = B_DecryptUpdate(con->decryptor, out, &updateLength, outMax,
			      rec->input, rec->nRecordLength, NULL,
			      (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
	    break;
        }
        length = updateLength;
        if ((con->cipherKind != SSL_CK_RC4_128_WITH_MD5) &&
            (con->cipherKind != SSL_CK_RC4_128_EXPORT40_WITH_MD5)) {
            out += updateLength;
            outMax -= updateLength;
            err = B_DecryptFinal(con->decryptor, out, &updateLength, outMax,
				 NULL, (A_SURRENDER_CTX *) NULL);
            if (err != 0) {
                break;
            }
            length += updateLength;
            EncryptFinal(con, 1, rec->input, rec->nRecordLength);
        }
        rec->nRecordLength = length - rec->nPadding;
        status = NS_OK;
        break;
	
    default:
        Ns_Log(Warning, "nsssl: unsupported cipher");
    }
    
    if (err != 0) {
        Ns_Log(Error, "nsssl: failed to decrypt, bsafe error %d", err);
    }
    if (status != NS_OK) {
        return NS_ERROR;
    } else {
        status = NS_ERROR;
    }
    
    
    /*
     * Message digest step.
     */
    do {
        unsigned char   buf[4];
	
        rec->nRecordLength -= con->macSize;
        rec->mac = rec->data;
        rec->data += con->macSize;
	
        err = B_DigestUpdate(con->digester, con->readKey,
			     con->ReadWriteKeyLength,
			     (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
            break;
        }
	
        err = B_DigestUpdate(con->digester, rec->data,
			     rec->nRecordLength + rec->nPadding,
			     (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
            break;
        }
        U32TOA(con->nReadSequence, buf);
	
        err = B_DigestUpdate(con->digester, buf, 4,
			     (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
            break;
        }
        err = B_DigestFinal(con->digester, rec->macBuf, &updateLength, 16,
			    (A_SURRENDER_CTX *) NULL);
        if ((err != 0) || (updateLength != 16)) {
            break;
        }
	
        if (memcmp(rec->mac, rec->macBuf, con->macSize) != 0) {
            Ns_Log(Error, "nsssl: invalid message authentication code");
            break;
        }
        status = NS_OK;
	
    } while (0);
    
    if (err != 0) {
        Ns_Log(Error, "nsssl: decrypt failed, bsafe error %d", err);
    }
    
    return status;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * Encrypt -- 
 * 
 *      Makes a MAC for (rec->nRecordLength + rec->nPadding) bytes in
 *      rec->input.  Encrypts MAC + rec->input and stores the result
 *      at rec->data.
 * 
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      con->rec (in the SSLConn context) contains the
 *      encrypted data.
 * 
 *----------------------------------------------------------------------
 */

static int
Encrypt(SSLConn * con)
{
    int             status;
    int             err;
    unsigned char  *out;
    unsigned int    outMax;
    unsigned int    length;
    unsigned int    updateLength;
    SSLRecord      *rec;

    status = NS_ERROR;
    err = 0;
    
    rec = &con->rec;
    
    /*
     * Message digest step.
     *   Note: We must always do MD5 with encryption.
     */
    do {
        unsigned char   buf[4];
	
        err = B_DigestUpdate(con->digester, con->writeKey,
			     con->ReadWriteKeyLength,
			     (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
            break;
        }
	
        err = B_DigestUpdate(con->digester, rec->input,
			     rec->nRecordLength + rec->nPadding,
			     (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
            break;
        }
        U32TOA(con->nWriteSequence, buf);
	
        err = B_DigestUpdate(con->digester, buf, 4,
			     (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
            break;
        }
        err = B_DigestFinal(con->digester, rec->macBuf, &updateLength, 16,
			    (A_SURRENDER_CTX *) NULL);
        if ((err != 0) || (updateLength != 16)) {
	    break;
        }
	
        if (EncryptInit(con, 0) != NS_OK) {
            break;
        }
        status = NS_OK;
	
    } while (0);
    
    if (err != 0) {
        Ns_Log(Error, "nsssl: encrypt failed, bsafe error %d", err);
    }

    /*
     * Return immediately on error or re-initialize status and continue.
     */
    if (status != NS_OK) {
        return NS_ERROR;
    } else {
        status = NS_ERROR;
    }

    /*
     * Encryption step.
     */
    switch (con->cipherKind) {
    case SSL_CK_NULL_WITH_MD5:
	/*
	 * No encryption -- just do a message digest and copy the data.
	 */
        memcpy(rec->data, rec->macBuf, con->macSize);
        memcpy(rec->data + con->macSize, rec->input,
	       rec->nRecordLength);
        rec->nRecordLength += con->macSize;
        status = NS_OK;
        break;
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
    case SSL_CK_DES_64_CBC_WITH_MD5:
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	/*
	 * Encryption with a message digest.
	 */
        out = rec->data;
        outMax = SSL_MAXRECSIZE;
        err = B_EncryptUpdate(con->encryptor, out, &updateLength, outMax,
			      rec->macBuf, con->macSize, NULL,
			      (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
            break;
        }
	
        length = updateLength;
        out += updateLength;
        outMax -= updateLength;
        err = B_EncryptUpdate(con->encryptor, out, &updateLength, outMax,
			      rec->input, rec->nRecordLength +
			      rec->nPadding, NULL,
			      (A_SURRENDER_CTX *) NULL);
        if (err != 0) {
            break;
        }

        length += updateLength;
	
        if ((con->cipherKind != SSL_CK_RC4_128_WITH_MD5) &&
            (con->cipherKind != SSL_CK_RC4_128_EXPORT40_WITH_MD5)) {
            out += updateLength;
            outMax -= updateLength;
            err = B_EncryptFinal(con->encryptor, out, &updateLength, outMax,
				 NULL, (A_SURRENDER_CTX *) NULL);
            if (err != 0) {
                break;
            }
            length += updateLength;
            EncryptFinal(con, 0, rec->data, length);
        }

        rec->nRecordLength = length;
	
        status = NS_OK;
        break;
	
    default:
        Ns_Log(Warning, "nsssl: unsupported cipher");
    }
    
    if (err != 0) {
        Ns_Log(Error, "nsssl: encrypt failed, bsafe error %d", err);
    }

    return status;

}


/* 
 *---------------------------------------------------------------------- 
 * 
 * RecvRecord -- 
 * 
 *      Reads an ssl record from con->socket.  If appropriate it
 *      decrypts and checks the MAC.  It sets rec->data to the
 *      received data of rec->nRecordLength bytes.
 * 
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      con->rec (in the SSLConn context) contains the
 *      record received.
 *
 *----------------------------------------------------------------------
 */

static int
RecvRecord(SSLConn * con)
{
    int             toread;
    unsigned char   buf[3];
    unsigned char  *dest;
    SSLRecord      *rec;


    if (Recv(con, buf, 3) != NS_OK) {
    	return NS_ERROR;
    }
    
    rec = &con->rec;

    if (buf[0] & 0x80) {
        /*
	 * 2 byte length header
	 */
        rec->nRecordLength = ((buf[0] & 0x7f) << 8) | buf[1];
        rec->fIsEscape = 0;
        rec->nPadding = 0;
        rec->input[0] = buf[2];
        dest = &rec->input[1];
        toread = rec->nRecordLength - 1;

    } else {
        /*
	 * 3 byte length header
	 */
        rec->nRecordLength = ((buf[0] & 0x3f) << 8) | buf[1];
        rec->fIsEscape = (buf[0] & 0x40) != 0;
        rec->nPadding = buf[2];
        dest = (unsigned char *) rec->input;
        toread = rec->nRecordLength;
    }

    if (toread == 2) {
        /*
	 * Client cancelled due to server certificate rejection.
	 */
	Ns_Log(Debug, "nsssl: client rejected cert and dropped connection");
	return NS_ERROR;
    }

    if (Recv(con, dest, toread) != NS_OK) {
	Ns_Log(Debug, "nsssl: client dropped connection");
    	return NS_ERROR;
    }
    
    if (!con->fEncryptionActive) {
        rec->data = rec->input;
	
    } else if (Decrypt(con) != NS_OK) {
	return NS_ERROR;
    }

    con->nReadSequence++;

    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * SendRecord -- 
 * 
 *      Creates and writes an ssl record to con->socket.  If
 *      appropriate it makes a MAC and encrypts the MAC + data +
 *      padding.
 *
 *      input: rec->nRecordLength bytes in rec->input,
 *             rec->fIsEscape denotes an escape record (unused and untested).
 *
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      con->socket (in the SSLConn context) contains the ssl
 *      record created.
 * 
 *----------------------------------------------------------------------
 */

static int
SendRecord(SSLConn * con)
{
    SSLRecord      *rec = &con->rec;
    int             towrite, nwrote;
    int             fThreeByteHeader;
    char    	   *buf;


    /*
     * Calculate the padding.
     */
    if (con->blockSize != 0) {
        rec->nPadding = (rec->nRecordLength + con->macSize)
            % con->blockSize;
        if (rec->nPadding != 0) {
            rec->nPadding = con->blockSize - rec->nPadding;
        }
    } else {
        rec->nPadding = 0;
    }
    
    /*
     * Decide the header size.
     */
    if (!rec->fIsEscape && (rec->nPadding == 0)) {
        /*
	 * We can use the 2 byte record header.
	 */
        fThreeByteHeader = 0;
        rec->data = &rec->output[2];

    } else {
        /*
	 * We need the 3 byte record header.
	 */
        fThreeByteHeader = 1;
        rec->data = &rec->output[3];
    }

    if (!con->fEncryptionActive) {
        memcpy(rec->data, rec->input, rec->nRecordLength);

    } else if (Encrypt(con) != NS_OK) {
    	return NS_ERROR;
    }

    if (!fThreeByteHeader) {
        towrite = 2;
        U16TOA(rec->nRecordLength, rec->output);
        rec->output[0] |= 0x80;

    } else {
        /*
	 * 3 byte record header.
	 */
        towrite = 3;
        U16TOA(rec->nRecordLength, rec->output);
        rec->output[2] = rec->nPadding;
        if (rec->fIsEscape) {
            rec->output[0] |= 0x40;
        }
    }

    towrite += rec->nRecordLength;
    buf = (char *) rec->output;

    while (towrite > 0) {
    	nwrote = Ns_SockSend(con->socket, buf, towrite, con->timeout);
	if (nwrote < 0) {
	    return NS_ERROR;
	}
	towrite -= nwrote;
	buf += nwrote;
    }
    con->nWriteSequence++;
    
    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * SetupDigester --
 * 
 *      Sets up up the message digester based on con->cipherKind.
 *      Currently only MD5 is used.
 *
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      Algorithm object in con->digester is created and initialized
 *      by the BSAFE library.
 *
 *----------------------------------------------------------------------
 */

static int
SetupDigester(SSLConn * con)
{

    switch (con->cipherKind) {

    case SSL_CK_NULL_WITH_MD5:
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
    case SSL_CK_DES_64_CBC_WITH_MD5:
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	con->macSize = 16;
	if (B_CreateAlgorithmObject(&con->digester) != 0 ||
	    B_SetAlgorithmInfo(con->digester, AI_MD5, NULL) != 0 ||
	    B_DigestInit(con->digester, (B_KEY_OBJ) NULL,
			 DIGEST_CHOOSER, NULL) != 0) {
	    Ns_Log(Error, "nsssl: failed initializing digester");
	    return NS_ERROR;
	}
	break;
	
    default:
	/*
	 * No digester in use.
	 */
	con->macSize = 0;
    }
    
    return NS_OK;
    
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * KeyMaterial
 * 
 *      This generates the key material used to generate the read and
 *      write keys during RSA public key exchange.
 *
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      con->masterKey, con->masterKeyLength,
 *      con->challenge, and con->challengeLength are populated.
 *
 *----------------------------------------------------------------------
 */

static int
KeyMaterial(SSLConn * con, unsigned char *dest, char *num)
{
    unsigned int    outputLengthFinal;
    
    if (B_DigestUpdate(con->digester,
		       con->masterKey,
		       con->masterKeyLength, NULL) != 0) {
    	return NS_ERROR;
    }

    if (num != NULL &&
	B_DigestUpdate(con->digester, (unsigned char *)num,
		       strlen(num), NULL) != 0) {
	return NS_ERROR;
    }

    if (B_DigestUpdate(con->digester,
		       con->challenge,
		       con->challengeLength, NULL) != 0 ||
    	B_DigestUpdate(con->digester,
		       con->connId,
		       SSL_SESSION_ID_LENGTH, NULL) != 0 ||
    	B_DigestFinal(con->digester,
		      dest,
		      &outputLengthFinal,
		      16, NULL) != 0 ||
	outputLengthFinal != 16) {

	return NS_ERROR;
    }

    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * DetermineSessionKeys
 * 
 *      Determines the session keys according to con->cipherKind
 *
 *      input:
 *       con->masterKey, con->challenge, con->connId, con->cipherKind
 *
 *      output:
 *       con->readKey, con->writeKey, con->ReadWriteKeyLength
 *
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
DetermineSessionKeys(SSLConn * con)
{
    unsigned char   keyMaterial[16];

    switch (con->cipherKind) {
    case SSL_CK_NULL_WITH_MD5:
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
	/*
	 * With MD5 message digest.
	 */
	con->ReadWriteKeyLength = 16;
	/* WRITE KEY = MD5[ MASTER-KEY, "0", CHALLENGE, CONNECTION-ID ] */
	/* READ KEY = MD5[ MASTER-KEY, "1", CHALLENGE, CONNECTION-ID ] */
	if ( (KeyMaterial(con, con->writeKey, "0") != NS_OK) ||
	     (KeyMaterial(con, con->readKey, "1") != NS_OK) ) {
	    return NS_ERROR;
	}
	break;
	
    case SSL_CK_DES_64_CBC_WITH_MD5:
	con->ReadWriteKeyLength = 8;
	
	/*
	 * WRITE KEY = HASH[ MASTER-KEY, CHALLENGE, CONNECTION-ID ] [0-7]
	 */
	if (KeyMaterial(con, con->writeKey, "0") != NS_OK) {
	    return NS_ERROR;
	}

	/*
	 * WRITE KEY = HASH[ MASTER-KEY, CHALLENGE, CONNECTION-ID ] [8-15]
	 */
	memcpy(con->readKey, &con->writeKey[8], 8);
	break;
	
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	con->ReadWriteKeyLength = 24;
	/*
	 * 3 * 8 byte keys.
	 */
	if ( (KeyMaterial(con, con->writeKey, "0") != NS_OK) ||
	     (KeyMaterial(con, keyMaterial, "1") != NS_OK) ) {
	    return NS_ERROR;
	}
	memcpy(&con->writeKey[16], keyMaterial, 8);
	memcpy(con->readKey, &keyMaterial[8], 8);
	if (KeyMaterial(con, &con->readKey[8], "2") != NS_OK) {
	    return NS_ERROR;
	}
	break;
	
    default:
	/*
	 * Note: We do not support SHA message digests at this time,
	 *       so in those extremely rare cases when a browser is set up 
	 *       to refuse MD5 and use SHA, this error will appear in
	 *       addition to the standard error cases.  That kind of
	 *       situation would be extremely rare, since all browsers
	 *       are set up to accept MD5 and only crazies turn it off.
	 */
	Ns_Log(Error, "nsssl: unsupported message digest algorithm");
	
	return NS_ERROR;
    }

    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * VerifyKeyArgs
 * 
 *      Verifies the master key length and key arg length after the
 *      server receives the CLIENT-MASTER-KEY.
 *
 *      input: con->cipherKind, con->masterKeyLength, con->keyArgLength
 *
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
VerifyKeyArgs(SSLConn * con)
{
    int             masterKeyLength;
    int             keyArgLength;
    int             status;
    
    status = NS_ERROR;
    
    /*
     * This switch is for the masterKeyLength.
     *   Note: there are two switch statements.
     */
    switch (con->cipherKind) {
    case SSL_CK_NULL_WITH_MD5:
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
	masterKeyLength = 16;
	break;
	
    case SSL_CK_DES_64_CBC_WITH_MD5:
	masterKeyLength = 8;
	break;
    
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	masterKeyLength = 24;
	break;

    default:
	/*
	 * This is an error condition.
	 */
	masterKeyLength = -1;
    }
    
    /*
     * This switch is for the keyArgLength.
     */
    switch (con->cipherKind) {
    case SSL_CK_NULL_WITH_MD5:
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
	keyArgLength = 0;
	break;
	
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
    case SSL_CK_DES_64_CBC_WITH_MD5:
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	keyArgLength = 8;
	break;
	
    default:
	/*
	 * This is an error condition.
	 */
	keyArgLength = -1;
    }
    
    /*
     * This looks like a lame way to detect an error condition.
     */
    if ((con->masterKeyLength == masterKeyLength) &&
        (con->keyArgLength == keyArgLength)) {
	status = NS_OK;
    }
    
    return status;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * VerifyKeyArgs
 * 
 *      Sets up encryption algorithm objects and key objects.
 *
 *      input:
 *       con->cipherKind, con->readKey, con->writeKey,
 *       con->ReadWriteKeyLength
 *
 *      output:
 *       con->encryptor, con->decryptor, con->readKeyObj,
 *       con->writeKeyObj
 *
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *
 *
 *----------------------------------------------------------------------
 */

static int
SetupEncryption(SSLConn * con)
{
    ITEM            item;
    int     	    rc4;


    /*
     * This is the Bizarro-world way of choosing export/domestic RC4.
     *  (I believe it's because we have to use SetAlgorithmInfo for
     *   RC4 and nothing else).  If you're going to add SSL v3 to this
     *   you'll be using SetAlgorithmInfo a whole lot more.
     */
    rc4 = 0;
    switch (con->cipherKind) {
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
	rc4 = 1;
	if (B_CreateAlgorithmObject(&con->encryptor) != 0 ||
	    B_CreateAlgorithmObject(&con->decryptor) != 0) {
	    return NS_ERROR;
	}
	/* Fallthrough */
	
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
    case SSL_CK_DES_64_CBC_WITH_MD5:
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	if (B_CreateKeyObject(&con->readKeyObj) != 0 ||
	    B_CreateKeyObject(&con->writeKeyObj) != 0) {
	    return NS_ERROR;
	}
    }
    
    /*
     * Setup the RC4 algorithm.
     *  Note:  Other algorithms used in SSL v3 will need to use this, too.
     */
    if (rc4 &&
    	(B_SetAlgorithmInfo(con->encryptor, AI_RC4, NULL) != 0 ||
    	 B_SetAlgorithmInfo(con->decryptor, AI_RC4, NULL) != 0)) {
	return NS_ERROR;
    }
    
    /*
     * Initialize the key objects.
     */
    switch (con->cipherKind) {
    case SSL_CK_DES_64_CBC_WITH_MD5:
	if (B_SetKeyInfo(con->writeKeyObj, KI_DES8, con->writeKey) != 0 ||
	    B_SetKeyInfo(con->readKeyObj, KI_DES8, con->readKey) != 0) {
	    return NS_ERROR;
	}
	break;
	
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	item.data = con->writeKey;
	item.len = con->ReadWriteKeyLength;
	if (B_SetKeyInfo(con->writeKeyObj, KI_Item, (POINTER) &item) != 0) {
	    return NS_ERROR;
	}
	item.data = con->readKey;
	if (B_SetKeyInfo(con->readKeyObj, KI_Item, (POINTER) &item)) {
	    return NS_ERROR;
	}
    }
    
    /*
     * Extra setup for RC4.
     */
    if (rc4 &&
    	(B_EncryptInit(con->encryptor,
		       con->writeKeyObj, ALGORITHM_CHOOSER, NULL) != 0 ||
	 B_DecryptInit(con->decryptor,
		       con->readKeyObj, ALGORITHM_CHOOSER, NULL) != 0)) {
	return NS_ERROR;
    }
    
    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * EncryptInit --
 * 
 *      Sets up the encryption/decryption algorithm for another
 *      round of doing it's thing.
 *
 *      (Note the dual switch(con->cipherKind) statements.)
 *
 *    input:    fRead, con->cipherKind, { con->decryptor | con->encryptor }
 *              con->keyArgData, con->keyArgLength, con->ReadWriteKeyLength
 *              { con->readKeyObj | con->writeKeyObj }
 *    output:   { con->encryptor | con->decryptor }
 *
 * Results: 
 *      NS_OK or NS_ERROR
 * 
 * Side effects: 
 *
 *
 *----------------------------------------------------------------------
 */

static int
EncryptInit(SSLConn * con, int fRead)
{
    A_RC2_CBC_PARAMS   rc2Params;
    B_ALGORITHM_OBJ   *algObj;
    unsigned char     *keyArgData;
    int err;

    algObj = fRead ? &con->decryptor : &con->encryptor;

    switch (con->cipherKind) {
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
    case SSL_CK_DES_64_CBC_WITH_MD5:
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	if (B_CreateAlgorithmObject(algObj) != 0) {
	    return NS_ERROR;
	}
    }

    if (con->keyArgLength > 0) {
        keyArgData = fRead ? con->readKeyArgData : con->writeKeyArgData;
    }
    
    /*
     * Setup the algorithms.
     */
    switch (con->cipherKind) {
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
	rc2Params.effectiveKeyBits = con->ReadWriteKeyLength * 8;
	rc2Params.iv = keyArgData;
	if (B_SetAlgorithmInfo(*algObj,
			       AI_RC2_CBC,
			       (POINTER) &rc2Params) != 0) {
	    return NS_ERROR;
	}
	break;

    case SSL_CK_DES_64_CBC_WITH_MD5:
	if (B_SetAlgorithmInfo(*algObj,
			       AI_DES_CBC_IV8,
			       (POINTER) keyArgData) != 0) {
	    return NS_ERROR;
	}
	break;
	
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	if (B_SetAlgorithmInfo(*algObj,
			       AI_DES_EDE3_CBC_IV8,
			       (POINTER) keyArgData) != 0) {
	    return NS_ERROR;
	}
	/* Fallthrough */
	
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
	break;
	
    default:
	Ns_Log(Warning, "nsssl: unsupported cipher");
	return NS_ERROR;
    }

    /*
     * Still setting up the algorithms.
     */
    switch (con->cipherKind) {
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
    case SSL_CK_IDEA_128_CBC_WITH_MD5:
    case SSL_CK_DES_64_CBC_WITH_MD5:
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	if (!fRead) {
	    err = B_EncryptInit(*algObj,
				con->writeKeyObj,
				ALGORITHM_CHOOSER, NULL);
	} else {
	    err = B_DecryptInit(*algObj,
				con->readKeyObj,
				ALGORITHM_CHOOSER, NULL);
	}
	if (err != 0) {
	    return NS_ERROR;
	}
	/* Fallthrough */
	
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
	break;
	
    default:
	Ns_Log(Warning, "nsssl: unsupported cipher");
	return NS_ERROR;
    }
    
    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * EncryptFinal --
 * 
 *      Update the key arg data and destroy the encryption/decryption
 *      object.
 *
 *      Note: This is not called for RC4.
 *
 * Results: 
 *      None.
 * 
 * Side effects: 
 *
 *
 *----------------------------------------------------------------------
 */

static void
EncryptFinal(SSLConn * con, int fRead, unsigned char *data, int length)
{
    B_ALGORITHM_OBJ *algObj;
    
    if (con->keyArgLength > 0) {
        unsigned char  *dest;
	
        dest = fRead ? con->readKeyArgData : con->writeKeyArgData;
	
        memcpy(dest, &data[length - 8], con->keyArgLength);
    }

    algObj = fRead ? &con->decryptor : &con->encryptor;
    B_DestroyAlgorithmObject(algObj);
    *algObj = NULL;

}


/* 
 *---------------------------------------------------------------------- 
 * 
 * DescribeError --
 * 
 *      Translate error code into English.
 *
 *      Note: This is only used for the server log.
 *
 * Results: 
 *      None.
 * 
 * Side effects: 
 *
 *
 *----------------------------------------------------------------------
 */

static void
DescribeError(unsigned char *errorcode)
{
    unsigned        err;
    char           *msg;

    err = ATOU16(errorcode);
    switch (err) {
    case SSL_PE_NO_CIPHER:
        msg = "No Cipher";
        break;
    case SSL_PE_NO_CERTIFICATE:
        msg = "No Certificate";
        break;
    case SSL_PE_BAD_CERTIFICATE:
        msg = "Bad Certificate";
        break;
    case SSL_PE_UNSUPPORTED_CERTIFICATE_TYPE:
        msg = "Unsupported Certificate Type";
        break;
    default:
        msg = "Unknown Error";
    }

    Ns_Log(Debug, "nsssl: client sent this error: '%s'", msg);
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * Surrender -- 
 * 
 *      Cede control to allow other threads to work while we're still
 *      busy running the random number generator.
 *
 * Results: 
 *      None.
 * 
 * Side effects: 
 *
 *
 *----------------------------------------------------------------------
 */

static int
Surrender(POINTER ignored)
{
    Ns_CsLeave(&csRandom);
    Ns_ThreadYield();
    Ns_CsEnter(&csRandom);

    return NS_OK;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * RandomCleanup --
 * 
 *      Destroy algorithm object and the critical section used for
 *      the random number generator.
 *
 * Results: 
 *      None.
 * 
 * Side effects: 
 *
 *
 *----------------------------------------------------------------------
 */

static void
RandomCleanup(void *ignored)
{

    B_DestroyAlgorithmObject(&randomObject);
    randomObject = NULL;
    Ns_CsDestroy(&csRandom);
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * CheckForAlgorithm --
 * 
 *      Report on ciphers/digests supported.
 *
 * Results: 
 *      NS_OK if your cipher is supported.
 *      NS_ERROR if your cipher isn't.
 * 
 * Side effects: 
 *
 *
 *----------------------------------------------------------------------
 */

static int
CheckForAlgorithm(int trialck)
{
    switch (trialck) {
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
#ifndef SSL_EXPORT
    case SSL_CK_RC4_128_WITH_MD5:
    case SSL_CK_RC2_128_CBC_WITH_MD5:
    case SSL_CK_DES_64_CBC_WITH_MD5:
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
#endif
	return NS_OK;
    }
    return NS_ERROR;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * DescribeAlgorithm --
 * 
 *      Describe ciphers/digests supported.
 *
 * Results: 
 *      char pointer to the ASCII name of the cipher.
 * 
 * Side effects: 
 *      None.
 *
 *----------------------------------------------------------------------
 */

static char *
DescribeAlgorithm(int trialck)
{
    char *desc;
    
    switch (trialck) {
    case SSL_CK_RC4_128_EXPORT40_WITH_MD5:
	desc = "SSL_CK_RC4_128_EXPORT40_WITH_MD5";
	break;
    case SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5:
	desc = "SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5";
	break;
#ifndef SSL_EXPORT
    case SSL_CK_RC4_128_WITH_MD5:
	desc = "SSL_CK_RC4_128_WITH_MD5";
	break;
    case SSL_CK_RC2_128_CBC_WITH_MD5:
	desc = "SSL_CK_RC2_128_CBC_WITH_MD5";
	break;
    case SSL_CK_DES_64_CBC_WITH_MD5:
	desc = "SSL_CK_DES_64_CBC_WITH_MD5";
	break;
    case SSL_CK_DES_192_EDE3_CBC_WITH_MD5:
	desc = "SSL_CK_DES_192_EDE3_CBC_WITH_MD5";
	break;
#endif
    default:
	desc = "unknown cipher";
    }
    return desc;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * GenerateRandomBytes --
 * 
 *      Returns the output of the random number generator.
 *      Note the use of critical sections.  This function necessarily
 *      takes a little while to execute, so we define surrenderCtx to
 *      point to our Surrender function, above.
 *
 * Results: 
 *      NS_OK or NS_ERROR.
 * 
 * Side effects: 
 *      A random number is put into the *output variable.
 *
 *----------------------------------------------------------------------
 */

static int
GenerateRandomBytes(unsigned char *output, int outputLength)
{
    int             retval = NS_ERROR;
    int             err = -1;
    
    Ns_CsEnter(&csRandom);
    if ( (err = (B_GenerateRandomBytes(randomObject, output, outputLength,
				       &surrenderCtx))) != 0 ) {
	Ns_Log(Error, "nsssl: "
	       "failed to generate random bytes, bsafe error %d", err);
        retval = NS_ERROR;
    } else {
        retval = NS_OK;
    }
    Ns_CsLeave(&csRandom);
    
    return retval;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NewSessionID --
 * 
 *      Generate a session id using the random number generator.
 *      Note: This actually used for both conn ids and session ids.
 *
 * Results: 
 *      NS_OK or NS_ERROR from GenerateRandomBytes.
 * 
 * Side effects: 
 *      See GenerateRandomBytes.
 *
 *----------------------------------------------------------------------
 */

static int
NewSessionID(unsigned char *buf)
{
    return GenerateRandomBytes(buf, SSL_SESSION_ID_LENGTH);
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * U32TOA
 * 
 *      Shift to unsigned character (32-bit).
 *
 * Results: 
 *      dest points to the converted value.
 * 
 * Side effects: 
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
U32TOA(unsigned u, unsigned char *dest)
{
    dest[0] = (u & 0xFF000000) >> 24;
    dest[1] = (u & 0x00FF0000) >> 16;
    dest[2] = (u & 0x0000FF00) >> 8;
    dest[3] = u & 0x000000FF;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * U24TOA
 * 
 *      Shift to unsigned character (24-bit).
 *
 * Results: 
 *      dest points to the converted value.
 * 
 * Side effects: 
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
U24TOA(unsigned u, unsigned char *dest)
{
    dest[0] = (u & 0x00FF0000) >> 16;
    dest[1] = (u & 0x0000FF00) >> 8;
    dest[2] = u & 0x000000FF;
}



/* 
 *---------------------------------------------------------------------- 
 * 
 * U24TOA
 * 
 *      Shift to unsigned character (16-bit).
 *
 * Results: 
 *      dest points to the converted value.
 * 
 * Side effects: 
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
U16TOA(unsigned u, unsigned char *dest)
{
    dest[0] = (u & 0x0000FF00) >> 8;
    dest[1] = u & 0x000000FF;
}
