# Compress Tcl string
set test "This is test string for compression"

set data [ns_zlib compress $test]

set test [ns_zlib uncompress $data]
ns_log Debug Uncompress: $test

# Compress the string into gzip format
set gzip [ns_zlib gzip $test]

# Save as gzip file
set fd [open /tmp/test.gz w]
fconfigure $fd -translation binary -encoding binary
puts -nonewline $fd $gzip
close $fd


# Uncompress gzipped file
set test [ns_zlib gunzip /tmp/test.gz]
ns_log Debug Ungzipped: $test

