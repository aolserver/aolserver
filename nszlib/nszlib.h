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
 * nszlib.h --
 *
 *	Public header of AOLserver zlib extensions.
 *
 *	$Header: /Users/dossy/Desktop/cvs/aolserver/nszlib/nszlib.h,v 1.1 2006/04/19 18:04:59 jgdavidson Exp $
 *
 */

#ifndef NSZLIB_H
#define NSZLIB_H

#include "ns.h"
#include "zlib.h"

NS_EXTERN unsigned char *Ns_ZlibCompress(unsigned char *inbuf,
					 unsigned long inlen,
					 unsigned long *outlen);
NS_EXTERN unsigned char *Ns_ZlibUncompress(unsigned char *inbuf,
					   unsigned long inlen,
					   unsigned long *outlen);
#endif
