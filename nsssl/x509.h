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


#ifndef X509_H
#define X509_H

#include <math.h>

extern int
GetPrivateKey(B_KEY_OBJ * key, char *filename);

extern int
GetCertificate(unsigned char **pCertificate, int *length, char *filename);

extern char *
PrivateKeyToPEM(B_KEY_OBJ privateKey);

extern unsigned char *
GetBerFromPEM(char *filename, char *section, int *length);

extern void
AddLengthCount(Ns_DString *ds, unsigned int length);

extern int
GetLengthCount(unsigned char *der, unsigned int *length);

extern int
SetOf(unsigned char *der, int length);

extern int
DecodeSetOf(Ns_DString *ds, unsigned char *der, int length, int indent);

void extern
RecodeAsSetOf(Ns_DString *dsSrc, Ns_DString *dsDest);

extern int
DERDecode(Ns_DString *ds, unsigned char *der, int length, int indent);

extern int
DEREncode(Ns_DString *ds, char *asn1);


#define SECTION_X509_CERTIFICATE "X509 CERTIFICATE"
#define SECTION_CERTIFICATE "CERTIFICATE"
#define SECTION_RSA_PRIVATE_KEY "RSA PRIVATE KEY"

#endif
