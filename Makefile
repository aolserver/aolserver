#
# Makefile --
#
#      Compile, link, and install AOLserver.
#

#
# Tell make where AOLserver source code lives.
#
NSHOME    =  $(shell pwd)
MAKEFLAGS += NSHOME=$(NSHOME)

include $(NSHOME)/include/Makefile.global

#
# AOLserver Dynamically-Loaded Modules
#
#  Choose the modules you want and put them in the MODULES variable below.
#  A typical web server might load nssock, nslog, and nsperm.
#
#   nssock      -- serves HTTP (nssock) and HTTPS (nsssl)
#   nscgi       -- CGI module
#   nscp        -- Control port remote administration interface
#   nslog       -- Common log format module
#   nsperm      -- Permissions module
#
#   nsunix      -- serve HTTP over Unix domain socket
#   nsvhr       -- Virtual hosting redirector
#
#   nsext       -- External database driver module
#   nspd        -- Archive library for building an external driver
#

MODULES   =  nssock nscgi nscp nslog nsperm nsext nspd nsunix nsvhr

#
# AOLserver main executable statically-links the thread and tcl libraries.
#

ALLDIRS   = nsd $(MODULES)

all: libtcl76 libtcl8x libnsthread
	@for i in $(ALLDIRS); do \
		$(ECHO) "building \"$$i\""; \
		( cd $$i && $(MAKE) all ) || exit 1; \
	done

#
# Installation to $(PREFIX) directory
#
#  Note:  Dependencies are checked in the individual directories.
#
install:
	$(MKDIR)                    $(INSTBIN)
	$(MKDIR)                    $(INSTLOG)
	$(MKDIR)                    $(INSTTCL)
	$(MKDIR)                    $(INSTLIB)
	$(MKDIR)                    $(INSTSRVMOD)
	$(MKDIR)                    $(INSTSRVPAG)
	$(CP) -r tcl                $(INSTMOD)
	$(CP) nsd/translate-ini     $(INSTBIN)
	$(CP) -r include            $(INSTINC)
	$(CP) nsd/sample-config.tcl $(INST)
	test -f $(INSTSRVPAG)/index.html \
		|| $(CP) doc/default-home.html $(INSTSRVPAG)/index.html
	@for i in $(ALLDIRS); do \
		$(ECHO) "installing \"$$i\""; \
		( cd $$i && $(MAKE) install) || exit 1; \
	done
	(cd thread && $(MAKE) install)

install-tests:
	$(CP) -r tests $(INSTSRVPAG)

#
# Cleaning
#
clean: libtcl8x-clean libtcl76-clean libnsthread-clean
	@for i in $(ALLDIRS); do \
		$(ECHO) "cleaning \"$$i\""; \
		( cd $$i && $(MAKE) $@) || exit 1; \
	done
