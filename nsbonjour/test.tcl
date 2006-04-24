proc queryRecordCallback {flags errorCode interfaceIndex fullname rrtype rrclass rdlen rdata ttl} {
    puts "flags: $flags"
    puts "errorCode: $errorCode"
    puts "interfaceIndex: $interfaceIndex" 
    puts "fullname: $fullname" 
    puts "rrtype: $rrtype" 
    puts "rrclass: $rrclass" 
    puts "rdlen: $rdlen" 
    puts "rdata: $rdata" 
    puts "ttl: $ttl\n"
}

proc registerCallback {flags errorCode name regType domain} {
    puts "registerCallback:"
    puts "flags: $flags"
    puts "errorCode: $errorCode"
    puts "name: $name"
    puts "regType: $regType"
    puts "domain: $domain\n"
}

proc enumerateCallback {flags interfaceIndex errorCode replyDomain} {
    puts "enumerateCallback:"
    puts "flags: $flags"
    puts "interfaceIndex: $interfaceIndex"
    puts "errorCode: $errorCode"
    puts "replyDomain: $replyDomain\n"
}

proc browseCallback {flags interfaceIndex errorCode serviceName regType replyDomain} {
    puts "browseCallback:"
    puts "flags: $flags"
    puts "interfaceIndex: $interfaceIndex"
    puts "errorCode: $errorCode"
    puts "serviceName: $serviceName"
    puts "regType: $regType"
    puts "replyDomain: $replyDomain\n"
}

proc eventCallback {fp} {
    global ctx

    DNSServiceProcessResult $ctx(sdRef)
}

global ctx

#
# Service Register
#

#TXTRecordCreate txtRef
#TXTRecordSetValue $txtRef path "/test/path"
#set txtLen [TXTRecordGetLength $txtRef]
#set txtRef [TXTRecordGetBytesPtr $txtRef]
#DNSServiceRegister sdRef 0 0 "test name" _http._tcp "" "" 9000 $txtLen $txtRef registerCallback

#
# Service Enumeration
#

#DNSServiceEnumerateDomains sdRef 0x40 0 enumerateCallback

#
# Service Browse
#

DNSServiceBrowse sdRef 0 0 _http._tcp "" browseCallback

#
# Service Query Record
#

#DNSServiceQueryRecord sdRef 0 0 [DNSServiceConstructFullName "_services" "_dns-sd._udp" "local"] 12 1 queryRecordCallback

DNSServiceProcessResult $sdRef

set fp [DNSServiceRefSockFD $sdRef]

fconfigure $fp -buffering none
fconfigure $fp -blocking false

set ctx(sdRef) $sdRef

fileevent $fp readable [list eventCallback $fp]

vwait forever
