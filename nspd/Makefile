
NSHOME     =  ..

include $(NSHOME)/include/Makefile.global

#
# Specify the name of the library.
#
TARGET	   =  libnspd.a


#
# Objects to build
#

OBJS       =  listen.o log.o main.o
NSEXTMSG   =  $(NSHOME)/nsext/nsextmsg.o

#
# Link the objects into a library.
#

all: $(TARGET)

$(TARGET): $(OBJS) $(NSEXTMSG)
	$(AR) $(ARFLAGS) $(TARGET) $(OBJS) $(NSEXTMSG)


#
# Installation (empty rule)
#
#  Note: libnspd.a should be put in the AOLserver binary distribution here.
#
install: all
	$(RM) $(INSTLIB)/libnspd.a
	$(CP) libnspd.a $(INSTLIB)


#
# Clean the directory to pre-built state.
#
clean:
	$(RM) $(OBJS) $(TARGET) so_locations

clobber: clean
	$(RM) *.so *.o *.a *~

distclean: clobber
	$(RM) TAGS core
