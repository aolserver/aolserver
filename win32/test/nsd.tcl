set home [file dirname [ns_info config]]
set bin [file dirname [info nameofexecutable]]
set server test
set sroot servers/$server
set db test

foreach d [list log servers $sroot $sroot/modules $sroot/pages] {
	if ![file exists $home/$d] {
		file mkdir $home/$d
	}
}

ns_section ns/parameters
ns_param home $home

ns_section ns/servers
ns_param $server $server

ns_section ns/server/$server
ns_param directoryfile index.adp
ns_param threadtimeout 5

ns_section ns/server/$server/modules
foreach m [list nssock nslog nscp nscgi] {
	ns_param $m $bin/$m.dll
}

ns_section ns/server/$server/adp
ns_param map /*.adp

ns_section ns/server/$server/module/nssock
ns_param port 8080

ns_section ns/server/$server/module/nscgi
foreach method {GET POST} {
	ns_param map "$method /*.bat"
	ns_param map "$method /cgi $bin"
}

ns_section ns/server/$server/module/nscp
ns_param address 127.0.0.1
ns_param port 9999

ns_section ns/server/$server/module/nscp/users
# password is "x"
ns_param user nsadmin:t2GqvvaiIUbF2:


# nsdb section for "test" odbc datasource
ns_section ns/db/drivers
ns_param odbc $bin/nsodbc.so

ns_section ns/db/pools
ns_param $db $db

ns_section ns/db/pool/$db
ns_param driver odbc
ns_param datasource $db

ns_section ns/server/$server/db
ns_param pools *
