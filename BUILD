
Building AOLserver (short form)
-------------------------------

AOLserver is very portable and can compile and run on nearly all Unix
platforms that support multithreaded applications.  The file you're
reading is a "quick-start" cookbook.  See index.html for more detailed
information.


System Requirements
-------------------

A C compiler, such as gcc, and GNU Make are both required.  Tcl is
included with AOLserver so you do not have to acquire and built it
separately.


Compiler Selection
------------------

AOLserver builds with gcc on all platforms but Irix where special ABI
requirements require the native compiler.

If you wish to try building with the native compiler on your platform,
try:
gmake nativeme=1

Conversely, if you want to use gcc on a platform that normally uses
the native compiler, type:
gmake gccme=1

More native compilers and their options can be enabled by looking at
the platform's definitions in include/Makefile.global.  Read
index.html for more information.


Debugging with Purify
---------------------

The AOLserver build process can be easily instructed to enable Purify,
Quantify, and other runtime debugging and analysis tools.

For Purify and Quantify, just type:
gmake pureme=1 PURIFY=/path/to/purify/or/quantify


Note: All AOLserver builds are built with debugging symbols and no
optimization by default.  The AOLserver project values correctness
over performance in that respect.  While optimization has its
advantages, the debugging of an optimized server is prohibitively
difficult and is not generally recommended.


More Information
----------------

See index.html in this directory for more information.

