#
# Makefile --
#
#      Compile, link, and install AOLserver.
#


include ./include/Makefile.global

#
# AOLserver Dynamically-Loaded Modules
#
#  Choose the modules you want and put them in the MODULES variable below.
#  A typical web server might load nssock, nslog, and nsperm.
#
#   nssock      -- serving HTTP
#   nsssl2      -- serving HTTPS (requires BSAFE 3 library)
#   nscgi       -- CGI module
#   nscp        -- Control port remote administration interface
#   nslog       -- Common log format module
#   nsperm      -- Permissions module
#
#   nsunix      -- serving HTTP over Unix domain socket
#   nsvhr       -- Virtual hosting redirector
#
#   nsext       -- External database driver module
#   nspd        -- Archive library for building an external driver
#   nspostgres  -- Postgres driver (requires Postgres library)
#   nssybpd     -- Sybase driver (requires Sybase library and nspd)
#   nssolid     -- Solid driver (requires Solid library)
#

MODULES   = nssock nscgi nscp nslog nsperm nsext nspd nsftp
#MODULES  = nsssl2 nspostgres nssybpd nssolid nsunix nsvhr


#
# AOLserver main executable statically-links the thread and tcl libraries.
#
NSDDIRS   = thread tcl7.6 tcl8.3.0 nsd

ALLDIRS   = $(NSDDIRS) $(MODULES)

all:
	@for i in $(ALLDIRS); do \
		echo "building \"$$i\""; \
		( cd $$i && $(MAKE) all ) || exit 1; \
	done

#
# Installation to $(INST) directory
#
#  Note:  Dependencies are checked in the individual directories.
#
install:
	$(MKDIR) $(INSTBIN)
	$(MKDIR) $(INSTLOG)
	$(MKDIR) $(INSTTCL)
	$(MKDIR) $(INSTSRVMOD)
	$(MKDIR) $(INSTSRVPAG)
	$(CP) -r tcl $(INSTMOD)
	$(CP) scripts/translate-ini $(INSTBIN)
	$(CP) scripts/translate-tcl $(INSTBIN)
	test -f $(INST)/nsd.tcl           \
		|| $(CP) scripts/nsd.tcl $(INST)
	test -f $(INSTSRVPAG)/index.html  \
		|| $(CP) scripts/index.html $(INSTSRVPAG)
	@for i in $(ALLDIRS); do \
		echo "installing \"$$i\""; \
		( cd $$i && $(MAKE) install) || exit 1; \
	done


#
# Cleaning rules.
#
#    clean:      remove objects
#    clobber:    remove as much cruft as possible
#    distclean:  clean for non-CVS distribution
#
clean:
	@for i in $(ALLDIRS); do \
		echo "cleaning \"$$i\""; \
		( cd $$i && $(MAKE) $@) || exit 1; \
	done

clobber: clean
	$(RM) *~
	@for i in $(ALLDIRS); do \
		echo "clobbering \"$$i\""; \
		( cd $$i && $(MAKE) $@) || exit 1; \
	done

distclean: clobber
	$(RM) TAGS core
	@for i in $(ALLDIRS); do \
		echo "distcleaning \"$$i\""; \
		( cd $$i && $(MAKE) $@) || exit 1; \
	done
	$(FIND) . -name \*~ -exec $(RM) {} \;
	$(FIND) . -name CVS -exec $(RM) -r {} \; -prune
	$(FIND) . -name .cvsignore -exec $(RM) {} \;
	$(FIND) . -name TODO -exec $(RM) {} \;

