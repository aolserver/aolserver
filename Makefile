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
#   nssock      -- serves HTTP
#   nsssl       -- serves HTTPS
#   nscgi       -- CGI module
#   nscp        -- Control port remote administration interface
#   nslog       -- Common log format module
#   nsperm      -- Permissions module
#   nsext       -- External database driver module
#   nspd        -- Archive library for building an external driver
#

MODULES   =  nssock nsssl nscgi nscp nslog nsperm nsext nspd 

ALLDIRS   = thread $(TCL_DIR) nsd $(MODULES) 

#
# Main build rule.
#
all:
	@for i in $(ALLDIRS); do \
		$(ECHO) "building \"$$i\""; \
		( cd $$i && $(MAKE) all ) || exit 1; \
	done

#
# Installation rule.
#
install:
	$(MKDIR)                    $(INSTBIN)
	$(MKDIR)                    $(INSTLOG)
	$(MKDIR)                    $(INSTTCL)
	$(MKDIR)                    $(INSTLIB)
	$(MKDIR)                    $(INSTLIB)/tcl8.3
	( cd $(TCL_DIR)/library && $(CP) -r . $(INSTLIB)/tcl8.3 || exit 1; )
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


#
# Test pages and scripts installation rule.
#
install-tests:
	$(CP) -r tests $(INSTSRVPAG)

#
# Cleaning rule.
#
clean: 
	@for i in $(ALLDIRS); do \
		$(ECHO) "cleaning \"$$i\""; \
		( cd $$i && $(MAKE) $@) || exit 1; \
	done

distclean: clean
	(cd $(TCL_DIR); make distclean)
