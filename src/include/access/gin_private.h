/*--------------------------------------------------------------------------
 * gin_private.h
 *	  header file for postgres inverted index access method implementation.
 *
 *	Copyright (c) 2006-2014, PostgreSQL Global Development Group
 *
 *	src/include/access/gin_private.h
 *--------------------------------------------------------------------------
 */
#ifndef GIN_PRIVATE_H
#define GIN_PRIVATE_H

#include "access/genam.h"
#include "access/gin.h"
#include "access/itup.h"
#include "fmgr.h"
#include "storage/bufmgr.h"
#include "utils/rbtree.h"


/*
 * Page opaque data in an inverted index page.
 *
 * Note: GIN does not include a page ID word as do the other index types.
 * This is OK because the opaque data is only 8 bytes and so can be reliably
 * distinguished by size.  Revisit this if the size ever increases.
 * Further note: as of 9.2, SP-GiST also uses 8-byte special space.  This is
 * still OK, as long as GIN isn't using all of the high-order bits in its
 * flags word, because that way the flags word cannot match the page ID used
 * by SP-GiST.
 */
typedef struct GinPageOpaqueData
{
	BlockNumber rightlink;		/* next page if any */
	OffsetNumber maxoff;		/* number entries on GIN_DATA page: number of
								 * heap ItemPointers on GIN_DATA|GIN_LEAF page
								 * or number of PostingItems on GIN_DATA &
								 * ~GIN_LEAF page. On GIN_LIST page, number of
								 * heap tuples. */
	uint16		flags;			/* see bit definitions below */
} GinPageOpaqueData;

typedef GinPageOpaqueData *GinPageOpaque;

#define GIN_DATA		  (1 << 0)
#define GIN_LEAF		  (1 << 1)
#define GIN_DELETED		  (1 << 2)
#define GIN_META		  (1 << 3)
#define GIN_LIST		  (1 << 4)
#define GIN_LIST_FULLROW  (1 << 5)		/* makes sense only on GIN_LIST page */
#define GIN_INCOMPLETE_SPLIT (1 << 6)	/* page was split, but parent not updated */

/* Page numbers of fixed-location pages */
#define GIN_METAPAGE_BLKNO	(0)
#define GIN_ROOT_BLKNO		(1)

typedef struct GinMetaPageData
{
	/*
	 * Pointers to head and tail of pending list, which consists of GIN_LIST
	 * pages.  These store fast-inserted entries that haven't yet been moved
	 * into the regular GIN structure.
	 */
	BlockNumber head;
	BlockNumber tail;

	/*
	 * Free space in bytes in the pending list's tail page.
	 */
	uint32		tailFreeSize;

	/*
	 * We store both number of pages and number of heap tuples that are in the
	 * pending list.
	 */
	BlockNumber nPendingPages;
	int64		nPendingHeapTuples;

	/*
	 * Statistics for planner use (accurate as of last VACUUM)
	 */
	BlockNumber nTotalPages;
	BlockNumber nEntryPages;
	BlockNumber nDataPages;
	int64		nEntries;

	/*
	 * GIN version number (ideally this should have been at the front, but too
	 * late now.  Don't move it!)
	 *
	 * Currently 1 (for indexes initialized in 9.1 or later)
	 *
	 * Version 0 (indexes initialized in 9.0 or before) is compatible but may
	 * be missing null entries, including both null keys and placeholders.
	 * Reject full-index-scan attempts on such indexes.
	 */
	int32		ginVersion;
} GinMetaPageData;

#define GIN_CURRENT_VERSION		1

#define GinPageGetMeta(p) \
	((GinMetaPageData *) PageGetContents(p))

/*
 * Macros for accessing a GIN index page's opaque data
 */
#define GinPageGetOpaque(page) ( (GinPageOpaque) PageGetSpecialPointer(page) )

#define GinPageIsLeaf(page)    ( GinPageGetOpaque(page)->flags & GIN_LEAF )
#define GinPageSetLeaf(page)   ( GinPageGetOpaque(page)->flags |= GIN_LEAF )
#define GinPageSetNonLeaf(page)    ( GinPageGetOpaque(page)->flags &= ~GIN_LEAF )
#define GinPageIsData(page)    ( GinPageGetOpaque(page)->flags & GIN_DATA )
#define GinPageSetData(page)   ( GinPageGetOpaque(page)->flags |= GIN_DATA )
#define GinPageIsList(page)    ( GinPageGetOpaque(page)->flags & GIN_LIST )
#define GinPageSetList(page)   ( GinPageGetOpaque(page)->flags |= GIN_LIST )
#define GinPageHasFullRow(page)    ( GinPageGetOpaque(page)->flags & GIN_LIST_FULLROW )
#define GinPageSetFullRow(page)   ( GinPageGetOpaque(page)->flags |= GIN_LIST_FULLROW )

#define GinPageIsDeleted(page) ( GinPageGetOpaque(page)->flags & GIN_DELETED)
#define GinPageSetDeleted(page)    ( GinPageGetOpaque(page)->flags |= GIN_DELETED)
#define GinPageSetNonDeleted(page) ( GinPageGetOpaque(page)->flags &= ~GIN_DELETED)
#define GinPageIsIncompleteSplit(page) ( GinPageGetOpaque(page)->flags & GIN_INCOMPLETE_SPLIT)

#define GinPageRightMost(page) ( GinPageGetOpaque(page)->rightlink == InvalidBlockNumber)

/*
 * We use our own ItemPointerGet(BlockNumber|GetOffsetNumber)
 * to avoid Asserts, since sometimes the ip_posid isn't "valid"
 */
#define GinItemPointerGetBlockNumber(pointer) \
	BlockIdGetBlockNumber(&(pointer)->ip_blkid)

#define GinItemPointerGetOffsetNumber(pointer) \
	((pointer)->ip_posid)

/*
 * Special-case item pointer values needed by the GIN search logic.
 *	MIN: sorts less than any valid item pointer
 *	MAX: sorts greater than any valid item pointer
 *	LOSSY PAGE: indicates a whole heap page, sorts after normal item
 *				pointers for that page
 * Note that these are all distinguishable from an "invalid" item pointer
 * (which is InvalidBlockNumber/0) as well as from all normal item
 * pointers (which have item numbers in the range 1..MaxHeapTuplesPerPage).
 */
#define ItemPointerSetMin(p)  \
	ItemPointerSet((p), (BlockNumber)0, (OffsetNumber)0)
#define ItemPointerIsMin(p)  \
	(GinItemPointerGetOffsetNumber(p) == (OffsetNumber)0 && \
	 GinItemPointerGetBlockNumber(p) == (BlockNumber)0)
#define ItemPointerSetMax(p)  \
	ItemPointerSet((p), InvalidBlockNumber, (OffsetNumber)0xffff)
#define ItemPointerIsMax(p)  \
	(GinItemPointerGetOffsetNumber(p) == (OffsetNumber)0xffff && \
	 GinItemPointerGetBlockNumber(p) == InvalidBlockNumber)
#define ItemPointerSetLossyPage(p, b)  \
	ItemPointerSet((p), (b), (OffsetNumber)0xffff)
#define ItemPointerIsLossyPage(p)  \
	(GinItemPointerGetOffsetNumber(p) == (OffsetNumber)0xffff && \
	 GinItemPointerGetBlockNumber(p) != InvalidBlockNumber)

/*
 * Posting item in a non-leaf posting-tree page
 */
typedef struct
{
	/* We use BlockIdData not BlockNumber to avoid padding space wastage */
	BlockIdData child_blkno;
	ItemPointerData key;
} PostingItem;

#define PostingItemGetBlockNumber(pointer) \
	BlockIdGetBlockNumber(&(pointer)->child_blkno)

#define PostingItemSetBlockNumber(pointer, blockNumber) \
	BlockIdSet(&((pointer)->child_blkno), (blockNumber))

/*
 * Category codes to distinguish placeholder nulls from ordinary NULL keys.
 * Note that the datatype size and the first two code values are chosen to be
 * compatible with the usual usage of bool isNull flags.
 *
 * GIN_CAT_EMPTY_QUERY is never stored in the index; and notice that it is
 * chosen to sort before not after regular key values.
 */
typedef signed char GinNullCategory;

#define GIN_CAT_NORM_KEY		0		/* normal, non-null key value */
#define GIN_CAT_NULL_KEY		1		/* null key value */
#define GIN_CAT_EMPTY_ITEM		2		/* placeholder for zero-key item */
#define GIN_CAT_NULL_ITEM		3		/* placeholder for null item */
#define GIN_CAT_EMPTY_QUERY		(-1)	/* placeholder for full-scan query */

/*
 * Access macros for null category byte in entry tuples
 */
#define GinCategoryOffset(itup,ginstate) \
	(IndexInfoFindDataOffset((itup)->t_info) + \
	 ((ginstate)->oneCol ? 0 : sizeof(int16)))
#define GinGetNullCategory(itup,ginstate) \
	(*((GinNullCategory *) ((char*)(itup) + GinCategoryOffset(itup,ginstate))))
#define GinSetNullCategory(itup,ginstate,c) \
	(*((GinNullCategory *) ((char*)(itup) + GinCategoryOffset(itup,ginstate))) = (c))

/*
 * Access macros for leaf-page entry tuples (see discussion in README)
 */
#define GinGetNPosting(itup)	GinItemPointerGetOffsetNumber(&(itup)->t_tid)
#define GinSetNPosting(itup,n)	ItemPointerSetOffsetNumber(&(itup)->t_tid,n)
#define GIN_TREE_POSTING		((OffsetNumber)0xffff)
#define GinIsPostingTree(itup)	(GinGetNPosting(itup) == GIN_TREE_POSTING)
#define GinSetPostingTree(itup, blkno)	( GinSetNPosting((itup),GIN_TREE_POSTING), ItemPointerSetBlockNumber(&(itup)->t_tid, blkno) )
#define GinGetPostingTree(itup) GinItemPointerGetBlockNumber(&(itup)->t_tid)

#define GinGetPostingOffset(itup)	GinItemPointerGetBlockNumber(&(itup)->t_tid)
#define GinSetPostingOffset(itup,n) ItemPointerSetBlockNumber(&(itup)->t_tid,n)
#define GinGetPosting(itup)			((ItemPointer) ((char*)(itup) + GinGetPostingOffset(itup)))

#define GinMaxItemSize \
	MAXALIGN_DOWN(((BLCKSZ - SizeOfPageHeaderData - \
		MAXALIGN(sizeof(GinPageOpaqueData))) / 3 - sizeof(ItemIdData)))

/*
 * Access macros for non-leaf entry tuples
 */
#define GinGetDownlink(itup)	GinItemPointerGetBlockNumber(&(itup)->t_tid)
#define GinSetDownlink(itup,blkno)	ItemPointerSet(&(itup)->t_tid, blkno, InvalidOffsetNumber)


/*
 * Data (posting tree) pages
 */
#define GinDataPageGetRightBound(page)	((ItemPointer) PageGetContents(page))
#define GinDataPageGetData(page)	\
	(PageGetContents(page) + MAXALIGN(sizeof(ItemPointerData)))
/* non-leaf pages contain PostingItems */
#define GinDataPageGetPostingItem(page, i)	\
	((PostingItem *) (GinDataPageGetData(page) + ((i)-1) * sizeof(PostingItem)))
/* leaf pages contain ItemPointers */
#define GinDataPageGetItemPointer(page, i)	\
	((ItemPointer) (GinDataPageGetData(page) + ((i)-1) * sizeof(ItemPointerData)))
#define GinSizeOfDataPageItem(page) \
	(GinPageIsLeaf(page) ? sizeof(ItemPointerData) : sizeof(PostingItem))

#define GinDataPageGetFreeSpace(page)	\
	(BLCKSZ - MAXALIGN(SizeOfPageHeaderData) \
	 - MAXALIGN(sizeof(ItemPointerData)) \
	 - GinPageGetOpaque(page)->maxoff * GinSizeOfDataPageItem(page) \
	 - MAXALIGN(sizeof(GinPageOpaqueData)))

#define GinMaxLeafDataItems \
	((BLCKSZ - MAXALIGN(SizeOfPageHeaderData) - \
	  MAXALIGN(sizeof(ItemPointerData)) - \
	  MAXALIGN(sizeof(GinPageOpaqueData))) \
	 / sizeof(ItemPointerData))

/*
 * List pages
 */
#define GinListPageSize  \
	( BLCKSZ - SizeOfPageHeaderData - MAXALIGN(sizeof(GinPageOpaqueData)) )

/*
 * Storage type for GIN's reloptions
 */
typedef struct GinOptions
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	bool		useFastUpdate;	/* use fast updates? */
} GinOptions;

#define GIN_DEFAULT_USE_FASTUPDATE	true
#define GinGetUseFastUpdate(relation) \
	((relation)->rd_options ? \
	 ((GinOptions *) (relation)->rd_options)->useFastUpdate : GIN_DEFAULT_USE_FASTUPDATE)


/* Macros for buffer lock/unlock operations */
#define GIN_UNLOCK	BUFFER_LOCK_UNLOCK
#define GIN_SHARE	BUFFER_LOCK_SHARE
#define GIN_EXCLUSIVE  BUFFER_LOCK_EXCLUSIVE


/*
 * GinState: working data structure describing the index being worked on
 */
typedef struct GinState
{
	Relation	index;
	bool		oneCol;			/* true if single-column index */

	/*
	 * origTupDesc is the nominal tuple descriptor of the index, ie, the i'th
	 * attribute shows the key type (not the input data type!) of the i'th
	 * index column.  In a single-column index this describes the actual leaf
	 * index tuples.  In a multi-column index, the actual leaf tuples contain
	 * a smallint column number followed by a key datum of the appropriate
	 * type for that column.  We set up tupdesc[i] to describe the actual
	 * rowtype of the index tuples for the i'th column, ie, (int2, keytype).
	 * Note that in any case, leaf tuples contain more data than is known to
	 * the TupleDesc; see access/gin/README for details.
	 */
	TupleDesc	origTupdesc;
	TupleDesc	tupdesc[INDEX_MAX_KEYS];

	/*
	 * Per-index-column opclass support functions
	 */
	FmgrInfo	compareFn[INDEX_MAX_KEYS];
	FmgrInfo	extractValueFn[INDEX_MAX_KEYS];
	FmgrInfo	extractQueryFn[INDEX_MAX_KEYS];
	FmgrInfo	consistentFn[INDEX_MAX_KEYS];
	FmgrInfo	comparePartialFn[INDEX_MAX_KEYS];		/* optional method */
	/* canPartialMatch[i] is true if comparePartialFn[i] is valid */
	bool		canPartialMatch[INDEX_MAX_KEYS];
	/* Collations to pass to the support functions */
	Oid			supportCollation[INDEX_MAX_KEYS];
} GinState;

/* XLog stuff */

#define XLOG_GIN_CREATE_INDEX  0x00

#define XLOG_GIN_CREATE_PTREE  0x10

typedef struct ginxlogCreatePostingTree
{
	RelFileNode node;
	BlockNumber blkno;
	uint32		nitem;
	/* follows list of heap's ItemPointer */
} ginxlogCreatePostingTree;

#define XLOG_GIN_INSERT  0x20

typedef struct ginxlogInsert
{
	RelFileNode node;
	BlockNumber blkno;
	uint16		flags;		/* GIN_SPLIT_ISLEAF and/or GIN_SPLIT_ISDATA */
	OffsetNumber offset;

	/*
	 * FOLLOWS:
	 *
	 * 1. if not leaf page, block numbers of the left and right child pages
	 * whose split this insertion finishes. As BlockIdData[2] (beware of adding
	 * fields before this that would make them not 16-bit aligned)
	 *
	 * 2. one of the following structs, depending on tree type.
	 *
	 * NB: the below structs are only 16-bit aligned when appended to a
	 * ginxlogInsert struct! Beware of adding fields to them that require
	 * stricter alignment.
	 */
} ginxlogInsert;

typedef struct
{
	bool		isDelete;
	IndexTupleData tuple;	/* variable length */
} ginxlogInsertEntry;

typedef struct
{
	OffsetNumber nitem;
	ItemPointerData items[1]; /* variable length */
} ginxlogInsertDataLeaf;

/* In an insert to an internal data page, the payload is a PostingItem */


#define XLOG_GIN_SPLIT	0x30

typedef struct ginxlogSplit
{
	RelFileNode node;
	BlockNumber lblkno;
	BlockNumber rblkno;
	BlockNumber rrlink;				/* right link, or root's blocknumber if root split */
	BlockNumber	leftChildBlkno;		/* valid on a non-leaf split */
	BlockNumber	rightChildBlkno;
	uint16		flags;

	/* follows: one of the following structs */
} ginxlogSplit;

/*
 * Flags used in ginxlogInsert and ginxlogSplit records
 */
#define GIN_INSERT_ISDATA	0x01	/* for both insert and split records */
#define GIN_INSERT_ISLEAF	0x02	/* .. */
#define GIN_SPLIT_ROOT		0x04	/* only for split records */

typedef struct
{
	OffsetNumber separator;
	OffsetNumber nitem;

	/* FOLLOWS: IndexTuples */
} ginxlogSplitEntry;

typedef struct
{
	OffsetNumber separator;
	OffsetNumber nitem;
	ItemPointerData rightbound;

	/* FOLLOWS: array of ItemPointers (for leaf) or PostingItems (non-leaf) */
} ginxlogSplitData;

#define XLOG_GIN_VACUUM_PAGE	0x40

typedef struct ginxlogVacuumPage
{
	RelFileNode node;
	BlockNumber blkno;
	OffsetNumber nitem;
	/* follows content of page */
} ginxlogVacuumPage;

#define XLOG_GIN_DELETE_PAGE	0x50

typedef struct ginxlogDeletePage
{
	RelFileNode node;
	BlockNumber blkno;
	BlockNumber parentBlkno;
	OffsetNumber parentOffset;
	BlockNumber leftBlkno;
	BlockNumber rightLink;
} ginxlogDeletePage;

#define XLOG_GIN_UPDATE_META_PAGE 0x60

typedef struct ginxlogUpdateMeta
{
	RelFileNode node;
	GinMetaPageData metadata;
	BlockNumber prevTail;
	BlockNumber newRightlink;
	int32		ntuples;		/* if ntuples > 0 then metadata.tail was
								 * updated with that many tuples; else new sub
								 * list was inserted */
	/* array of inserted tuples follows */
} ginxlogUpdateMeta;

#define XLOG_GIN_INSERT_LISTPAGE  0x70

typedef struct ginxlogInsertListPage
{
	RelFileNode node;
	BlockNumber blkno;
	BlockNumber rightlink;
	int32		ntuples;
	/* array of inserted tuples follows */
} ginxlogInsertListPage;

#define XLOG_GIN_DELETE_LISTPAGE  0x80

#define GIN_NDELETE_AT_ONCE 16
typedef struct ginxlogDeleteListPages
{
	RelFileNode node;
	GinMetaPageData metadata;
	int32		ndeleted;
	BlockNumber toDelete[GIN_NDELETE_AT_ONCE];
} ginxlogDeleteListPages;


/* ginutil.c */
extern Datum ginoptions(PG_FUNCTION_ARGS);
extern void initGinState(GinState *state, Relation index);
extern Buffer GinNewBuffer(Relation index);
extern void GinInitBuffer(Buffer b, uint32 f);
extern void GinInitPage(Page page, uint32 f, Size pageSize);
extern void GinInitMetabuffer(Buffer b);
extern int ginCompareEntries(GinState *ginstate, OffsetNumber attnum,
				  Datum a, GinNullCategory categorya,
				  Datum b, GinNullCategory categoryb);
extern int ginCompareAttEntries(GinState *ginstate,
					 OffsetNumber attnuma, Datum a, GinNullCategory categorya,
				   OffsetNumber attnumb, Datum b, GinNullCategory categoryb);
extern Datum *ginExtractEntries(GinState *ginstate, OffsetNumber attnum,
				  Datum value, bool isNull,
				  int32 *nentries, GinNullCategory **categories);

extern OffsetNumber gintuple_get_attrnum(GinState *ginstate, IndexTuple tuple);
extern Datum gintuple_get_key(GinState *ginstate, IndexTuple tuple,
				 GinNullCategory *category);

/* gininsert.c */
extern Datum ginbuild(PG_FUNCTION_ARGS);
extern Datum ginbuildempty(PG_FUNCTION_ARGS);
extern Datum gininsert(PG_FUNCTION_ARGS);
extern void ginEntryInsert(GinState *ginstate,
			   OffsetNumber attnum, Datum key, GinNullCategory category,
			   ItemPointerData *items, uint32 nitem,
			   GinStatsData *buildStats);

/* ginbtree.c */

typedef struct GinBtreeStack
{
	BlockNumber blkno;
	Buffer		buffer;
	OffsetNumber off;
	/* predictNumber contains predicted number of pages on current level */
	uint32		predictNumber;
	struct GinBtreeStack *parent;
} GinBtreeStack;

typedef struct GinBtreeData *GinBtree;

typedef struct GinBtreeData
{
	/* search methods */
	BlockNumber (*findChildPage) (GinBtree, GinBtreeStack *);
	BlockNumber (*getLeftMostChild) (GinBtree, Page);
	bool		(*isMoveRight) (GinBtree, Page);
	bool		(*findItem) (GinBtree, GinBtreeStack *);

	/* insert methods */
	OffsetNumber (*findChildPtr) (GinBtree, Page, BlockNumber, OffsetNumber);
	bool		(*placeToPage) (GinBtree, Buffer, OffsetNumber, void *, BlockNumber, XLogRecData **);
	Page		(*splitPage) (GinBtree, Buffer, Buffer, OffsetNumber, void *, BlockNumber, XLogRecData **);
	void	   *(*prepareDownlink) (GinBtree, Buffer);
	void		(*fillRoot) (GinBtree, Page, BlockNumber, Page, BlockNumber, Page);

	bool		isData;

	Relation	index;
	BlockNumber rootBlkno;
	GinState   *ginstate;		/* not valid in a data scan */
	bool		fullScan;
	bool		isBuild;

	/* Search key for Entry tree */
	OffsetNumber entryAttnum;
	Datum		entryKey;
	GinNullCategory entryCategory;

	/* Search key for data tree (posting tree) */
	ItemPointerData itemptr;
} GinBtreeData;

/* This represents a tuple to be inserted to entry tree. */
typedef struct
{
	IndexTuple	entry;			/* tuple to insert */
	bool		isDelete;		/* delete old tuple at same offset? */
} GinBtreeEntryInsertData;

/*
 * This represents an itempointer, or many itempointers, to be inserted to
 * a data (posting tree) leaf page
 */
typedef struct
{
	ItemPointerData *items;
	uint32		nitem;
	uint32		curitem;
} GinBtreeDataLeafInsertData;

/*
 * For internal data (posting tree) pages, the insertion payload is a
 * PostingItem
 */

extern GinBtreeStack *ginFindLeafPage(GinBtree btree, bool searchMode);
extern Buffer ginStepRight(Buffer buffer, Relation index, int lockmode);
extern void freeGinBtreeStack(GinBtreeStack *stack);
extern void ginInsertValue(GinBtree btree, GinBtreeStack *stack,
			   void *insertdata, GinStatsData *buildStats);

/* ginentrypage.c */
extern IndexTuple GinFormTuple(GinState *ginstate,
			 OffsetNumber attnum, Datum key, GinNullCategory category,
			 ItemPointerData *ipd, uint32 nipd, bool errorTooBig);
extern void GinShortenTuple(IndexTuple itup, uint32 nipd);
extern void ginPrepareEntryScan(GinBtree btree, OffsetNumber attnum,
					Datum key, GinNullCategory category,
					GinState *ginstate);
extern void ginEntryFillRoot(GinBtree btree, Page root, BlockNumber lblkno, Page lpage, BlockNumber rblkno, Page rpage);

/* gindatapage.c */
extern BlockNumber createPostingTree(Relation index,
				  ItemPointerData *items, uint32 nitems,
				  GinStatsData *buildStats);
extern void GinDataPageAddItemPointer(Page page, ItemPointer data, OffsetNumber offset);
extern void GinDataPageAddPostingItem(Page page, PostingItem *data, OffsetNumber offset);
extern void GinPageDeletePostingItem(Page page, OffsetNumber offset);
extern void ginInsertItemPointers(Relation index, BlockNumber rootBlkno,
					  ItemPointerData *items, uint32 nitem,
					  GinStatsData *buildStats);
extern GinBtreeStack *ginScanBeginPostingTree(Relation index, BlockNumber rootBlkno);
extern void ginDataFillRoot(GinBtree btree, Page root, BlockNumber lblkno, Page lpage, BlockNumber rblkno, Page rpage);
extern void ginPrepareDataScan(GinBtree btree, Relation index, BlockNumber rootBlkno);

/* ginscan.c */

/*
 * GinScanKeyData describes a single GIN index qualifier expression.
 *
 * From each qual expression, we extract one or more specific index search
 * conditions, which are represented by GinScanEntryData.  It's quite
 * possible for identical search conditions to be requested by more than
 * one qual expression, in which case we merge such conditions to have just
 * one unique GinScanEntry --- this is particularly important for efficiency
 * when dealing with full-index-scan entries.  So there can be multiple
 * GinScanKeyData.scanEntry pointers to the same GinScanEntryData.
 *
 * In each GinScanKeyData, nentries is the true number of entries, while
 * nuserentries is the number that extractQueryFn returned (which is what
 * we report to consistentFn).	The "user" entries must come first.
 */
typedef struct GinScanKeyData *GinScanKey;

typedef struct GinScanEntryData *GinScanEntry;

typedef struct GinScanKeyData
{
	/* Real number of entries in scanEntry[] (always > 0) */
	uint32		nentries;
	/* Number of entries that extractQueryFn and consistentFn know about */
	uint32		nuserentries;

	/* array of GinScanEntry pointers, one per extracted search condition */
	GinScanEntry *scanEntry;

	/* array of check flags, reported to consistentFn */
	bool	   *entryRes;

	/* other data needed for calling consistentFn */
	Datum		query;
	/* NB: these three arrays have only nuserentries elements! */
	Datum	   *queryValues;
	GinNullCategory *queryCategories;
	Pointer    *extra_data;
	StrategyNumber strategy;
	int32		searchMode;
	OffsetNumber attnum;

	/*
	 * Match status data.  curItem is the TID most recently tested (could be a
	 * lossy-page pointer).  curItemMatches is TRUE if it passes the
	 * consistentFn test; if so, recheckCurItem is the recheck flag.
	 * isFinished means that all the input entry streams are finished, so this
	 * key cannot succeed for any later TIDs.
	 */
	ItemPointerData curItem;
	bool		curItemMatches;
	bool		recheckCurItem;
	bool		isFinished;
}	GinScanKeyData;

typedef struct GinScanEntryData
{
	/* query key and other information from extractQueryFn */
	Datum		queryKey;
	GinNullCategory queryCategory;
	bool		isPartialMatch;
	Pointer		extra_data;
	StrategyNumber strategy;
	int32		searchMode;
	OffsetNumber attnum;

	/* Current page in posting tree */
	Buffer		buffer;

	/* current ItemPointer to heap */
	ItemPointerData curItem;

	/* for a partial-match or full-scan query, we accumulate all TIDs here */
	TIDBitmap  *matchBitmap;
	TBMIterator *matchIterator;
	TBMIterateResult *matchResult;

	/* used for Posting list and one page in Posting tree */
	ItemPointerData *list;
	uint32		nlist;
	OffsetNumber offset;

	bool		isFinished;
	bool		reduceResult;
	uint32		predictNumberResult;
}	GinScanEntryData;

typedef struct GinScanOpaqueData
{
	MemoryContext tempCtx;
	GinState	ginstate;

	GinScanKey	keys;			/* one per scan qualifier expr */
	uint32		nkeys;

	GinScanEntry *entries;		/* one per index search condition */
	uint32		totalentries;
	uint32		allocentries;	/* allocated length of entries[] */

	bool		isVoidRes;		/* true if query is unsatisfiable */
} GinScanOpaqueData;

typedef GinScanOpaqueData *GinScanOpaque;

extern Datum ginbeginscan(PG_FUNCTION_ARGS);
extern Datum ginendscan(PG_FUNCTION_ARGS);
extern Datum ginrescan(PG_FUNCTION_ARGS);
extern Datum ginmarkpos(PG_FUNCTION_ARGS);
extern Datum ginrestrpos(PG_FUNCTION_ARGS);
extern void ginNewScanKey(IndexScanDesc scan);

/* ginget.c */
extern Datum gingetbitmap(PG_FUNCTION_ARGS);

/* ginvacuum.c */
extern Datum ginbulkdelete(PG_FUNCTION_ARGS);
extern Datum ginvacuumcleanup(PG_FUNCTION_ARGS);

/* ginbulk.c */
typedef struct GinEntryAccumulator
{
	RBNode		rbnode;
	Datum		key;
	GinNullCategory category;
	OffsetNumber attnum;
	bool		shouldSort;
	ItemPointerData *list;
	uint32		maxcount;		/* allocated size of list[] */
	uint32		count;			/* current number of list[] entries */
} GinEntryAccumulator;

typedef struct
{
	GinState   *ginstate;
	long		allocatedMemory;
	GinEntryAccumulator *entryallocator;
	uint32		eas_used;
	RBTree	   *tree;
} BuildAccumulator;

extern void ginInitBA(BuildAccumulator *accum);
extern void ginInsertBAEntries(BuildAccumulator *accum,
				   ItemPointer heapptr, OffsetNumber attnum,
				   Datum *entries, GinNullCategory *categories,
				   int32 nentries);
extern void ginBeginBAScan(BuildAccumulator *accum);
extern ItemPointerData *ginGetBAEntry(BuildAccumulator *accum,
			  OffsetNumber *attnum, Datum *key, GinNullCategory *category,
			  uint32 *n);

/* ginfast.c */

typedef struct GinTupleCollector
{
	IndexTuple *tuples;
	uint32		ntuples;
	uint32		lentuples;
	uint32		sumsize;
} GinTupleCollector;

extern void ginHeapTupleFastInsert(GinState *ginstate,
					   GinTupleCollector *collector);
extern void ginHeapTupleFastCollect(GinState *ginstate,
						GinTupleCollector *collector,
						OffsetNumber attnum, Datum value, bool isNull,
						ItemPointer ht_ctid);
extern void ginInsertCleanup(GinState *ginstate,
				 bool vac_delay, IndexBulkDeleteResult *stats);

/* ginpostinglist.c */
extern uint32 ginMergeItemPointers(ItemPointerData *dst,
					 ItemPointerData *a, uint32 na,
					 ItemPointerData *b, uint32 nb);


/*
 * Merging the results of several gin scans compares item pointers a lot,
 * so we want this to be inlined. But if the compiler doesn't support that,
 * fall back on the non-inline version from itemptr.c. See STATIC_IF_INLINE in
 * c.h.
 */
#ifdef PG_USE_INLINE
static inline int
ginCompareItemPointers(ItemPointer a, ItemPointer b)
{
	uint64 ia = (uint64) a->ip_blkid.bi_hi << 32 | (uint64) a->ip_blkid.bi_lo << 16 | a->ip_posid;
	uint64 ib = (uint64) b->ip_blkid.bi_hi << 32 | (uint64) b->ip_blkid.bi_lo << 16 | b->ip_posid;

	if (ia == ib)
		return 0;
	else if (ia > ib)
		return 1;
	else
		return -1;
}
#else
#define ginCompareItemPointers(a, b) ItemPointerCompare(a, b)
#endif   /* PG_USE_INLINE */

#endif   /* GIN_PRIVATE_H */
