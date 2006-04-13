#!/bin/sh

#
# nsinstall-man.sh --
#
#	Install a man or HTML page, making links to all functions/commands
#	documented in the file.  Modified from the Tcl source.
#
# $Id: nsinstall-man.sh,v 1.3 2006/04/13 19:06:59 jgdavidson Exp $
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
if test "$#" -ne 2; then
    echo "Usage: $0 <options> file dir"
    exit 1
fi
MANPAGE=$1

# A sed script to parse the alternative names out of a man page.
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

SECTION=`echo $MANPAGE | $SED 's/.*\(.\)$/\1/'`
MACROS=`dirname $MANPAGE`/man.macros
if test -z "$HTM"
then
    DIR=$2/man$SECTION
    MAN2HTM=$CAT
else
    DIR=$2
    MAN2HTM="$GROFF -Thtml -man"
fi
FIRST=""
$MD $DIR
for name in $NAMES; do
    tail=$name.$SECTION
    file=$DIR/$tail$HTM
    $RM $file
    if test -z "$FIRST" ; then
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
