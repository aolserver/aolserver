#
# nscgi Makefile
#
#   This module installs several files into these directories:
#     modules/tcl/nsperm
#     servers/server1/modules/nsperm
#

NSHOME   =  ..

#
# Module name
#
MOD      =  nsperm.so

#
# Objects to build.
#
OBJS     =  nsperm.o

#
# Installation directory for data files
#
MODDIR   =  nsperm


############################################################
#
# Special copy of ../include/Makefile.module appears below
#
#  Note:  This is atypical of most modules.  The standard
#         Makefile.module is usually all you will ever need.
#

include $(NSHOME)/include/Makefile.global

all: $(MOD)

# Override LIBS variable
LIBS=

$(MOD): $(OBJS)
	$(RM) $(MOD)
	$(LDSO) -o $(MOD) $(OBJS) $(MODLIBS)

$(OBJS): $(HDRS)

install: all
	$(RM) $(INSTBIN)/$(MOD)
	$(CP) $(MOD) $(INSTBIN)
	$(MKDIR) $(INSTTCL)/$(MODDIR)
	$(MKDIR) $(INSTSRVMOD)/$(MODDIR)
	test -d $(INSTSRVMOD)/$(MODDIR)/passwd \
		|| $(CP) passwd group hosts.allow hosts.deny perms $(INSTSRVMOD)/$(MODDIR)
	$(CP) init.tcl compat.tcl $(INSTTCL)/$(MODDIR)


clean:
	$(RM) $(OBJS) $(MOD) so_locations

clobber: clean
	$(RM) *.so *.o *.a *~

distclean: clobber
	$(RM) TAGS core
