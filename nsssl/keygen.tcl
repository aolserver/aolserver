#
# The contents of this file are subject to the AOLserver Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://aolserver.com/.
#
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
# the License for the specific language governing rights and limitations
# under the License.
#
# The Original Code is AOLserver Code and related documentation
# distributed by AOL.
# 
# The Initial Developer of the Original Code is America Online,
# Inc. Portions created by AOL are Copyright (C) 1999 America Online,
# Inc. All Rights Reserved.
#
# Alternatively, the contents of this file may be used under the terms
# of the GNU General Public License (the "GPL"), in which case the
# provisions of GPL are applicable instead of those above.  If you wish
# to allow use of your version of this file only under the terms of the
# GPL and not to allow others to use your version of this file under the
# License, indicate your decision by deleting the provisions above and
# replace them with the notice and other provisions required by the GPL.
# If you do not delete the provisions above, a recipient may use your
# version of this file under either the License or the GPL.
#

#
# EXPORT NOTICE 
# 
# This source code is subject to the U.S. Export Administration
# Regulations and other U.S. law, and may not be exported or
# re-exported to certain countries (currently Afghanistan
# (Taliban-controlled areas), Cuba, Iran, Iraq, Libya, North Korea,
# Serbia (except Kosovo), Sudan and Syria) or to persons or entities
# prohibited from receiving U.S. exports (including Denied Parties,
# Specially Designated Nationals, and entities on the Bureau of
# Export Administration Entity List).
#


#
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsssl/keygen.tcl,v 1.2 2002/02/08 07:56:16 hobbs Exp $
#

#
# keygen.tcl -- All-in-one SSL Key and Certificate Request generator
#
#      Enter your data in the appropriate fields below
#      and type:
#        ./bin/nsd -ft keygen.tcl
#
#      You'll get a private key, "keyfile.pem", and
#      a certificate request, "certreq.pem", to use with
#      this key to send to a certificate authority.
#
#      "keyfile.pem" is sensitive data!  Keep it safe!
#      "certreq.pem" should be sent to your certificate authority.
#
#      After running this program copy these files somewhere else so
#      they do not get over-written if you run this again.
#

#
# RSA encryption level (bits)
#
#  The lowest level is 256 bits, and the highest depends on the
#  version of nsssl you're using.  To comply with export restrictions,
#  nsssle.so is limited to 512 bits.  The levels are strictly enforced
#  in the nsssl.so module itself.  You'll need to rebuild nsssl with
#  "gmake SSL=1 SSL_DOMESTIC=1" to get the 128-bit/1024-bit domestic
#  version.
#

set module "./bin/nsssle.so" ;# 40-bit/512-bit export version.
#set module "./bin/nsssl.so"  ;# 128-bit/1024-bit domestic version.

set modulus       512              ;# nsssle.so is limited to 512 bits
#set modulus       1024             ;# nsssl.so goes up to 1024 bits

#
# Key and Certificate Request file names
#
set keyfile       "./keyfile.pem"  ;# Private key -- should be kept safe!!
set certreqfile   "./certreq.pem"  ;# Certificate -- you send this to your CA

#
# x.509 certificate information
#
#  This is entirely up to you.
#
set commonname  {www.nowhere.com}

set email       {nobody@nowhere.com}

set phone       {703-555-1212}

set org         {Nowhere, Inc.}

set orgunit     {Technology Division}

set locality    {Somewhere}

set state       {VA}

set country     {US}



############################################################
#
# You should not need to modify anything below this line.
#

# Initialize nsssl.
ns_log notice "keygen.tcl: initializing nsssl."

load $module

proc nsssl_keygen { \
			modulus \
			commonname email phone \
			org orgunit \
			locality state country \
			{keyfilename {./keyfile.pem}} \
			{certreqfile {./certreq.pem}} } {
    
    ns_log notice "keygen.tcl: generating ssl private key..."
    
    # Generate private key.
    set privatekey_octs  [ssl_rsa_key generate $modulus]
    
    # Convert to PEM format.
    set privatekey  [osi_pem put "RSA PRIVATE KEY" $privatekey_octs]
    set privatekey  [osi_octet_string fromstring   $privatekey]
    
    # Write to file.
    osi_octet_string write $keyfilename $privatekey
    
    ns_log notice "keygen.tcl: done generating ssl private key."

    ns_log notice "keygen.tcl: generating certificate request..."

    set publickey [ssl_rsa_key publickey $privatekey_octs]
    
    # Build the request string.
    set reqinfo "sequence \{
	integer 00
	sequence \{
	    set-of \{
		[osi_attribute put countryName $country]
		[osi_attribute put stateOrProvinceName $state]\n"
    
    if { $locality != "" } {
	append reqinfo "[osi_attribute put localityName $locality]\n"
    }
    
    append reqinfo "[osi_attribute put organizationName $org]
		[osi_attribute put organizationalUnitName $orgunit]
		[osi_attribute put commonName $commonname]
            \}
        \}
        [osi_der_decode -octets $publickey]
    \}"
    
    if {[ns_info winnt]} {
	set eol "\r\n"
    } else {
	set eol "\n"
    }

    set clearinfo \
	"Certificate Request generated by AOLserver [ns_info version]:$eol"

    append clearinfo "\tCommon Name: $commonname$eol"

    append clearinfo "\tOrganizational Unit: $orgunit$eol"

    append clearinfo "\tOrganizational Name: $org$eol"

    if { $locality != "" } {
	append clearinfo "\tLocality Name: $locality$eol"
    }

    append clearinfo "\tState Or Province: $state$eol"

    append clearinfo "\tCountry Name: $country$eol"

    set octs [osi_der_encode octets $reqinfo]

    set req [osi_sign_data $octs $privatekey_octs]

    set pem [osi_pem put "NEW CERTIFICATE REQUEST" $req]

    set pem "Webmaster: $email${eol}Phone: $phone${eol}$clearinfo$eol$pem"

    set octs [osi_octet_string fromstring $pem]

    if {[catch {
	osi_octet_string write $certreqfile $octs
    } info]} {
	ns_log Error \
	    "keygen.tcl: unable to write certificate request to $certreqfile."
    }
    
}


#
# Various tools for Open Systems Interconnection
#
proc ctoken {pstr separators} {
    upvar $pstr str
    set i 0
    set l [string length $str]
    while { $i < $l } {
        set c [string index $str $i]
        if {[string first $c $separators] >= 0} {
            if { $i == 0 } {
                return ""
            }
            set c [string range $str 0 [expr {$i-1}]]
            set str [string range $str $i end]
            return $c
        }
        incr i
    }
    set c $str
    set str ""
    return $c
}

proc strpbrk { str delim { pos 0 } } {
    if { $pos > 0 } {
	set str [string range $str $pos end]
    }

    set i 0
    set l [string length $str]
    set j -1
    while { $i < $l } {
	set c [string index $str $i]
	set j [string first $c $delim]
	if { $j != -1 } {
	    break
	}
	incr i
    }

    if { $j != -1 } {
	return [expr {$i+$pos}]
    } else {
	return -1
    }
}

proc osi_asn1_to_list str {
    set l {}
    set c 0
    set str [string trimleft $str]
    while {[string length $str] > 0} {
        set tag [string trim [ctoken str " \n\r\t"]]
        set str [string trimleft $str]

        if { [string match NULL $tag] || [string match ":*:" $tag] } {
            lappend l [list $tag]
        } else {
            set value [string trim [ctoken str " \n\r\t"]]
            if {[string match $value "\{"]} {
		set i 0
		set p 0
                while {[set p [strpbrk $str "\{\}" $p]] != -1 } {
		    if {[string index $str $p] == "\}"} {
			if { $i == 0 } {
			    break
			}
			set i [expr {$i-1}]
		    } else {
			incr i
		    }
		    incr p
		}
                if { $p == -1 } {
                    return -code error "unbalanced braces."
                }

                set value [osi_asn1_to_list \
			[string range $str 0 [expr {$p-1}]]]
                set str [string range $str [expr {$p+1}] end]
            } elseif {[string match $value "\("]} {
		set i [string first "\)" $str]
		if { $i == -1 } {
                    return -code error "unbalanced parenthesis."
		}

		set value "\([string range $str 0 $i]"
		incr i
		if {[string length $str] > $i} {
		    set str [string range $str [expr {$i+1}] end]
		} else {
		    set str ""
		}
	    }
            lappend l [list $tag $value]
        }
        incr c
        set str [string trim $str]
    }

    if { $c > 1 } {
        return $l
    } else {
        return [lindex $l 0]
    }
}

proc osi_pem { command sectionname arg { key 0 } } {
    set bboundary "-----Begin ${sectionname}-----"
    set eboundary "-----End ${sectionname}-----"
    set bboundaryU "-----BEGIN ${sectionname}-----"
    set eboundaryU "-----END ${sectionname}-----"

    if { $command == "get" } {
        set i [string first $bboundary $arg]
        if { $i == -1 } {
	    set i [string first $bboundaryU $arg]
	    if { $i == -1 } {
		return -code error "Couldn't find section."
	    }
        }
        set i [expr {$i+[string length $bboundary]}]
        set j [expr {[string first $eboundary $arg]-1}]
        if { $j <= $i } {
	    set j [expr {[string first $eboundaryU $arg]-1}]
	    if { $j <= $i } {
		return -code error "Couldn't find section."
	    }
        }

        set arg [string range $arg $i $j]

        set i [string first "DEK-Info:" $arg]
        if { $i != -1 } {
            return -code error "Encryption not implemented yet."
        }

        return [osi_octet_string frombase64 $arg]
    }

    if { $command == "put" } {
        set content [osi_octet_string tobase64 $arg]
	if {[ns_info winnt]} {
            set eol "\r\n"
        } else {
            set eol "\n"
        }

        if { $key != "0" } {
            return -code error "Encryption not implemented yet."
        }

        return "${bboundaryU}${eol}${content}${eboundaryU}${eol}"
    }

    return -code error "usage: osi_pem { get | put } arg"
}

proc osi_attribute { command arg1 { arg2 0 } } {
    if { $command == "putid" && $arg2 != "0" } {
	if {[osi_der_encode IsPrintableString $arg2]} {
	    set stype PrintableString
	} else {
	    set stype T61String
	}

	return "SEQUENCE \{
	OBJECT-IDENTIFIER \( 2 5 4 $arg1 \)
	$stype \"$arg2\"
    \}"
    } elseif { $command == "getid" && $arg2 == "0" } {
	set i [string first "\)" $arg1]
	if { $i <= 0 } {
	    return -code error "Could not find OBJECT IDENTIFIER."
	}
	set arg1 [string trimright [string range $arg1 0 [expr {$i-1}]]]
	set i [string last " " $arg1]
	if { $i == -1 } {
	    return -code error "Confused by input."
	}

	return [string range $arg1 [expr {$i+1}] end]
    } elseif { $command == "put" && $arg2 != "0" } {
	if { $arg1 == "commonName" } {
	    set i 3
	} elseif { $arg1 == "countryName" } {
	    set i 6
	} elseif { $arg1 == "localityName" } {
	    set i 7
	} elseif { $arg1 == "stateOrProvinceName" } {
	    set i 8
	} elseif { $arg1 == "organizationName" } {
	    set i 10
	} elseif { $arg1 == "organizationalUnitName" } {
	    set i 11
	} else {
	    return -code error "unknown attribute type $arg1."
	}

	return [osi_attribute putid $i $arg2]
    } else {
	return -code error "usage: osi_attribute: { getid | put | putid } $arg"
    }
}

proc osi_algorithm { command arg1 { arg2 0 } } {
    if { $command == "putid" && $arg2 != "0" } {
	return "SEQUENCE \{
        OBJECT-IDENTIFIER \( $arg1 \)
        $arg2
    \}"
    } elseif { $command == "put" && $arg2 == "0" } {
	set p "NULL"
	if { $arg1 == "md2withRSAEncryption" } {
	    set i "1 2 840 113549 1 1 2"
	} elseif { $arg1 == "md5withRSAEncryption" } {
	    set i "1 2 840 113549 1 1 4"
	} elseif { $arg1 == "rc4" } {
	    set i "1 2 840 113549 3 4"
	} elseif { $arg1 == "rsaEncryption" } {
	    set i "1 2 840 113549 1 1 1"
	} else {
	    return -code error "unknown algorithm type $arg1."
	}

	return [osi_algorithm putid $i $p]
    } else {
	return -code error "usage: osi_algorithm: { put $arg \
		| putid $id $arg }"
    }
}

proc osi_sign_data { data privatekey } {
    set sig [ssl_signature sign $data $privatekey]
    set a "sequence \{
    INSERT-$data
    [osi_algorithm put md5withRSAEncryption]
    $sig
    \}"
    return [osi_der_encode octets $a]
}

#
# Run the stuff the user wanted.
#
nsssl_keygen \
    $modulus \
    $commonname $email $phone \
    $org $orgunit \
    $locality $state $country \
    $keyfile $certreqfile
    

#
# Say goodbye.
#
ns_log notice \
    "keygen.tcl: send the '$certreqfile' file to your certificate authority."
ns_log notice \
    "keygen.tcl: WARNING! The '$keyfile' file is sensitive!  Keep it safe!"

#
# Terminate the nsd process.
#
ns_log notice "keygen.tcl: completed.  shutting down."
exit

