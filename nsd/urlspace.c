/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 * 
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */


/* 
 * urlspace.c --
 *
 *	This file implements a Trie data structure. It is used
 *	for "UrlSpecificData"; for example, when one registers
 *	a handler for all GET /foo/bar/ *.html requests, the data
 *	structure that holds that information is implemented herein.
 *	For full details see the file doc/urlspace.txt.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/urlspace.c,v 1.11 2003/11/03 19:23:26 pkhincha Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * This optimization, when turned on, prevents the server from doing a
 * whole lot of calls to Tcl_StringMatch on every lookup in urlspace.
 * Instead, a strcmp is done. This hasn't been thoroughly tested, so
 * it is off by default.
 *
 *  #define __URLSPACE_OPTIMIZE__
 */

/*
 * This structure defines a Node. It is the lowest-level structure in
 * urlspace and contains the data the the user puts in. It holds data
 * whose scope is a set of URLs, such as /foo/bar/ *.html.
 * Data/cleanup functions are kept seperately for inheriting and non-
 * inheriting URLs, as there could be overlap.
 */

typedef struct {
    int    id;                          /* Handle from Ns_UrlSpecificAlloc */
    void  *dataInherit;                 /* User's data */
    void  *dataNoInherit;               /* User's data */
    void   (*deletefuncInherit) (void *);    /* Cleanup function */
    void   (*deletefuncNoInherit) (void *);  /* Cleanup function */
} Node;

/*
 * This structure defines a trie. A trie is a tree whose nodes are
 * branches and channels. It is an inherently recursive data structure,
 * and each node is itself a trie. Each node represents one "part" of
 * a URL; in this case, a "part" is server name, method, directory, or
 * wildcard.
 */

typedef struct {
    Ns_Index   branches;
    Ns_Index  *indexnode;
} Trie;

/*
 * A branch is a typical node in a Trie. The "word" is the part of the
 * URL that the branch represents, and "node" is the sub-trie.
 */

typedef struct {
    char  *word;
    Trie   node;
} Branch;

/*
 * A channel is much like a branch. It exists only at the second level
 * (Channels come out of Junctions, which are top-level structures).
 * The filter is a copy of the very last part of the URLs matched by
 * branches coming out of this channel (only branches come out of channels).
 * When looking for a URL, the filename part of the target URL is compared
 * with the filter in each channel, and the channel is traversed only if
 * there is a match
 */

typedef struct {
    char  *filter;
    Trie   trie;
} Channel;

/*
 * A Junction is the top-level structure. Channels come out of a junction.
 * Currently, only one junction is defined--the static global urlspace.
 */

typedef struct {
    Ns_Index byname;
    /* 
     * We've experimented with getting rid of this index because
     * it is like byname but in semi-reverse lexicographical
     * order.  This optimization seems to work in all cases, but
     * we need a thorough way of testing all cases.
     */
#ifndef __URLSPACE_OPTIMIZE__
    Ns_Index byuse;
#endif
} Junction;

/*
 * Local functions defined in this file
 */

static void  TrieDestroy(Trie *trie);
static void  NodeDestroy(Node *nodePtr);
static int   CmpNodes(Node **leftPtrPtr, Node **rightPtrPtr);
static int   CmpIdWithNode(int id, Node **nodePtrPtr);
static Ns_Index * IndexNodeCreate(void);
static void  IndexNodeDestroy(Ns_Index *indexPtr);
static int   CmpBranches(Branch **leftPtrPtr, Branch **rightPtrPtr);
static int   CmpKeyWithBranch(char *key, Branch **branchPtrPtr);
static void  BranchDestroy(Branch *branchPtr);

/*
 * Utility functions
 */

static void MkSeq(Ns_DString *dsPtr, char *server, char *method, char *url);
#ifdef DEBUG
static void indentspace(int n);
static void PrintTrie(Trie *triePtr, int indent);
static void PrintJunction(Junction *junctionPtr);
static void PrintSeq(char *seq);
#endif

/*
 * Trie functions
 */

static void  TrieInit(Trie *triePtr);
static void  TrieAdd(Trie *triePtr, char *seq, int id, void *data, int flags, 
                     void (*deletefunc) (void *));
static void  TrieTrunc(Trie *triePtr, int id);
static void  TrieDestroy(Trie *triePtr);
static int   TrieBranchTrunc(Trie *triePtr, char *seq, int id);
static void *TrieFind(Trie *triePtr, char *seq, int id, int *depthPtr);
static void *TrieFindExact(Trie *triePtr, char *seq, int id, int flags);
static void *TrieDelete(Trie *triePtr, char *seq, int id, int flags);

/*
 * Channel functions
 */

#ifndef __URLSPACE_OPTIMIZE__
static int CmpChannels(Channel **leftPtrPtr, Channel **rightPtrPtr);
static int CmpKeyWithChannel(char *key, Channel **channelPtrPtr);
#endif

static int CmpChannelsAsStrings(Channel **leftPtrPtr, Channel **rightPtrPtr);
static int CmpKeyWithChannelAsStrings(char *key, Channel **channelPtrPtr);

/*
 * Juntion functions
 */

static void JunctionInit(Junction *juncPtr);
static void JunctionAdd(Junction *juncPtr, char *seq, int id, void *data,
			int flags, void (*deletefunc) (void *));
static void JunctionBranchTrunc(Junction *juncPtr, char *seq, int id);
static void *JunctionFind(Junction *juncPtr, char *seq, int id, int fast);
static void *JunctionFindExact(Junction *juncPtr, char *seq, int id, int flags,
			       int fast);
static void *JunctionDelete(Junction *juncPtr, char *seq, int id, int flags);

/*
 * Static variables defined in this file
 */

static Junction urlspace;    /* All URL-specific data stored here */
static Ns_Mutex lock;


/*
 *----------------------------------------------------------------------
 *
 * NsInitUrlSpace --
 *
 *	Initialize the urlspace API.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsInitUrlSpace(void)
{
    Ns_MutexSetName(&lock, "ns:urlspace");
    JunctionInit(&urlspace);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_UrlSpecificAlloc --
 *
 *	Allocate a unique ID to create a seperate virtual URL-space. 
 *
 * Results:
 *	An integer handle 
 *
 * Side effects:
 *	nextid will be incremented; don't call after server startup.
 *
 *----------------------------------------------------------------------
 */

int
Ns_UrlSpecificAlloc(void)
{
    int        id;
    static int nextid = 0;

    Ns_MutexLock(&lock);
    id = nextid++;
    Ns_MutexUnlock(&lock);
    return id;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_UrlSpecificSet --
 *
 *	Associate data with a set of URLs matching a wildcard, or 
 *	that are simply sub-URLs.
 *
 *	Flags can be NS_OP_NOINHERIT or NS_OP_NODELETE.
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	Will set data in the urlspace trie. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_UrlSpecificSet(char *server, char *method, char *url, int id, void *data,
		  int flags, void (*deletefunc) (void *))
{
    Ns_DString ds;

    Ns_DStringInit(&ds);
    MkSeq(&ds, server, method, url);
    Ns_MutexLock(&lock);
    JunctionAdd(&urlspace, ds.string, id, data, flags, deletefunc);
    Ns_MutexUnlock(&lock);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_UrlSpecificGet --
 *
 *	Find URL-specific data in the subspace identified by id that 
 *	the passed-in URL matches 
 *
 * Results:
 *	A pointer to user data, set with Ns_UrlSpecificSet 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_UrlSpecificGet(char *server, char *method, char *url, int id)
{
    Ns_DString  ds;
    void       *data;

    Ns_DStringInit(&ds);
    MkSeq(&ds, server, method, url);
    Ns_MutexLock(&lock);
    data = JunctionFind(&urlspace, ds.string, id, 0);
    Ns_MutexUnlock(&lock);
    Ns_DStringFree(&ds);

    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_UrlSpecificGetFast --
 *
 *	Similar to Ns_UrlSpecificGet, but doesn't support wildcards; 
 *	on the other hand, it's a lot faster. 
 *
 * Results:
 *	See Ns_UrlSpecificGet 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_UrlSpecificGetFast(char *server, char *method, char *url, int id)
{
    Ns_DString  ds;
    void       *data;

    Ns_DStringInit(&ds);
    MkSeq(&ds, server, method, url);
    Ns_MutexLock(&lock);
    data = JunctionFind(&urlspace, ds.string, id, 1);
    Ns_MutexUnlock(&lock);
    Ns_DStringFree(&ds);
    
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_UrlSpecificGetExact --
 *	Similar to Ns_UrlSpecificGet, but does not support URL
 *	inheritance
 *
 * Results:
 *	See Ns_UrlSpecificGet 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_UrlSpecificGetExact(char *server, char *method, char *url, int id,
		       int flags)
{
    Ns_DString  ds;
    void       *data;

    Ns_DStringInit(&ds);
    MkSeq(&ds, server, method, url);
    Ns_MutexLock(&lock);
    data = JunctionFindExact(&urlspace, ds.string, id, flags, 0);
    Ns_MutexUnlock(&lock);
    Ns_DStringFree(&ds);
    
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_UrlSpecificDestroy --
 *
 *	Delete some urlspecific data.
 *
 *	flags can be NS_OP_NODELETE, NS_OP_NOINHERIT and/or NS_OP_RECURSE
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	Will remove data from urlspace; don't call this after server 
 *	startup. 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_UrlSpecificDestroy(char *server, char *method, char *url, int id, int flags)
{
    Ns_DString  ds;
    void       *data = NULL;

    Ns_DStringInit(&ds);
    MkSeq(&ds, server, method, url);
    Ns_MutexLock(&lock);
    if (flags & NS_OP_RECURSE) {
	JunctionBranchTrunc(&urlspace, ds.string, id);
	data = NULL;
    } else {
	data = JunctionDelete(&urlspace, ds.string, id, flags);
    }
    Ns_MutexUnlock(&lock);
    Ns_DStringFree(&ds);
    
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ServerSpecificAlloc --
 *
 *	Allocate a unique integer to be used with Ns_ServerSpecific* 
 *	calls 
 *
 * Results:
 *	An integer handle 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ServerSpecificAlloc(void)
{
    return Ns_UrlSpecificAlloc();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ServerSpecificSet --
 *
 *	Set server-specific data 
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	See Ns_UrlSpecificSet 
 *
 *----------------------------------------------------------------------
 */

void
Ns_ServerSpecificSet(char *handle, int id, void *data, int flags,
		     void (*deletefunc) (void *))
{
    Ns_UrlSpecificSet(handle, NULL, NULL, id, data, flags, deletefunc);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ServerSpecificGet --
 *
 *	Get server-specific data.
 *
 * Results:
 *	User server-specific data.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_ServerSpecificGet(char *handle, int id)
{
    return Ns_UrlSpecificGet(handle, NULL, NULL, id);
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_ServerSpecificDestroy --
 *
 *	Destroy server-specific data. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will remove data from urlspace.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_ServerSpecificDestroy(char *handle, int id, int flags)
{
    return Ns_UrlSpecificDestroy(handle, NULL, NULL, id, flags);
}


/*
 *----------------------------------------------------------------------
 *
 * NodeDestroy --
 *
 *	Free a node and its data 
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	The delete function is called and the node is freed 
 *
 *----------------------------------------------------------------------
 */

static void
NodeDestroy(Node *nodePtr)
{
    if (nodePtr == NULL) {
        goto done;
    }
    if (nodePtr->deletefuncNoInherit != NULL) {
        (*nodePtr->deletefuncNoInherit) (nodePtr->dataNoInherit);
    }
    if (nodePtr->deletefuncInherit != NULL) {
        (*nodePtr->deletefuncInherit) (nodePtr->dataInherit);
    }
    ns_free(nodePtr);

 done:
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * CmpNodes --
 *
 *	Compare two Nodes by id. Ns_Index calls use this as a callback.
 *
 * Results:
 *	0 if equal, 1 if left is greater, -1 if left is less.
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static int
CmpNodes(Node **leftPtrPtr, Node **rightPtrPtr)
{
    if ((*leftPtrPtr)->id != (*rightPtrPtr)->id) {
        return ((*leftPtrPtr)->id > (*rightPtrPtr)->id) ? 1 : -1;
    } else {
        return 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CmpIdWithNode --
 *
 *	Compare a node's ID to a passed-in ID; called by Ns_Index*
 *
 * Results:
 *	0 if equal; 1 if id is too high; -1 if id is too low 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static int
CmpIdWithNode(int id, Node **nodePtrPtr)
{
    if (id != (*nodePtrPtr)->id) {
        return (id > (*nodePtrPtr)->id) ? 1 : -1;
    } else {
        return 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * IndexNodeCreate --
 *
 *	Initialize a trie->indexnode structure 
 *
 * Results:
 *	A pointer to an appropriately initialized index 
 *
 * Side effects:
 *	Memory alloacted. 
 *
 *----------------------------------------------------------------------
 */

static Ns_Index *
IndexNodeCreate(void)
{
    Ns_Index *indexPtr;

    indexPtr = ns_malloc(sizeof(Ns_Index));
    Ns_IndexInit(indexPtr, 5, (int (*) (const void *, const void *)) CmpNodes,
        (int (*) (const void *, const void *)) CmpIdWithNode);
    
    return indexPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * IndexNodeDestroy --
 *
 *	Wipe out a trie->indexnode structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory freed.
 *
 *----------------------------------------------------------------------
 */

static void
IndexNodeDestroy(Ns_Index *indexPtr)
{
    int i;

    i = Ns_IndexCount(indexPtr);
    while (i--) {
        NodeDestroy(Ns_IndexEl(indexPtr, i));
    }
    Ns_IndexDestroy(indexPtr);
    ns_free(indexPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * CmpBranches --
 *
 *	Compare two branches' word members. Called by Ns_Index* 
 *
 * Results:
 *	0 if equal, -1 if left is greater; 1 if right is greater 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CmpBranches(Branch **leftPtrPtr, Branch **rightPtrPtr)
{
    return strcmp((*leftPtrPtr)->word, (*rightPtrPtr)->word);
}


/*
 *----------------------------------------------------------------------
 *
 * CmpBranches --
 *
 *	Compare a branch's word to a passed-in key; called by 
 *	Ns_Index*.
 *
 * Results:
 *	0 if equal, -1 if left is greater; 1 if right is greater.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
CmpKeyWithBranch(char *key, Branch ** branchPtrPtr)
{
    return strcmp(key, (*branchPtrPtr)->word);
}


/*
 *----------------------------------------------------------------------
 *
 * BranchDestroy --
 *
 *	Free a branch structure 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will free memory. 
 *
 *----------------------------------------------------------------------
 */

static void
BranchDestroy(Branch *branchPtr)
{
    ns_free(branchPtr->word);
    TrieDestroy(&branchPtr->node);
    ns_free(branchPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TrieInit --
 *
 *	Initialize a Trie data structure with 25 branches and set the 
 *	Cmp functions for Ns_Index*. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	The trie is initialized and memory is allocated; memory is 
 *	allocated. 
 *
 *----------------------------------------------------------------------
 */

static void
TrieInit(Trie *triePtr)
{
    Ns_IndexInit(&triePtr->branches, 25,
		 (int (*) (const void *, const void *)) CmpBranches,
		 (int (*) (const void *, const void *)) CmpKeyWithBranch);
    triePtr->indexnode = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * TrieAdd --
 *
 *	Add something to the Trie data structure, usually to the 
 *	variable urlspace. 
 *
 *	seq is a null-delimited string of words, terminated with
 *	two nulls.
 *	id is allocated with Ns_UrlSpecificAlloc.
 *	flags is a bitmask; optionally OR NS_OP_NODELETE,
 *	NS_OP_NOINHERIT for desired behavior.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Memory is allocated. If a node is found and the 
 *	NS_OP_NODELETE is not set, the current node's data is deleted. 
 *
 *----------------------------------------------------------------------
 */

static void
TrieAdd(Trie *triePtr, char *seq, int id, void *data, int flags,
	void (*deletefunc) (void *))
{
    if (*seq == '\0') {
        Node *nodePtr;

        /*
	 * The entire sequence has been traversed, creating a branch
	 * for each word. Now it is time to make a Node.
	 *
	 * First, allocate a new node or find a matching one in existance.
	 */
	
        if (triePtr->indexnode == NULL) {
            triePtr->indexnode = IndexNodeCreate();
            nodePtr = NULL;
        } else {
            nodePtr = Ns_IndexFind(triePtr->indexnode, (void *) id);
        }

        if (nodePtr == NULL) {

	    /*
	     * Create and initialize a new node.
	     */
	    
            nodePtr = ns_malloc(sizeof(Node));
            nodePtr->id = id;
            Ns_IndexAdd(triePtr->indexnode, nodePtr);
            nodePtr->dataInherit = NULL;
            nodePtr->dataNoInherit = NULL;
            nodePtr->deletefuncInherit = NULL;
            nodePtr->deletefuncNoInherit = NULL;
        } else {

	    /*
	     * If NS_OP_NODELETE is NOT set, then delete the current node
	     * because one already exists.
	     */
	    
            if ((flags & NS_OP_NODELETE) == 0) {
                if ((flags & NS_OP_NOINHERIT) != 0) {
                    if (nodePtr->deletefuncNoInherit != NULL) {
                        (*nodePtr->deletefuncNoInherit)
			    (nodePtr->dataNoInherit);
                    }
                } else {
                    if (nodePtr->deletefuncInherit != NULL) {
                        (*nodePtr->deletefuncInherit) (nodePtr->dataInherit);
                    }
                }
            }
        }

        if (flags & NS_OP_NOINHERIT) {
            nodePtr->dataNoInherit = data;
            nodePtr->deletefuncNoInherit = deletefunc;
        } else {
            nodePtr->dataInherit = data;
            nodePtr->deletefuncInherit = deletefunc;
        }
    } else {
        Branch *branchPtr;

	/*
	 * We are parsing the middle of a sequence, such as "foo" in:
	 * "server1\0GET\0foo\0*.html\0"
	 *
	 * Create a new branch and recurse to add the next word in the
	 * sequence.
	 */

	branchPtr = Ns_IndexFind(&triePtr->branches, seq);
        if (branchPtr == NULL) {
            branchPtr = ns_malloc(sizeof(Branch));
            branchPtr->word = ns_strdup(seq);
            TrieInit(&branchPtr->node);

            Ns_IndexAdd(&triePtr->branches, branchPtr);
        }
        TrieAdd(&branchPtr->node, seq + strlen(seq) + 1, id, data, flags,
		deletefunc);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TrieTrunc --
 *
 *	Wipes out all references to id. If id==-1 then it wipes out 
 *	everything. 
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	Nodes and branches may be destroyed/freed. 
 *
 *----------------------------------------------------------------------
 */

static void
TrieTrunc(Trie *triePtr, int id)
{
    int n;

    n = Ns_IndexCount(&triePtr->branches);

    if (n > 0) {
        /*
	 * Loop over each branch and recurse.
	 */
	
        int             i;

        for (i = 0; i < n; i++) {
            Branch *branchPtr;

	    branchPtr = Ns_IndexEl(&triePtr->branches, i);
            TrieTrunc(&branchPtr->node, id);
        }
    }
    if (triePtr->indexnode != NULL) {
        if (id != -1) {
            Node *nodePtr;

	    /*
	     * Destroy just the node for this ID
	     */
	    
	    nodePtr = Ns_IndexFind(triePtr->indexnode, (void *) id);
            if (nodePtr != NULL) {
                NodeDestroy(nodePtr);
                Ns_IndexDel(triePtr->indexnode, nodePtr);
            }
        } else {

	    /*
	     * Destroy the whole index of nodes.
	     */
	    
            IndexNodeDestroy(triePtr->indexnode);
            triePtr->indexnode = NULL;
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TrieDestroy --
 *
 *	Delete an entire Trie.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will free all the elements of the trie.
 *
 *----------------------------------------------------------------------
 */

static void
TrieDestroy(Trie *triePtr)
{
    int n;

    n = Ns_IndexCount(&triePtr->branches);

    if (n > 0) {
        int i;

	/*
	 * Loop over each branch and delete it
	 */
	
        for (i = 0; i < n; i++) {
            Branch *branchPtr;

	    branchPtr = Ns_IndexEl(&triePtr->branches, i);
            BranchDestroy(branchPtr);
        }
        Ns_IndexDestroy(&triePtr->branches);
    }
    if (triePtr->indexnode != NULL) {
        IndexNodeDestroy(triePtr->indexnode);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TrieBranchTrunc --
 *
 *	Cut off a branch from a trie 
 *
 * Results:
 *	0 on success, -1 on failure 
 *
 * Side effects:
 *	Will delete a branch. 
 *
 *----------------------------------------------------------------------
 */

static int
TrieBranchTrunc(Trie *triePtr, char *seq, int id)
{
    if (*seq != '\0') {
        Branch *branchPtr;

	branchPtr = Ns_IndexFind(&triePtr->branches, seq);

	/*
	 * If this sequence exists, recursively delete it; otherwise
	 * return an error.
	 */
	
        if (branchPtr != NULL) {
            return TrieBranchTrunc(&branchPtr->node, seq + strlen(seq) + 1,
				   id);
        } else {
            return -1;
        }
    } else {
	/*
	 * The end of the sequence has been reached. Finish up the job
	 * and return success.
	 */
	
        TrieTrunc(triePtr, id);
        return 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TrieFind --
 *
 *	Find a node in a trie matching a sequence.
 *
 * Results:
 *	Return the appropriate node's data.
 *
 * Side effects:
 *	The depth variable will be set-by-reference to the depth of
 *	the returned node. If no node is set, it will not be changed.
 *
 *----------------------------------------------------------------------
 */

static void *
TrieFind(Trie *triePtr, char *seq, int id, int *depthPtr)
{
    void *data;
    int   ldepth;

    data = NULL;
    ldepth = *depthPtr;
    if (triePtr->indexnode != NULL) {
        Node *nodePtr;

	/*
	 * We've reached a trie with an indexnode, which means that our
	 * data may be here. (If node is null that means that there
	 * is data at this branch, but not with this particular ID).
	 */

	nodePtr = Ns_IndexFind(triePtr->indexnode, (void *) id);
	
        if (nodePtr != NULL) {
            if ((*seq == '\0') && (nodePtr->dataNoInherit != NULL)) {
                data = nodePtr->dataNoInherit;
            } else {
                data = nodePtr->dataInherit;
            }
        }
    }
    if (*seq != '\0') {
        Branch *branchPtr;

	/*
	 * We have not yet reached the end of the sequence, so
	 * recurse if there are any sub-branches
	 */

	branchPtr = Ns_IndexFind(&triePtr->branches, seq);
        ldepth += 1;
        if (branchPtr != NULL) {
            void *p = TrieFind(&branchPtr->node, seq + strlen(seq) + 1, id,
			       &ldepth);

            if (p != NULL) {
                data = p;
                *depthPtr = ldepth;
            }
        }
    }
    
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * TrieFindExact --
 *
 *	Similar to TrieFind, but will not do inheritance.
 *      if (flags & NS_OP_NOINHERIT) then data set with
 *	that flag will be returned; otherwise only data set without that
 *	flag will be returned.
 *
 * Results:
 *	See TrieFind.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static void *
TrieFindExact(Trie *triePtr, char *seq, int id, int flags)
{
    void *data;

    data = NULL;
    if (*seq != '\0') {
        Branch *branchPtr;

	/*
	 * We have not reached the end of the sequence yet, so
	 * we must recurse.
	 */

	branchPtr = Ns_IndexFind(&triePtr->branches, seq);
        if (branchPtr != NULL) {
            data = TrieFindExact(&branchPtr->node, seq + strlen(seq) + 1, id,
                                 flags);
        }
    } else if (triePtr->indexnode != NULL) {
        Node *nodePtr;

	/*
	 * We reached the end of the sequence. Grab the data from
	 * this node. If the flag specifies NOINHERIT, then return
	 * the non-inheriting data, otherwise return the inheriting
	 * data.
	 */

	nodePtr = Ns_IndexFind(triePtr->indexnode, (void *) id);
        if (nodePtr != NULL) {
            if (flags & NS_OP_NOINHERIT) {
                data = nodePtr->dataNoInherit;
            } else {
                data = nodePtr->dataInherit;
            }
        }
    }
    
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * TrieDelete --
 *
 *	Delete a url, defined by a sequence, from a trie.
 *
 *	The NS_OP_NOINHERIT bit may be set in flags to use
 *	noninheriting data; NS_OP_NODELETE may be set to
 *	skip calling the delete function.
 *
 * Results:
 *	A pointer to the now-deleted data. 
 *
 * Side effects:
 *	Data may be deleted. 
 *
 *----------------------------------------------------------------------
 */

static void *
TrieDelete(Trie *triePtr, char *seq, int id, int flags)
{
    void *data;

    data = NULL;
    if (*seq != '\0') {
        Branch *branchPtr;

	/*
	 * We have not yet reached the end of the sequence. So
	 * recurse.
	 */

	branchPtr = Ns_IndexFind(&triePtr->branches, seq);
        if (branchPtr != NULL) {
            data = TrieDelete(&branchPtr->node, seq + strlen(seq) + 1, id,
			      flags);
        }
    } else if (triePtr->indexnode != NULL) {
        Node *nodePtr;

	/*
	 * We've reached the end of the sequence; if a node exists for
	 * this ID then delete the inheriting/noninheriting data (as
	 * specified in flags) and call the delte func if requested.
	 * The data will be set to null either way.
	 */

	nodePtr = Ns_IndexFind(triePtr->indexnode, (void *) id);
        if (nodePtr != NULL) {
            if (flags & NS_OP_NOINHERIT) {
                data = nodePtr->dataNoInherit;
                nodePtr->dataNoInherit = NULL;
                if (nodePtr->deletefuncNoInherit != NULL) {
                    if (!(flags & NS_OP_NODELETE)) {
                        (*nodePtr->deletefuncNoInherit) (data);
                    }
                    nodePtr->deletefuncNoInherit = NULL;
                }
            } else {
                data = nodePtr->dataInherit;
                nodePtr->dataInherit = NULL;
                if (nodePtr->deletefuncInherit != NULL) {
                    if (!(flags & NS_OP_NODELETE)) {
                        (*nodePtr->deletefuncInherit) (data);
                    }
                    nodePtr->deletefuncInherit = NULL;
                }
            }
        }
    }
    
    return data;
}

#ifndef __URLSPACE_OPTIMIZE__

/*
 *----------------------------------------------------------------------
 *
 * CmpChannels --
 *
 *	Compare the filters of two channels. 
 *
 * Results:
 *	0: Not the case that one contains the other OR they both 
 *	contain each other; 1: left contains right; -1: right contans 
 *	left. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
CmpChannels(Channel **leftPtrPtr, Channel **rightPtrPtr)
{
    int lcontainsr, rcontainsl;

    lcontainsr = Tcl_StringMatch((*rightPtrPtr)->filter,
				 (*leftPtrPtr)->filter);
    rcontainsl = Tcl_StringMatch((*leftPtrPtr)->filter,
				 (*rightPtrPtr)->filter);

    if (lcontainsr && rcontainsl) {
        return 0;
    } else if (lcontainsr) {
        return 1;
    } else if (rcontainsl) {
        return -1;
    } else {
        return 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CmpKeyWithChannel --
 *
 *	Compare a key to a channel's filter 
 *
 * Results:
 *	0: Not the case that one contains the other OR they both 
 *	contain each other; 1: key contains filter; -1: filter 
 *	contains key 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static int
CmpKeyWithChannel(char *key, Channel **channelPtrPtr)
{
    int lcontainsr, rcontainsl;

    lcontainsr = Tcl_StringMatch((*channelPtrPtr)->filter, key);
    rcontainsl = Tcl_StringMatch(key, (*channelPtrPtr)->filter);
    if (lcontainsr && rcontainsl) {
        return 0;
    } else if (lcontainsr) {
        return 1;
    } else if (rcontainsl) {
        return -1;
    } else {
	/*
	 * Neither is the case
	 */

        return 0;
    }
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * CmpChannelsAsStrings --
 *
 *	Compare the filters of two channels.
 *
 * Results:
 *	Same as strcmp.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
CmpChannelsAsStrings(Channel **leftPtrPtr, Channel **rightPtrPtr)
{
    return strcmp((*leftPtrPtr)->filter, (*rightPtrPtr)->filter);
}


/*
 *----------------------------------------------------------------------
 *
 * CmpKeyWithChannelAsStrings --
 *
 *	Compare a string key to a channel's filter 
 *
 * Results:
 *	Same as strcmp. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
CmpKeyWithChannelAsStrings(char *key, Channel **channelPtrPtr)
{
    return strcmp(key, (*channelPtrPtr)->filter);
}


/*
 *----------------------------------------------------------------------
 *
 * JunctionInit --
 *
 *	Initialize a junction.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will set up the index in a junction. 
 *
 *----------------------------------------------------------------------
 */

static void
JunctionInit(Junction *juncPtr)
{
#ifndef __URLSPACE_OPTIMIZE__
    Ns_IndexInit(&juncPtr->byuse, 5,
        (int (*) (const void *, const void *)) CmpChannels,
        (int (*) (const void *, const void *)) CmpKeyWithChannel);
#endif
    Ns_IndexInit(&juncPtr->byname, 5,
        (int (*) (const void *, const void *)) CmpChannelsAsStrings,
        (int (*) (const void *, const void *)) CmpKeyWithChannelAsStrings);
}


/*
 *----------------------------------------------------------------------
 *
 * JunctionBranchTrunc --
 *
 *	Truncate a branch, defined by a sequence and ID, in a 
 *	junction 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	See TrieBranchTrunc 
 *
 *----------------------------------------------------------------------
 */

static void
JunctionBranchTrunc(Junction *juncPtr, char *seq, int id)
{
    int i;
    int n;

    /*
     * Loop over every channel in a junction and truncate the sequence in
     * each.
     */
    
#ifndef __URLSPACE_OPTIMIZE__
    n = Ns_IndexCount(&juncPtr->byuse);
    for (i = 0; i < n; i++) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byuse, i);
        TrieBranchTrunc(&channelPtr->trie, seq, id);
    }
#else
    n = Ns_IndexCount(&juncPtr->byname);
    for (i = (n - 1); i >= 0; i--) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byname, i);
        TrieBranchTrunc(&channelPtr->trie, seq, id);
    }
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * JunctionAdd --
 *
 *	This function is called from Ns_UrlSpecificSet which is 
 *	usually called from Ns_RegisterRequest, 
 *	Ns_RegisterProxyRequest, InitAliases for mapping aliases, and 
 *	the nsperm functions TribeAlloc and Ns_AddPermission for 
 *	adding permissions. It adds a sequence, terminating in a new 
 *	node, to a junction.
 *
 *	Flags may be a bit-combination of NS_OP_NOINHERIT, NS_OP_NODELETE.
 *	NOINHERIT sets the data as noninheriting, so only an exact sequence
 *	will match in the future; NODELETE means that if a node already
 *	exists with this sequence/ID it will not be deleted but replaced.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Modifies seq, assuming
 *	seq = "handle\0method\0urltoken\0urltoken\0..\0\0\"
 *
 *----------------------------------------------------------------------
 */

static void
JunctionAdd(Junction *juncPtr, char *seq, int id, void *data, int flags,
    void (*deletefunc) (void *))
{
    Channel    *channelPtr;
    Ns_DString  dsWord;
    char       *p;
    int         l;
    int         depth;

    depth = 0;

    /*
     * Find out how deep the sequence is, and position p at the
     * beginning of the last word in the sequence.
     */
    
    for (p = seq; p[l = strlen(p) + 1] != '\0'; p += l) {
        depth++;
    }

    Ns_DStringInit(&dsWord);

    /*
     * If it's a valid sequence that has a wildcard in its last element,
     * append the whole string to dsWord, then cut off the last word from
     * p.
     * Otherwise, set dsWord to "*" because there is an implicit * wildcard
     * at the end of URLs like /foo/bar
     *
     * dsWord will eventually be used to set or find&reuse a channel filter.
     */
    
    if ((p != NULL) && (depth > 1) && (strchr(p, '*') || strchr(p, '?'))) {
        Ns_DStringAppend(&dsWord, p);
        *p = '\0';
    } else {
        Ns_DStringAppend(&dsWord, "*");
    }

    /*
     * Find a channel whose filter matches what the filter on this URL
     * should be.
     */
    
    channelPtr = Ns_IndexFind(&juncPtr->byname, dsWord.string);

    /* 
     * If no channel is found, create a new channel and add it to the
     * list of channels in the junction.
     */

    if (channelPtr == NULL) {
        channelPtr = (Channel *) ns_malloc(sizeof(Channel));
        channelPtr->filter = ns_strdup(dsWord.string);
        TrieInit(&channelPtr->trie);

#ifndef __URLSPACE_OPTIMIZE__
        Ns_IndexAdd(&juncPtr->byuse, channelPtr);
#endif
        Ns_IndexAdd(&juncPtr->byname, channelPtr);
    }
    
    /* 
     * Now we need to create a sequence of branches in the trie (if no
     * appropriate sequence already exists) and a node at the end of it.
     * TrieAdd will do that.
     */
    
    TrieAdd(&channelPtr->trie, seq, id, data, flags, deletefunc);

    Ns_DStringFree(&dsWord);
}



/*
 *----------------------------------------------------------------------
 *
 * JunctionFind --
 *
 *	Locate a node for a given sequence in a junction.
 *	As usual sequence is "handle\0method\0urltoken\0...\0\0".
 *
 *	The "fast" boolean switch makes it do strcmp instead of
 *	Tcl string matches on the filters. Not useful for wildcard
 *	matching.
 *
 * Results:
 *	User data
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static void *
JunctionFind(Junction *juncPtr, char *seq, int id, int fast)
{
    char *p;
    int   l;
    int   i, n;
    void *data;
    int   depth;

    n = 0;

    /*
     * After this loop, p will point at the last element in the sequence.
     * n will be the number of elements in the sequence.
     */
    
    for (p = seq; p[l = strlen(p) + 1] != '\0'; p += l) {
        n++;
    }

    if (n < 2) {
        /*
	 * If there are fewer than 2 elements then advance p to the
	 * end of the string.
	 */
        p += strlen(p) + 1;
    }

    /*
     * Check filters from most restrictive to least restrictive
     */
    
    data = NULL;
#ifndef __URLSPACE_OPTIMIZE__
    l = Ns_IndexCount(&juncPtr->byuse);
#else
    l = Ns_IndexCount(&juncPtr->byname);
#endif

#ifdef DEBUG
    if (n >= 2) {
        fprintf(stderr, "Checking Id=%d, Seq=", id);
        PrintSeq(seq);
        fputs("\n", stderr);
    }
#endif

    /* 
     * For __URLSPACE_OPTIMIZE__
     * Basically if we use the optimize, let's reverse the order
     * by which we search because the byname is in "almost" exact
     * reverse lexicographical order.
     *
     * Loop over all the channels in the index.
     */
    
#ifndef __URLSPACE_OPTIMIZE__
    for (i = 0; i < l; i++) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byuse, i);
#else
    for (i = (l - 1); i >= 0; i--) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byname, i);
#endif
        int doit;

	if (fast) {
	    doit = !strcmp(p, channelPtr->filter);
	} else {
	    doit = Tcl_StringMatch(p, channelPtr->filter);
	}
	if (doit) {
	    /*
	     * We got here because this url matches the filter
	     * (for example, it's *.adp).
	     */
	    
            if (data == NULL) {
		/*
		 * Nothing has been found so far. Traverse the channel
		 * and find the node; set data to that. Depth will be
		 * set to the level of the node.
		 */
		
                depth = 0;
                data = TrieFind(&channelPtr->trie, seq, id, &depth);
            } else {
                void *candidate;
                int   cdepth;

		/*
		 * Let's see if this channel has a node that also matches
		 * the sequence but is more specific (has a greater depth)
		 * that the previously found node.
		 */
		
                cdepth = 0;
                candidate = TrieFind(&channelPtr->trie, seq, id, &cdepth);
                if ((candidate != NULL) && (cdepth > depth)) {
                    data = candidate;
                    depth = cdepth;
                }
            }
        }

#ifdef DEBUG
        if (n >= 2) {
            if (data == NULL) {
                fprintf(stderr, "Channel %s: No match\n",
                    channelPtr->filter);
            } else {
                fprintf(stderr, "Channel %s: depth=%d, data=%p\n",
                    channelPtr->filter, depth, data);
            }
        }
#endif
    }

#ifdef DEBUG
    if (n >= 2) {
        fprintf(stderr, "Done.\n");
    }
#endif
    
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * JunctionFindExact --
 *
 *	Find a node in a junction that exactly matches a sequence.
 *
 * Results:
 *	User data.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void *
JunctionFindExact(Junction *juncPtr, char *seq, int id, int flags, int fast)
{
    char *p;
    int   l;
    int   i;
    int   depth;
    void *data;

    depth = 0;

    /*
     * Set p to the last element of the sequence, and
     * depth to the number of elements in the sequence.
     */
    
    for (p = seq; p[l = strlen(p) + 1] != '\0'; p += l) {
        depth++;
    }

    data = NULL;

    /*
     * First, loop through all the channels that have non-"*"
     * filters looking for an exact match
     */

#ifndef __URLSPACE_OPTIMIZE__
    l = Ns_IndexCount(&juncPtr->byuse);

    for (i = 0; i < l; i++) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byuse, i);
#else
    l = Ns_IndexCount(&juncPtr->byname);

    for (i = (l - 1); i >= 0; i--) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byname, i);
#endif
        if (strcmp(p, channelPtr->filter) == 0) {
	    /*
	     * The last element of the sequence exactly matches the
	     * filter, so this is the one. Wipe out the last word and
	     * return whatever node coems out of TrieFindExact.
	     */
	    
            *p = '\0';
            data = TrieFindExact(&channelPtr->trie, seq, id, flags);
            goto done;
        }
    }
    
    /*
     * Now go to the channel with the "*" filter and look there for 
     * an exact match:
     */
    
#ifndef __URLSPACE_OPTIMIZE__
    for (i = 0; i < l; i++) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byuse, i);
#else
    for (i = (l - 1); i >= 0; i--) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byname, i);
#endif
        if (strcmp("*", channelPtr->filter) == 0) {
            data = TrieFindExact(&channelPtr->trie, seq, id, flags);
            break;
        }
    }
    
  done:
    
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * JunctionDelete --
 *
 *	Delete a node from a junction matching a sequence 
 *
 * Results:
 *	A pointer to the deleted node 
 *
 * Side effects:
 *	The node will be deleted if NS_OP_NODELETE isn't set in flags 
 *
 *----------------------------------------------------------------------
 */

static void *
JunctionDelete(Junction *juncPtr, char *seq, int id, int flags)
{
    char *p;
    int   l;
    int   i;
    int   depth;
    void *data;

    /*
     * Set p to the last element of the sequence, and
     * depth to the number of elements in the sequence.
     */

    depth = 0;
    for (p = seq; p[l = strlen(p) + 1] != '\0'; p += l) {
        depth++;
    }

    data = NULL;

#ifndef __URLSPACE_OPTIMIZE__
    l = Ns_IndexCount(&juncPtr->byuse);
    for (i = 0; (i < l) && (data == NULL); i++) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byuse, i);
#else
    l = Ns_IndexCount(&juncPtr->byname);
    for (i = (l - 1); (i >= 0) && (data == NULL); i--) {
        Channel *channelPtr = Ns_IndexEl(&juncPtr->byname, i);
#endif
        if ((depth == 2) && (strcmp(p, channelPtr->filter) == 0)) {
	    /*
	     * This filter exactly matches the last element of the
	     * sequence, so get the node and delete it. (This is
	     * server-specific data because depth is 2).
	     */
	    
            *p = '\0';
            data = TrieFindExact(&channelPtr->trie, seq, id, flags);
            if (data != NULL) {
                TrieDelete(&channelPtr->trie, seq, id, flags);
            }
        } else if (Tcl_StringMatch(p, channelPtr->filter)) {
	    /*
	     * The filter matches, so get the node and delete it.
	     */
	    
            data = TrieFindExact(&channelPtr->trie, seq, id, flags);
            if (data != NULL) {
                TrieDelete(&channelPtr->trie, seq, id, flags);
            }
        }
    }
    
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * MkSeq --
 *
 *	Build a "sequence" out of a server/method/url; turns it into 
 *	"server\0method\0urltoken\0...\0\0" 
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	Sequence goes into ds 
 *
 *----------------------------------------------------------------------
 */

static void
MkSeq(Ns_DString *dsPtr, char *server, char *method, char *url)
{
    if ((method != NULL) && (url != NULL)) {
        char *p;
        int   done;

	/*
	 * It is URLspecific data, not serverspecific data
	 * if we get here.
	 */
	
        Ns_DStringNAppend(dsPtr, server, (int)(strlen(server) + 1));
        Ns_DStringNAppend(dsPtr, method, (int)(strlen(method) + 1));

	/*
	 * Loop over each directory in the URL and turn the slashes
	 * into nulls.
	 */
	
        done = 0;
        while (!done && *url != '\0') {
            if (*url != '/') {
                int l;

                p = strchr(url, '/');
                if (p != NULL) {
                    l = p - url;
                } else {
                    l = strlen(url);
                    done = 1;
                }

                Ns_DStringNAppend(dsPtr, url, l++);
                Ns_DStringNAppend(dsPtr, "\0", 1);
                url += l;
            } else {
                url++;
            }
        }

	/*
	 * Put another null on the end to mark the end of the
	 * string.
	 */
	
        Ns_DStringNAppend(dsPtr, "\0", 1);
    } else {
	/*
	 * This is Server-specific data, so there's only going to
	 * be one element.
	 */
	
        Ns_DStringNAppend(dsPtr, server, (int)(strlen(server) + 1));
        Ns_DStringNAppend(dsPtr, "\0", 1);
    }
}

#ifdef DEBUG

/*
 *----------------------------------------------------------------------
 *
 * indentspace --
 *
 *	Print n spaces.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will print to stderr.
 *
 *----------------------------------------------------------------------
 */

static void
indentspace(int n)
{
    int i;

    fputc('\n', stderr);
    for (i = 0; i < n; i++) {
        fputc(' ', stderr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PrintTrie --
 *
 *	Output the trie to standard error.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will write to stderr. 
 *
 *----------------------------------------------------------------------
 */

static void
PrintTrie(Trie *triePtr, int indent)
{
    int i;

    indentspace(indent);
    fprintf(stderr, "Branches:");
    for (i = 0; i < (&(triePtr->branches))->n; i++) {
        Branch *branch;

        branch = (Branch *) Ns_IndexEl(&(triePtr->branches), i);
        indentspace(indent + 2);
        fprintf(stderr, "(%s):", branch->word);
        PrintTrie(&(branch->node), indent + 4);
    }
    indentspace(indent);
    fprintf(stderr, "IndexNodes:");
    if (triePtr->indexnode != NULL) {
        for (i = 0; i < triePtr->indexnode->n; i++) {
            Node *nodePtr;

            nodePtr = (Node *) Ns_IndexEl(triePtr->indexnode, i);
            indentspace(indent + 2);
            if (nodePtr->dataInherit != NULL) {
                fprintf(stderr, "(Id: %d, inherit): %p", 
                        nodePtr->id, nodePtr->dataInherit);
            }
            if (nodePtr->dataNoInherit != NULL) {
                fprintf(stderr, "(Id: %d, noinherit): %p",
                        nodePtr->id, nodePtr->dataNoInherit);
            }
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PrintJunction --
 *
 *	Print a junction to std error. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will write to stderr. 
 *
 *----------------------------------------------------------------------
 */

static void
PrintJunction(Junction *junctionPtr)
{
    int i;

    fprintf(stderr, "Junction:");

#ifndef __URLSPACE_OPTIMIZE__
    for (i = 0; i < (&(junctionPtr->byuse))->n; i++) {
        Channel *channelPtr;
        channelPtr = (Channel *) Ns_IndexEl(&(junctionPtr->byuse), i);
#else
    for (i = ((&(junctionPtr->byname))->n - 1); i >= 0; i--) {
        Channel *channelPtr;
        channelPtr = (Channel *) Ns_IndexEl(&(junctionPtr->byname), i);
#endif
        fprintf(stderr, "\n  Channel[%d]:\n", i);
        fprintf(stderr, "    Filter: %s\n", channelPtr->filter);
        fprintf(stderr, "    Trie:");
        PrintTrie(&(channelPtr->trie), 4);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PrintSeq --
 *
 *	Print a null-delimited sequence to stderr.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will write to stderr. 
 *
 *----------------------------------------------------------------------
 */

static void
PrintSeq(char *seq)
{
    char *p;

    for (p = seq; *p != '\0'; p += strlen(p) + 1) {
        if (p != seq) {
            fputs(", ", stderr);
        }
        fputs(p, stderr);
    }
}

#endif
