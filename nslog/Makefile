
#
# Module name
#
MOD      =  nslog.so

#
# Objects to build
#
OBJS     =  nslog.o

#
# Installation directory for data files
#
MODDIR   =  nslog


############################################################
#
# Special copy of ../include/Makefile.module appears below
#
#  Note:  This is atypical of most modules.  The standard
#         Makefile.module is usually all you will ever need.
#

include ../include/Makefile.global

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
	$(MKDIR) $(INSTSRVMOD)/$(MODDIR)

clean:
	$(RM) $(OBJS) $(MOD) so_locations

clobber: clean
	$(RM) *.so *.o *.a *~

distclean: clobber
	$(RM) TAGS core
