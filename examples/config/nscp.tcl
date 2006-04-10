#
# $Header: /Users/dossy/Desktop/cvs/aolserver/examples/config/nscp.tcl,v 1.2 2006/04/10 18:51:26 shmooved Exp $
# $Name:  $
#
# nscp.tcl --
#
#     AOLserver control port configuration example. 
#
# Results:     
#
#     A control port listening on port 8001 of localhost (127.0.0.1)
#     will be configured. The default user name and password are
#     left empty, so when prompted, simply hit enter. 
#
#     For security reasons, it is not recommended that you run a 
#     control port on any address other then localhost. You should  
#     also be sure to specify a user name and password for each user.
# 
# Defining Users and Passwords:
#
#     Users are specified using the following format:
#
#     <userName>:<encryptedPassword>
#
#     You can use the ns_crypt Tcl command to generate an encrypted
#     password. The ns_crypt command uses the same algorithm as the 
#     Unix crypt(3) command. You could also use passwords from the
#     /etc/passwd file.
#
#     The first two characters of the password are the salt - they can be 
#     anything since the salt is used to simply introduce disorder into
#     the encoding algorithm.
#
#     ns_crypt <key> <salt>
#     ns_crypt x t2
#
# Additional Configuration Options:
#
#     cpcmdlogging
#
#     Boolean (default: false). If enabled, all commands entered via the 
#     control port are logged to the server log. This can be useful for 
#     debugging and auditing purposes.
#

ns_section "ns/server/server1/modules"
    ns_param nscp nscp.so

ns_section "ns/server/server1/module/nscp"
    ns_param address "127.0.0.1"
    ns_param port 8001
    ns_param cpcmdlogging "false"
 
ns_section "ns/server/server1/module/nscp/users"
    ns_param user ":"
