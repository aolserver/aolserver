
NSHOME    =  ..

include $(NSHOME)/include/Makefile.global


OBJS      = compat.o cs.o rwlock.o reentrant.o \
            sema.o thread.o tls.o time.o memory.o debug.o \
            master.o pool.o error.o signal.o oldpools.o \
            mutex.o cond.o osthread.o tcl8x.o

TEST      = test
TEST_OBJS = test.o

CFLAGS    += -I./


all:      $(LIBNSTHREAD) $(TEST)

#
# Link the objects into a static library.
#
$(LIBNSTHREAD): $(OBJS)
	$(RM) $(LIBNSTHREAD)
	$(AR) $(ARFLAGS) $(LIBNSTHREAD) $(OBJS)
	$(RANLIB) $(LIBNSTHREAD)

$(TEST):  $(TEST_OBJS) $(LIBNSTHREAD)
	$(CC) $(LDFLAGS) -o $(TEST) $(TEST_OBJS) $(LIBS) $(LIBNSTHREAD) 

#
# Special dependency for the OS interfaces.
#
osthread.o: pthread.cpp sproc.cpp cthread.cpp

#
# Installs to the lib/ directory of the binary distribution.
#
install: all
	$(RM) $(INSTLIB)/libnsthread.a
	$(CP) libnsthread.a $(INSTLIB)

#
# Clean the directory to pre-build state.
#
clean:
	$(RM) $(LIBNSTHREAD) $(TEST) $(OBJS) $(TEST_OBJS) so_locations

clobber: clean
	$(RM) *.so *.o *.a *~

distclean: clobber
	$(RM) TAGS core
