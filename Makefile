# Copyright 1994-1996 America Online, Inc. 

#
# Makefile for nspostgres.so module.
#
# -DFOR_ACS_USE required for ACS/pg use!

# To compile the postgres.so loadable AOLserver module, edit the NSHOME
# variable (below) to the directory where the AOLserver files are located,
# edit the PGLIB and PGINC variables to the directories where the PostgreSQL
# libraries and includes are (the RPM installation is different from default), 
# and uncomment the CC, COPTS, and LDFLAGS variables for your platform and
# compiler.  Then type "make install" to compile and install the module
# in the AOLserver bin directory.  To have the AOLserver load the
# postgres.so module at runtime, you will need to add the following
# entry in the [ns\server\godzilla\modules] section of your nsd.ini file
# (where 'godzilla' is the name of a virtual server):
#
#	nspostgres=nspostgres.so
#
#
# NOTE:  This code is unsupported and for example purposes only.

INSTALL=/home/nsadmin

# Location of the PostgreSQL libraries
PGLIB=/usr/local/pgsql/lib

# Location of the PostgreSQL includes
PGINC=/usr/local/pgsql/include

# Location of the AOLserver files (normally the ~nsadmin directory):
#NSHOME=/opt/aolserver
# Alternate location for AOLserver
NSHOME=/home/aolserver/aolserver3_0

CC=gcc
COPTS=-Wall -fpic -shared -I/usr/local/pgsql/include -I/home/aolserver/include -I-/usr/include

# Debian Linux with deb AOLserver & PostgreSQL
#CC=gcc
#PGLIB=/usr/lib
#PGINC=/usr/include/postgresql
#NSINC=/usr/include/aolserver
#COPTS=-fpic -shared -I$(PGINC) -I$(NSINC) -I-/usr/include

# RedHat Linux with RPM PostgreSQL
CC=gcc
#PGLIB=/usr/lib
#PGINC=/usr/include/pgsql
COPTS=-fpic -shared -I$(PGINC) -I$(NSHOME)/include -I-/usr/include

# Solaris 2.4
#CC=/opt/SUNWspro/bin/cc
#COPTS=-g -mt -Xa
#LDFLAGS=-dy -G

# Alpha Digital Unix 3.2
#CC=cc
#COPTS=-g -D_REENTRANT -threads
#LDFLAGS=-shared -expect_unresolved '*'

# HP/UX
#CC=cc
#COPTS=-g -Ae -D_REENTRANT +z
#LDFLAGS=-b

# SGI Irix 5.3
#CC=cc
#COPTS=-g -D_SGI_MP_SOURCE
#LDFLAGS=-shared

# FreeBSD 3
# The make install target isn't what you want---just copy postgres.so to
# /usr/local/libexec/aolserver
#COPTS=-g -Wall -fpic -pthread -D_THREAD_SAFE -I/usr/local/include/aolserver -I/usr/local/pgsql/include
#LDFLAGS=-pthread -Wl,-E

# You should not need to edit anything below this line.

EXTRA_OBJS=$(PGLIB)/libpq.so
OBJS=nspostgres.o
MODULE=nspostgres.so
CFLAGS=-DFOR_ACS_USE -DBIND_EMULATION -I$(NSHOME)/include $(COPTS)
LDFLAGS=-shared -I$(PGINC) -I$(NSHOME)/include -I-/usr/include

all: $(MODULE)

$(MODULE): $(OBJS)
	gcc $(LDFLAGS) -o $(MODULE) $(OBJS) $(EXTRA_OBJS)

install: $(MODULE)
	cp $(MODULE) $(INSTALL)/bin/
	chmod +x $(INSTALL)/bin/$(MODULE)

clean:
	rm -f *.o *.so
