#
# nssock/nsssl --
#
#      Socket driver for HTTP and HTTPS connections.
#

NSHOME   =  ..


##################################################
#
# Make the nsssl (HTTPS) module.
#

#BSAFE=/usr/local/bsafe
#BSAFE_VERSION=-DBSAFE4


ifeq ($(SSL),1)

ifdef BSAFE

#
# Module name
#
#  nsssl.so   is the domestic 128-bit/1024-bit version.
#  nsssle.so  is the export 40-bit/512-bit version.
#
ifeq ($(SSL_DOMESTIC),1)
  MOD    =  nsssl.so
  CFLAGS =  -DSSL=1 $(BSAFE_VERSION) -I$(BSAFE)/include
else
  MOD    =  nsssle.so
  CFLAGS =  -DSSL_EXPORT=1 -DSSL=1 $(BSAFE_VERSION) -I$(BSAFE)/include
endif

#
# Objects to build.
#
OBJS      =  sock.o ssl.o ssltcl.o t_stdlib.o x509.o

#
# Header files
#
HDRS      =  ssl.h ssltcl.h x509.h

#
# Extra libraries
#
#  BSAFE can be obtained from http://www.rsasecurity.com/
#
MODLIBS   =  $(BSAFE)/lib/libbsafe.a

#
# Module directory (for installation)
#
MODDIR    =  nsssl


############################################################
#
# Special copy of ../include/Makefile.module appears below
#
#  Note:  This is atypical of most modules.  The standard
#         Makefile.module is usually all you will ever need.
#

include $(NSHOME)/include/Makefile.global

all: clean $(MOD)

# Override LIBS variable.
LIBS=

$(MOD): $(OBJS)
	$(RM) $(MOD)
	$(LDSO) -o $(MOD) $(OBJS) $(MODLIBS)

$(OBJS): $(HDRS)

install: all
	$(RM) $(INSTBIN)/$(MOD)
	$(CP) $(MOD) $(INSTBIN)
	$(MKDIR) $(INSTMOD)/$(MODDIR)
	test -f $(INSTMOD)/$(MODDIR)/certfile.pem \
		|| $(CP) certfile.pem $(INSTMOD)/$(MODDIR)
	test -f $(INSTMOD)/$(MODDIR)/keyfile.pem \
		|| $(CP) keyfile.pem $(INSTMOD)/$(MODDIR)
	$(CP) keygen.tcl $(INSTMOD)/$(MODDIR)
	$(MKDIR) $(INSTSRVMOD)/$(MODDIR)

clean: clobber
	$(RM) $(OBJS) $(MOD) so_locations

clobber:
	$(RM) *.so *.o *.a *~ so_locations

distclean: clobber
	$(RM) TAGS core

else
all:
	@echo "** "
	@echo "** BSAFE variable not set."
	@echo "** nsssl will not be built."
	@echo "** "
install:   all
clean:
	$(RM) *.so *.o *.a *~ TAGS core so_locations
clobber:   clean
distclean: clean
endif

else

##################################################
#
# Make the nssock (HTTP) module.
#

#
# Module name
#
MOD       =  nssock.so

#
# Objects to build.
#
OBJS      =  sock.o

all: objclean $(MOD)

objclean:
	$(RM) *.o

include $(NSHOME)/include/Makefile.module

endif

