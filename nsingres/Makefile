#
# $Header: /Users/dossy/Desktop/cvs/aolserver/nsingres/Attic/Makefile,v 1.1.1.1 2004/09/13 15:22:07 dossy Exp $
#

ifndef NSHOME
    NSHOME    =  ../aolserver
endif

#
# Module name
#
MOD       =  nsingres.so

#
# Objects to build.
#
OBJS      =  nsingres.o

#
# Ingres should be installed in $II_SYSTEM.
#
ifndef II_SYSTEM

all:
	@echo "** "
	@echo "** II_SYSTEM variable not set."
	@echo "** nsingres will not be built."
	@echo "** "

install: all

clean:

else

#
# Header files in THIS directory
#
HDRS     =

#
# Extra libraries
#
MODLIBS  =  -L$(II_SYSTEM)/ingres/lib -linterp.1 -lframe.1 -lq.1 -lcompat.1

#
# Compiler flags
#
CFLAGS   = -I$(II_SYSTEM)/ingres/files


include  $(NSHOME)/include/Makefile.module

#
# Special rules for ESQL/C
#

.SUFFIXES: .ec

%.c: %.ec
	$(II_SYSTEM)/ingres/bin/esqlc -f$@ -multi -p $^

endif
