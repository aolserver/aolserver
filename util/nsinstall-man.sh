#!/bin/sh

#
# nsinstall-man.sh --
#
#	Install a man or HTML page, making links to all functions/commands
#	documented in the file.  Modified from the Tcl source.
#
# $Id: nsinstall-man.sh,v 1.4 2006/04/28 13:47:45 jgdavidson Exp $
#

ECHO=:
LN=ln
RM="rm -f"
MD="mkdir -p"
SED=sed
CAT=cat
CHMOD=chmod
MODE=0444
GROFF=groff
HTM=""

while true; do
    case $1 in
        -v | --verbose)  	  ECHO=echo;;
        -s | --symlinks)  	  LN="$LN -s ";;
	-h | --html)		  HTM=".htm";;
	*)  break ;;
    esac
    shift
done
if test "$#" -ne 1 -a "$#" -ne 2; then
    echo "Usage: $0 <options> file ?dir?"
    exit 1
fi
if test "$#" -eq 2; then
    DIR=$2
else
    DIR=$AOLSERVER
fi
if test -z "$DIR"; then
    echo "Must specify output directory or set AOLSERVER environment variable."
    exit 1
fi
MANPAGE=$1
SECTION=`echo $MANPAGE | $SED 's/.*\(.\)$/\1/'`

#
# Search for man.macros in common locations.
#

MACROS=""
for d in \
	`dirname $MANPAGE` \
	$AOLSERVER/include \
	`dirname $0`/../include \
	../doc \
	./aolserver/doc
do
	f="$d/man.macros"
	if test -r $f; then
		MACROS=$f
		break
	fi
done
if test -z "$MACROS"; then
    echo "Can not locate required man.macros"
    exit 1
fi

#
# Parse the alternative names out of a man page.
#
#    /^\\.SH NAME/{   ;# Look for a line, that starts with .SH NAME
#	s/^.*$//      ;# Delete the content of this line from the buffer
#	n             ;# Read next line
#	s/,//g        ;# Remove all commas ...
#	s/\\\ //g     ;# .. and backslash-escaped spaces.
#	s/ \\\-.*//   ;# Delete from \- to the end of line
#	p             ;# print the result
#	q             ;# exit
#   }
#
# Backslashes are trippled in the sed script, because it is in
# backticks which don't pass backslashes literally.
#
# Please keep the commented version above updated if you
# change anything to the script below.
NAMES=`$SED -n '
    /^\\.SH NAME/{
	s/^.*$//
	n
	s/,//g
	s/\\\ //g
	s/ \\\-.*//
	p
	q
    }' $MANPAGE`

#
# Create or link man pages for each name.
#

if test -z "$HTM"; then
    DIR=$DIR/man$SECTION
    MAN2HTM=$CAT
else
    DIR=$DIR
    MAN2HTM="$GROFF -Thtml -man"
fi

FIRST=""
$MD $DIR
for name in $NAMES
do
    tail=$name.$SECTION
    file=$DIR/$tail$HTM
    $RM $file
    if test -z "$FIRST"; then
	FIRST=$file
	$SED -e "/man\.macros/r $MACROS" -e "/man\.macros/d" $MANPAGE | \
		$MAN2HTM > $file
	$CHMOD $MODE $file
    	$ECHO "created: $file ($MODE)"
    else
	$LN $FIRST $file
    	$ECHO "linked: $file"
    fi
done
