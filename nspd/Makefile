
include ../include/Makefile.global

#
# Specify the name of the library.
#
TARGET	   =  libnspd.a


#
# Objects to build
#

OBJS       =  listen.o log.o main.o
NSEXTMSG   =  ../nsext/nsextmsg.o

#
# Link the objects into a library.
#

all: $(TARGET)

$(TARGET): $(OBJS) $(NSEXTMSG)
	$(AR) $(ARFLAGS) $(TARGET) $(OBJS) $(NSEXTMSG)


#
# Installation (empty rule)
#
install:


#
# Clean the directory to pre-built state.
#
clean:
	$(RM) $(OBJS) $(TARGET) so_locations

clobber: clean
	$(RM) *.so *.o *.a *~

distclean: clobber
	$(RM) TAGS core
