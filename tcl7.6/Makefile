
#
# Tcl 7.6 Library Makefile
#

include ../include/Makefile.global


ifeq (gcc,$(CC))
    ENABLE_GCC=--enable-gcc
endif

TCLDIR   = $(shell pwd)/unix

all: $(TCLDIR)/Makefile
	(cd $(TCLDIR); $(MAKE) CFLAGS='$(CFLAGS)' $(TCL76_LIB))

$(TCLDIR)/Makefile: $(TCLDIR)/Makefile.in $(TCLDIR)/configure
	(cd $(TCLDIR); ./configure $(ENABLE_GCC))

clean:
	(cd $(TCLDIR); $(MAKE) -f Makefile.in distclean)

install: all
