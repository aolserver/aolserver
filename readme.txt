
What is this?
------------

This module implements a simple AOLserver database services driver.  A
database driver is a module which interfaces between the AOLserver
database-independent nsdb module and the API of a particular DBMS.  A
database driver's job is to open connections, send SQL statements, and
translate the results into the form used by nsdb.  In this case, the
driver is for the PostgreSQL ORDBMS from The PostgreSQL Global Development
Group.  This is the official driver for the ACS-PG project. PostgreSQL can be
downloaded and installed on most Unix systems.  To use this driver, you
must have PostgreSQL installed on your system.  For more information on
PostgreSQL or to download the code, open:

        http://www.postgresql.org


How does it work?
----------------

Driver modules look much like ordinary AOLserver modules but are
loaded differently.  Instead of being listed with other modules in the
[ns\server\<server-name>\modules] configuration section, a database
driver is listed in the [ns\db\drivers] section and nsdb does
the loading.  The database driver initialization function normally does
little more than call the nsdb Ns_DbRegisterDriver() function with an
array of pointers to functions.  The functions are then later used by
nsdb to open database connections and send and process queries.  This
architecture is much like ODBC on Windows.  In addition to open,
select, and getrow functions, the driver also provides system catalog
functions and a function for initializing a virtual server.  The
virtual server initialization function is called each time nsdb is
loaded into a virtual server.  In this case, the server initialization
function, Ns_PgServerInit, adds the "ns_pg" Tcl command to the server's
Tcl interpreters which can be used to fetch information about an active
PostgreSQL connection in a Tcl script.

Contributors to this file include:

	Don Baccus		<dhogaza@pacifier.com>
	Lamar Owen		<lamar.owen@wgcr.org>
	Jan Wieck		<wieck@debis.com>
	Keith Pasket		(SDL/USU)
	Scott Cannon, Jr.	(SDL/USU)
        Dan Wickstrom           <danw@rtp.ericsson.se>

Original example driver by Jim Davidson

