#include "ns.h"
#include "dns_sd.h"
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <sys/select.h>
#include <sys/errno.h>

typedef struct BonjourContext {
    Tcl_Interp *interp;
    Tcl_Obj *scriptObjPtr;
} BonjourContext;

#define TXTBUFSZ 1300

NS_EXTERN int Nsbonjour_Init(Tcl_Interp *interp);

// Tcl command prototypes
static Tcl_ObjCmdProc DNSServiceConstructFullNameObjCmd;
static Tcl_ObjCmdProc DNSServiceQueryRecordObjCmd;
static Tcl_ObjCmdProc DNSServiceBrowseObjCmd;
static Tcl_ObjCmdProc DNSServiceRefSockFDObjCmd;
static Tcl_ObjCmdProc DNSServiceProcessResultObjCmd;
static Tcl_ObjCmdProc DNSServiceEnumerateDomainsObjCmd;
static Tcl_ObjCmdProc DNSServiceRegisterObjCmd;
static Tcl_ObjCmdProc TXTRecordCreateObjCmd;
static Tcl_ObjCmdProc TXTRecordGetBytesPtrObjCmd;
static Tcl_ObjCmdProc TXTRecordGetLengthObjCmd;
static Tcl_ObjCmdProc TXTRecordRemoveValueObjCmd;
static Tcl_ObjCmdProc TXTRecordSetValueObjCmd;

// DNSServiceRef Tcl_Obj Functions
static void BonjourDNSServiceRefUpdateStr(Tcl_Obj *objPtr);
static void BonjourDNSServiceRefFree(Tcl_Obj *objPtr);
static void DNSServiceRefToObj(DNSServiceRef sdRef, Tcl_Obj **objPtrPtr);
static int ObjToDNSServiceRef(Tcl_Interp *interp, Tcl_Obj *objPtr, DNSServiceRef **sdRefPtrPtr);
static int TclObjIsDNSServiceRef(Tcl_Obj *objPtr);

// TXTRecordRef Tcl_Obj Functions
static void BonjourTXTRecordRefUpdateStr(Tcl_Obj *objPtr);
static void BonjourTXTRecordRefFree(Tcl_Obj *objPtr);
static void TXTRecordRefToObj(TXTRecordRef *txtRefPtr, Tcl_Obj **objPtrPtr);
static int ObjToTXTRecordRef(Tcl_Interp *interp, Tcl_Obj *objPtr, TXTRecordRef **txtRefPtrPtr);
static int TclObjIsTXTRecordRefType(Tcl_Obj *objPtr);

// Function prototypes
static void DNSSD_API TclDNSServiceDomainEnumReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *replyDomain, void *context);
static void DNSSD_API TclDNSServiceRegisterReply(DNSServiceRef sdRef, DNSServiceFlags flags, DNSServiceErrorType errorCode, const char *name, const char *regType, const char *domain, void *context);
static void DNSSD_API TclDNSServiceBrowseReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName, const char *regType, const char *replyDomain, void *context);
static void TclDNSServiceQueryRecordReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *fullname, uint16_t rrtype, uint16_t rrclass, uint16_t rdlen, const void *rdata, uint32_t ttl, void *context);

static char *TclmDNSError(Tcl_Interp *interp, DNSServiceErrorType err);
static char *mDNSStrErr(DNSServiceErrorType err);

// Tcl_Obj types

Tcl_ObjType bonjourDNSServiceRefType = {
    "DNSServiceRef",
    BonjourDNSServiceRefFree,
    NULL,
    BonjourDNSServiceRefUpdateStr,
    NULL
};

Tcl_ObjType bonjourTXTRecordRefType = {
    "TXTRecordRef",
    BonjourTXTRecordRefFree,
    NULL,
    BonjourTXTRecordRefUpdateStr,
    NULL
};

static void
BonjourDNSServiceRefUpdateStr(Tcl_Obj *objPtr)
{
    DNSServiceRef *sdRefPtr;
    int len;
    char buf[1024];
    
    sdRefPtr = (DNSServiceRef *) objPtr->internalRep.otherValuePtr;
    snprintf(buf, 1023, "%p", sdRefPtr);
    len = strlen(buf);
    objPtr->bytes = ckalloc(len + 1); 
    strcpy(objPtr->bytes, buf);      
    objPtr->length = strlen(objPtr->bytes);
}

static void 
BonjourDNSServiceRefFree(Tcl_Obj *objPtr)
{
    DNSServiceRef *sdRefPtr = (DNSServiceRef *) objPtr->internalRep.otherValuePtr;
    DNSServiceRefDeallocate((DNSServiceRef) sdRefPtr);
}

static void
DNSServiceRefToObj(DNSServiceRef sdRef, Tcl_Obj **objPtrPtr)
{   
    *objPtrPtr = Tcl_NewObj();
    (*objPtrPtr)->internalRep.otherValuePtr = (void *) sdRef;
    (*objPtrPtr)->typePtr = &bonjourDNSServiceRefType;
    Tcl_InvalidateStringRep(*objPtrPtr);
}

static int
ObjToDNSServiceRef(Tcl_Interp *interp, Tcl_Obj *objPtr, DNSServiceRef **sdRefPtrPtr)
{
    *sdRefPtrPtr = NULL;

    if (!TclObjIsDNSServiceRef(objPtr)) {
        Tcl_AppendResult(interp, "expected bonjourDNSServiceRefType but got: ",
                        Tcl_GetString(objPtr), NULL);
        return TCL_ERROR;
    }
    *sdRefPtrPtr = (DNSServiceRef *) objPtr->internalRep.otherValuePtr;
    return TCL_OK;
}

static int
TclObjIsDNSServiceRef(Tcl_Obj *objPtr)
{
    return (objPtr->typePtr == &bonjourDNSServiceRefType);
}


// TXTRecordRef

static void
BonjourTXTRecordRefUpdateStr(Tcl_Obj *objPtr)
{
    TXTRecordRef *txtRefPtr;
    int len;
    char buf[24];
    
    txtRefPtr = (TXTRecordRef *) objPtr->internalRep.otherValuePtr;
    snprintf(buf, 23, "%p", txtRefPtr);
    len = strlen(buf);
    objPtr->bytes = ckalloc(len + 1); 
    strcpy(objPtr->bytes, buf);      
    objPtr->length = strlen(objPtr->bytes);
}

static void 
BonjourTXTRecordRefFree(Tcl_Obj *objPtr)
{
    TXTRecordRef *txtRefPtr = (TXTRecordRef *) objPtr->internalRep.otherValuePtr;
    char *buffer;
    
    buffer = (char *) TXTRecordGetBytesPtr(txtRefPtr);
    Tcl_Free(buffer);
    TXTRecordDeallocate(txtRefPtr);
    Tcl_Free((char *) txtRefPtr);
}

static void
TXTRecordRefToObj(TXTRecordRef *txtRefPtr, Tcl_Obj **objPtrPtr)
{
    *objPtrPtr = Tcl_NewObj();
    (*objPtrPtr)->internalRep.otherValuePtr = (void *) txtRefPtr;
    (*objPtrPtr)->typePtr = &bonjourTXTRecordRefType;
    Tcl_InvalidateStringRep(*objPtrPtr);
}

static int
ObjToTXTRecordRef(Tcl_Interp *interp, Tcl_Obj *objPtr, TXTRecordRef **txtRefPtrPtr)
{
    *txtRefPtrPtr = NULL;

    if (!TclObjIsTXTRecordRefType(objPtr)) {
        Tcl_AppendResult(interp, "expected bonjourTXTRecordRefType but got: ",
                        Tcl_GetString(objPtr), NULL);
        return TCL_ERROR;
    }
    *txtRefPtrPtr = (TXTRecordRef *) objPtr->internalRep.otherValuePtr;
    return TCL_OK;
}

static int
TclObjIsTXTRecordRefType(Tcl_Obj *objPtr)
{
    return (objPtr->typePtr == &bonjourTXTRecordRefType);
}

// Init

static Ns_TclTraceProc InitInterp;

int
NsBonjourModInit(char *server, char *module)
{
    Ns_TclRegisterTrace(server, InitInterp, NULL, NS_TCL_TRACE_CREATE);
    return NS_OK;
}

static int
InitInterp(Tcl_Interp *interp, void *arg)
{
    return Nsbonjour_Init(interp);
}

int
Nsbonjour_Init(Tcl_Interp *interp)
{
    Tcl_RegisterObjType(&bonjourDNSServiceRefType);

    // Querying Records
    Tcl_CreateObjCommand(interp, "DNSServiceQueryRecord", 
                        DNSServiceQueryRecordObjCmd, NULL, NULL);
						
    // Browsing
    Tcl_CreateObjCommand(interp, "DNSServiceBrowse", 
                        DNSServiceBrowseObjCmd, NULL, NULL);
    
    // Accessing Sockets
    Tcl_CreateObjCommand(interp, "DNSServiceRefSockFD", 
                        DNSServiceRefSockFDObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "DNSServiceProcessResult", 
                        DNSServiceProcessResultObjCmd, NULL, NULL);

    // Enumerating Domains
    Tcl_CreateObjCommand(interp, "DNSServiceEnumerateDomains", 
                        DNSServiceEnumerateDomainsObjCmd, NULL, NULL);

    // Registering Services
    Tcl_CreateObjCommand(interp, "DNSServiceRegister", 
                        DNSServiceRegisterObjCmd, NULL, NULL);

    // Constructing TXT Records
    Tcl_CreateObjCommand(interp, "TXTRecordCreate", 
                        TXTRecordCreateObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "TXTRecordGetBytesPtr", 
                        TXTRecordGetBytesPtrObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "TXTRecordGetLength", 
                        TXTRecordGetLengthObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "TXTRecordRemoveValue", 
                        TXTRecordRemoveValueObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "TXTRecordSetValue", 
                        TXTRecordSetValueObjCmd, NULL, NULL);
						
    // Miscellaneous
    Tcl_CreateObjCommand(interp, "DNSServiceConstructFullName",
                        DNSServiceConstructFullNameObjCmd, NULL, NULL);

    return TCL_OK;
}

int
Bonjour_SafeInit(Tcl_Interp *interp)
{
    return Nsbonjour_Init(interp);
}

static int
DNSServiceConstructFullNameObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Tcl_Obj *objPtr;
    char *service, *regtype, *domain;
    char fullName[kDNSServiceMaxDomainName];
    DNSServiceErrorType err;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "service regtype domain");
        return TCL_ERROR;
    }

    service = Tcl_GetStringFromObj(objv[1], NULL);
    regtype = Tcl_GetStringFromObj(objv[2], NULL);
    domain = Tcl_GetStringFromObj(objv[3], NULL);
	
    err = DNSServiceConstructFullName(fullName, (char *)service, (char *)regtype, (char *)domain);

    if (err != kDNSServiceErr_NoError) {
        Tcl_SetResult(interp, TclmDNSError(interp, err), TCL_STATIC);
        return TCL_ERROR;
    }
	
    objPtr = Tcl_NewStringObj((char *)fullName, strlen(fullName));
    Tcl_SetObjResult(interp, objPtr);

    return TCL_OK;
}

static int
DNSServiceRefSockFDObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int fd = -1;
    Tcl_Channel chan = NULL;
    DNSServiceRef *sdRef = NULL;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "sdRef");
        return TCL_ERROR;
    }
    if (ObjToDNSServiceRef(interp, objv[1], &sdRef) != TCL_OK) {
        return TCL_ERROR;
    }
    fd = DNSServiceRefSockFD((DNSServiceRef) sdRef);
    if (fd == -1) {
        Tcl_SetResult(interp, "DNSServiceRefSockFD failed", TCL_STATIC);
        return TCL_ERROR;
    }   
    chan = Tcl_MakeTcpClientChannel((ClientData) fd);
    Tcl_RegisterChannel(interp, chan);
	Tcl_AppendElement(interp, Tcl_GetChannelName(chan));
    return TCL_OK;
}

static int
DNSServiceQueryRecordObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                              Tcl_Obj *CONST objv[])
{
 int interfaceIndex, flags, rrtype, rrclass;
    char *fullname;
    Tcl_Obj *objPtr, *scriptObjPtr;
    DNSServiceRef sdRef = NULL;
    DNSServiceErrorType err;
    BonjourContext *contextPtr;
    
    if (objc != 8) {
        Tcl_WrongNumArgs(interp, 1, objv, "sdRefVar flags interfaceIndex fullname rrtype rrclass callback");
        return TCL_ERROR;
    }

     if (Tcl_GetIntFromObj(interp, objv[2], &flags) == TCL_ERROR
        || Tcl_GetIntFromObj(interp, objv[3], &interfaceIndex) == TCL_ERROR
		|| Tcl_GetIntFromObj(interp, objv[5], &rrtype) == TCL_ERROR
		|| Tcl_GetIntFromObj(interp, objv[6], &rrclass) == TCL_ERROR) {
        return TCL_ERROR;
    }

    fullname = Tcl_GetStringFromObj(objv[4], NULL);
   
    contextPtr = (BonjourContext *) Tcl_Alloc(sizeof(*contextPtr));
    scriptObjPtr = Tcl_DuplicateObj(objv[7]);
    contextPtr->interp = interp;
    contextPtr->scriptObjPtr = scriptObjPtr;

    err = DNSServiceQueryRecord(&sdRef, flags, interfaceIndex, fullname, rrtype, 
                                rrclass, TclDNSServiceQueryRecordReply,
                                (void *) contextPtr);

    if (!sdRef || err != kDNSServiceErr_NoError) {
        Tcl_SetResult(interp, TclmDNSError(interp, err), TCL_STATIC);
        return TCL_ERROR;
    }

    DNSServiceRefToObj(sdRef, &objPtr);
    if (Tcl_ObjSetVar2(interp, objv[1], NULL, objPtr, TCL_LEAVE_ERR_MSG) == NULL) {
        Tcl_DecrRefCount(objPtr);
        return TCL_ERROR;
    }

    return TCL_OK;
}

static int
DNSServiceBrowseObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                              Tcl_Obj *CONST objv[])
{
	int interfaceIndex, flags;
    char *regType, *domain;
    Tcl_Obj *objPtr, *scriptObjPtr;
    DNSServiceRef sdRef = NULL;
    DNSServiceErrorType err;
    BonjourContext *contextPtr;
    
    if (objc != 7) {
        Tcl_WrongNumArgs(interp, 1, objv, "sdRefVar flags interface regType domain callback");
        return TCL_ERROR;
    }

     if (Tcl_GetIntFromObj(interp, objv[2], &flags) == TCL_ERROR
        || Tcl_GetIntFromObj(interp, objv[3], &interfaceIndex) == TCL_ERROR) {
        return TCL_ERROR;
    }

    regType = Tcl_GetStringFromObj(objv[4], NULL);
    domain = Tcl_GetStringFromObj(objv[5], NULL);
   
    if (domain[0] == '.' && domain[1] == 0) {
        domain = "";
    }

    contextPtr = (BonjourContext *) Tcl_Alloc(sizeof(*contextPtr));
    scriptObjPtr = Tcl_DuplicateObj(objv[6]);
    contextPtr->interp = interp;
    contextPtr->scriptObjPtr = scriptObjPtr;

    err = DNSServiceBrowse(&sdRef, flags, interfaceIndex, regType, domain, 
                                TclDNSServiceBrowseReply,
                                (void *) contextPtr);

    if (!sdRef || err != kDNSServiceErr_NoError) {
        Tcl_SetResult(interp, TclmDNSError(interp, err), TCL_STATIC);
        return TCL_ERROR;
    }

    DNSServiceRefToObj(sdRef, &objPtr);
    if (Tcl_ObjSetVar2(interp, objv[1], NULL, objPtr, TCL_LEAVE_ERR_MSG) == NULL) {
        Tcl_DecrRefCount(objPtr);
        return TCL_ERROR;
    }

    return TCL_OK;
}

static int
DNSServiceProcessResultObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                              Tcl_Obj *CONST objv[])
{
    DNSServiceRef *sdRef = NULL;
    DNSServiceErrorType err;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "sdRef");
        return TCL_ERROR;
    }
    if (ObjToDNSServiceRef(interp, objv[1], &sdRef) != TCL_OK) {
        return TCL_ERROR;
    }
    err = DNSServiceProcessResult((DNSServiceRef) sdRef);
    if (!sdRef || err != kDNSServiceErr_NoError) {
        Tcl_SetResult(interp, TclmDNSError(interp, err), TCL_STATIC);
        return TCL_ERROR;
    }
	
    return TCL_OK;
}

static int
DNSServiceEnumerateDomainsObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                                 Tcl_Obj *CONST objv[])
{  
    int flags, interfaceIndex;
    Tcl_Obj *objPtr, *scriptObjPtr;
    DNSServiceRef sdRef = NULL;
    DNSServiceErrorType err;
    BonjourContext *contextPtr;
    
    if (objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "sdRefVar flags interfaceIndex callback");
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[2], &flags) == TCL_ERROR
        || Tcl_GetIntFromObj(interp, objv[3], &interfaceIndex) == TCL_ERROR) {
        return TCL_ERROR;
    }
    
    contextPtr = (BonjourContext *) Tcl_Alloc(sizeof(*contextPtr));
    scriptObjPtr = Tcl_DuplicateObj(objv[4]);
    contextPtr->interp = interp;
    contextPtr->scriptObjPtr = scriptObjPtr;

    err = DNSServiceEnumerateDomains(&sdRef, flags, interfaceIndex, 
                                (DNSServiceDomainEnumReply) TclDNSServiceDomainEnumReply,
                                (void *) contextPtr);
    if (!sdRef || err != kDNSServiceErr_NoError) {
        Tcl_SetResult(interp, TclmDNSError(interp, err), TCL_STATIC);
        return TCL_ERROR;
    }
    DNSServiceRefToObj(sdRef, &objPtr);
    if (Tcl_ObjSetVar2(interp, objv[1], NULL, objPtr, TCL_LEAVE_ERR_MSG) == NULL) {
        Tcl_DecrRefCount(objPtr);
        return TCL_ERROR;
    }

    return TCL_OK;
}

static int
DNSServiceRegisterObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                                 Tcl_Obj *CONST objv[])
{  
    int interfaceIndex, flags, txtLen;
    uint16_t portAsNumber;
    char *name, *regType, *domain, *host, *port, *txtRecord;
    Tcl_Obj *objPtr, *scriptObjPtr;
    DNSServiceRef sdRef = NULL;
    DNSServiceErrorType err;
    BonjourContext *contextPtr;

    if (objc != 12) {
        Tcl_WrongNumArgs(interp, 1, objv, "sdRefVar flags interface name regType domain host port txtLen txtRecord callback");
        return TCL_ERROR;
    }
   
    if (Tcl_GetIntFromObj(interp, objv[2], &flags) == TCL_ERROR
        || Tcl_GetIntFromObj(interp, objv[3], &interfaceIndex) == TCL_ERROR
        || Tcl_GetIntFromObj(interp, objv[9], &txtLen) == TCL_ERROR) {
        return TCL_ERROR;
    }

    name = Tcl_GetStringFromObj(objv[4], NULL);
    regType = Tcl_GetStringFromObj(objv[5], NULL);
    domain = Tcl_GetStringFromObj(objv[6], NULL);
    host = Tcl_GetStringFromObj(objv[7], NULL);
    port = Tcl_GetStringFromObj(objv[8], NULL);
    txtRecord = Tcl_GetStringFromObj(objv[10], NULL);

    portAsNumber = (int)strtol(port, (char **)NULL, 10);
    portAsNumber = htons(portAsNumber);

    if (name[0] == '.' && name[1] == 0) {
        name = "";
    }
    if (domain[0] == '.' && domain[1] == 0) {
        domain = "";
    }

    contextPtr = (BonjourContext *) Tcl_Alloc(sizeof(*contextPtr));
    scriptObjPtr = Tcl_DuplicateObj(objv[11]);
    contextPtr->interp = interp;
    contextPtr->scriptObjPtr = scriptObjPtr;

    err = DNSServiceRegister(&sdRef, flags, interfaceIndex, name, regType, domain, host, portAsNumber, txtLen, txtRecord, 
                                TclDNSServiceRegisterReply,
                                (void *) contextPtr);

    if (!sdRef || err != kDNSServiceErr_NoError) {
        Tcl_SetResult(interp, TclmDNSError(interp, err), TCL_STATIC);
        return TCL_ERROR;
    }
    DNSServiceRefToObj(sdRef, &objPtr);
    if (Tcl_ObjSetVar2(interp, objv[1], NULL, objPtr, TCL_LEAVE_ERR_MSG) == NULL) {
        Tcl_DecrRefCount(objPtr);
        return TCL_ERROR;
    }

    return TCL_OK;
}

static void
TclDNSServiceQueryRecordReply(DNSServiceRef sdRef, DNSServiceFlags flags,
                            uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                            const char *fullname, uint16_t rrtype, uint16_t rrclass,
							uint16_t rdlen, const void *rdata, uint32_t ttl, void *context)
{
    int objc;
    Tcl_Obj **objv;
    Tcl_Obj *listObjPtr;
    Tcl_Interp *interp;
    BonjourContext *contextPtr = (BonjourContext *) context;

    interp = contextPtr->interp;
    listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

    Tcl_ListObjAppendElement(interp, listObjPtr, contextPtr->scriptObjPtr);
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(flags));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(errorCode));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(interfaceIndex));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewStringObj((char *)fullname, strlen(fullname)));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(rrtype));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(rrclass));
	Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(rdlen));
	Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewStringObj((char *)rdata, strlen(rdata)));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(ttl));

    Tcl_ListObjGetElements(interp, listObjPtr, &objc, &objv);

    Tcl_EvalObjv(interp, objc, objv, TCL_EVAL_GLOBAL);
}

static void DNSSD_API
TclDNSServiceDomainEnumReply(DNSServiceRef sdRef, DNSServiceFlags flags,
                            uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                            const char *replyDomain, void *context)
{
    int objc;
    Tcl_Obj **objv;
    Tcl_Obj *listObjPtr;
    Tcl_Interp *interp;
    BonjourContext *contextPtr = (BonjourContext *) context;

    interp = contextPtr->interp;
    listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

    Tcl_ListObjAppendElement(interp, listObjPtr, contextPtr->scriptObjPtr);
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(flags));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(errorCode));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(interfaceIndex));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewStringObj((char *)replyDomain, strlen(replyDomain)));

    Tcl_ListObjGetElements(interp, listObjPtr, &objc, &objv);

    Tcl_EvalObjv(interp, objc, objv, TCL_EVAL_GLOBAL);

}

static void DNSSD_API
TclDNSServiceRegisterReply(DNSServiceRef sdRef, DNSServiceFlags flags,
                            DNSServiceErrorType errorCode, const char *name, const char *regType,
                            const char *domain, void *context)
{
    int objc;
    Tcl_Obj **objv;
    Tcl_Obj *listObjPtr;
    Tcl_Interp *interp;
    BonjourContext *contextPtr = (BonjourContext *) context;

    interp = contextPtr->interp;
    listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

    Tcl_ListObjAppendElement(interp, listObjPtr, contextPtr->scriptObjPtr);
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(flags));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(errorCode));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewStringObj((char *)name, strlen(name)));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewStringObj((char *)regType, strlen(regType)));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewStringObj((char *)domain, strlen(domain)));

    Tcl_ListObjGetElements(interp, listObjPtr, &objc, &objv);

    Tcl_EvalObjv(interp, objc, objv, TCL_EVAL_GLOBAL);
}

static void DNSSD_API 
TclDNSServiceBrowseReply(DNSServiceRef sdRef, DNSServiceFlags flags, 
                            uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName, 
                            const char *regType, const char *replyDomain, void *context)
{
    int objc;
    Tcl_Obj **objv;
    Tcl_Obj *listObjPtr;
    Tcl_Interp *interp;
    BonjourContext *contextPtr = (BonjourContext *) context;

    interp = contextPtr->interp;
    listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

    Tcl_ListObjAppendElement(interp, listObjPtr, contextPtr->scriptObjPtr);
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(flags));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(interfaceIndex));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewIntObj(errorCode));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewStringObj((char *)serviceName, strlen(serviceName)));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewStringObj((char *)regType, strlen(regType)));
    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewStringObj((char *)replyDomain, strlen(replyDomain)));

    Tcl_ListObjGetElements(interp, listObjPtr, &objc, &objv);

    Tcl_EvalObjv(interp, objc, objv, TCL_EVAL_GLOBAL);
}

// Tcl commands for constructing TXT records

static int
TXTRecordCreateObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                                 Tcl_Obj *CONST objv[])
{
    Tcl_Obj *objPtr;
    TXTRecordRef *txtRefPtr;
    char *buffer;
    
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "txtRefVar");
        return TCL_ERROR;
    }

    txtRefPtr = (TXTRecordRef *) Tcl_Alloc(sizeof(*txtRefPtr));
    buffer = Tcl_Alloc(TXTBUFSZ);
    TXTRecordCreate(txtRefPtr, TXTBUFSZ, (void *) buffer);
    TXTRecordRefToObj(txtRefPtr, &objPtr);
    if (Tcl_ObjSetVar2(interp, objv[1], NULL, objPtr, TCL_LEAVE_ERR_MSG) == NULL) {
        Tcl_DecrRefCount(objPtr);
        return TCL_ERROR;
    }

    return TCL_OK;
}

static int
TXTRecordGetBytesPtrObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                                 Tcl_Obj *CONST objv[])
{
    Tcl_Obj *objPtr;
    TXTRecordRef *txtPtr = NULL;
    unsigned char *buffer;
    int len;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "txtRef");
        return TCL_ERROR;
    }
    if (ObjToTXTRecordRef(interp, objv[1], &txtPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    buffer = (unsigned char *) TXTRecordGetBytesPtr(txtPtr);
    len = (int) TXTRecordGetLength(txtPtr);
    objPtr = Tcl_NewByteArrayObj(buffer, len);
    Tcl_SetObjResult(interp, objPtr);

    return TCL_OK;
}

static int
TXTRecordGetLengthObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                                 Tcl_Obj *CONST objv[])
{  
    Tcl_Obj *objPtr;
    TXTRecordRef *txtPtr = NULL;
    int len;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "txtRef");
        return TCL_ERROR;
    }
    if (ObjToTXTRecordRef(interp, objv[1], &txtPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    len = (int) TXTRecordGetLength(txtPtr);
    objPtr = Tcl_NewIntObj(len);
    Tcl_SetObjResult(interp, objPtr);

    return TCL_OK;
}

static int
TXTRecordRemoveValueObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                                 Tcl_Obj *CONST objv[])
{
    TXTRecordRef *txtRefPtr;
    DNSServiceErrorType err;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "txtRef key");
        return TCL_ERROR;
    }
    if (ObjToTXTRecordRef(interp, objv[1], &txtRefPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    err = TXTRecordRemoveValue(txtRefPtr, Tcl_GetString(objv[2]));
    if (err != kDNSServiceErr_NoError) {
        Tcl_SetResult(interp, TclmDNSError(interp, err), TCL_STATIC);
        return TCL_ERROR;
    }

    return TCL_OK;
}

static int
TXTRecordSetValueObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                                 Tcl_Obj *CONST objv[])
{  
    TXTRecordRef *txtRefPtr;
    DNSServiceErrorType err;
    int valueLen;
    char *value;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "txtRef key value");
        return TCL_ERROR;
    }
    if (ObjToTXTRecordRef(interp, objv[1], &txtRefPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    value = Tcl_GetStringFromObj(objv[3], &valueLen);
    err = TXTRecordSetValue(txtRefPtr, Tcl_GetString(objv[2]), valueLen, value);
    if (err != kDNSServiceErr_NoError) {
        Tcl_SetResult(interp, TclmDNSError(interp, err), TCL_STATIC);
        return TCL_ERROR;
    }

    return TCL_OK;
}

// Utils

static char *
mDNSStrErr(DNSServiceErrorType err)
{
    switch (err) {
    case kDNSServiceErr_NoError:
        return "kDNSServiceErr_NoError";
    case kDNSServiceErr_Unknown:
        return "kDNSServiceErr_Unknown";
    case kDNSServiceErr_NoSuchName:
        return "kDNSServiceErr_NoSuchName";
    case kDNSServiceErr_NoMemory:
        return "kDNSServiceErr_NoMemory";
    case kDNSServiceErr_BadParam:
        return "kDNSServiceErr_BadParam";
    case kDNSServiceErr_BadReference:
        return "kDNSServiceErr_BadReference";
    case kDNSServiceErr_BadState:
        return "kDNSServiceErr_BadState";
    case kDNSServiceErr_BadFlags:
        return "kDNSServiceErr_BadFlags";
    case kDNSServiceErr_Unsupported:
        return "kDNSServiceErr_Unsupported";
    case kDNSServiceErr_NotInitialized:
        return "kDNSServiceErr_NotInitialized";
    case kDNSServiceErr_AlreadyRegistered:
        return "kDNSServiceErr_AlreadyRegistered";
    case kDNSServiceErr_NameConflict:
        return "kDNSServiceErr_NameConflict";
    case kDNSServiceErr_Invalid:
        return "kDNSServiceErr_Invalid";
    case kDNSServiceErr_Firewall:
        return "kDNSServiceErr_Firewall";
    case kDNSServiceErr_Incompatible:
        return "kDNSServiceErr_Incompatible";
    case kDNSServiceErr_BadInterfaceIndex:
        return "kDNSServiceErr_BadInterfaceIndex";
    case kDNSServiceErr_Refused:
        return "kDNSServiceErr_Refused";
    case kDNSServiceErr_NoSuchRecord:
        return "kDNSServiceErr_NoSuchRecord";
    case kDNSServiceErr_NoAuth:
        return "kDNSServiceErr_NoAuth";
    case kDNSServiceErr_NoSuchKey:
        return "kDNSServiceErr_NoSuchKey";
    case kDNSServiceErr_NATTraversal:
        return "kDNSServiceErr_NATTraversal";
    case kDNSServiceErr_DoubleNAT:
        return "kDNSServiceErr_DoubleNAT";
    case kDNSServiceErr_BadTime:
        return "kDNSServiceErr_BadTime";
    default:
        return "kDNSServiceErr_Unknown";
    }
}

static char *
TclmDNSError(Tcl_Interp *interp, DNSServiceErrorType err)
{
    char *diagMsgPtr, buf[24];
    
    diagMsgPtr = mDNSStrErr(err);
    sprintf(buf, "%d", err);
    Tcl_SetErrorCode(interp, "DNSServiceErrorType", buf, diagMsgPtr, NULL);
    return diagMsgPtr;
}
