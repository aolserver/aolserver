
ifndef BSAFE

all:
	@echo "** "
	@echo "** BSAFE variable not set."
	@echo "** nsssl will not be built."
	@echo "** "

install: all

clean:

else

NSHOME   =  ..

#
# Module name
#
ifeq ($(SSL_DOMESTIC),1)
  MOD    =  nsssl.so
  CFLAGS =  $(BSAFE_VERSION) -I$(BSAFE)/include
else
  MOD    =  nsssle.so
  CFLAGS =  -DSSL_EXPORT=1 $(BSAFE_VERSION) -I$(BSAFE)/include
endif
MODLIBS  =  -L$(BSAFE)/lib -lbsafe
MODDIR   =  nsssl
HDRS     =  ssl.h ssltcl.h x509.h


#
# Rainbow SSL accelerator card support.
#
#   Note: Requires position-independent version of libbswift.a.
#         Requires libswift.so to be somewhere in your system's library path.
#         Contact Rainbow at http://www.rainbow.com/ for the software.
#
ifdef SWIFT
  CFLAGS  +=-DHAVE_SWIFT=1 -I$(SWIFT)/include
  MODLIBS +=-L$(SWIFT)/lib -lbswift -lswift
endif


#
# Objects to build
#
OBJS      =  nsssl.o ssl.o ssltcl.o t_stdlib.o x509.o

include  $(NSHOME)/include/Makefile.module

install: all
	$(RM) $(INSTBIN)/$(MOD)
	$(CP) $(MOD) $(INSTBIN)
	$(MKDIR) $(INSTMOD)/nsssl
	$(CP) *.tcl $(INSTMOD)/nsssl
	$(MKDIR) $(INSTSRVMOD)/nsssl
	$(CP) *.pem $(INSTSRVMOD)/nsssl

endif
