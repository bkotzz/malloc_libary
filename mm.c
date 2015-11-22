/*
 *
 * This implementation uses segregated free lists.  A global array
 * contains pointer to 15 distinct free lists, where each list holds
 * free blocks in a certain range.
 *
 * Index 0 - 32 byte blocks
 * Index 1 - 33-64 byte blocks
 * Index 2 - 65-128 byte blocks
 * ...
 * Index 14 - 32769-65536 byte blocks
 *
 * The format of an allocated block is the following:
 * [8 byte header][16 byte aligned data][8 byte footer]
 *
 * The format of a free block is the following:
 * [8 byte header][8 byte pointer to previous block][unused][8 byte pointer to next block][8 byte footer]
 *
 * The header and footer fields are identical, and contain
 * the size of the entire block.  The least significant bit
 * of the size refers to the allocation of the block (0 == free, 1 == allocated)
 * The pointers point to the header of the adjacent block in the free list.
 *
 * When a block is freed, it is added to the front of the existing free list.
 * When a block is allocated, it is removed from the free list.
 *
 * When allocating a block that is bigger than the allocation request, the block is split,
 * and the remainder is added to a free list.
 *
 * Before any block is added to a free list, it is coalesced with its
 * neighboring blocks, if possible.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Kotopoulos",
    /* First member's full name */
    "Bradley Kotsopoulos",
    /* First member's email address */
    "brad.kotsopoulos@mail.utoronto.ca",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/*************************************************************************
 * Basic Constants and Macros
 * You are not required to use these macros but may find them helpful.
*************************************************************************/

static const unsigned int arrayLength = 15;		// number of free lists
static char* array[15];							// array of free list pointers
static char* heapStart;							// pointer to first byte used on heap

enum Status
{
	FREE = 0,
	ALLOCATED
};

/**********************************************************
 * HELPER FUNCTIONS
 **********************************************************/


// Round up a block size to the nearest
// 16 bytes
static unsigned int roundUp(size_t size)
{
	if( 0 == size % 16 )
		return (unsigned int) size;

	size &= ~15;
	size += 16;
	return (unsigned int) size;
}


// given size, multiple of 16 between 2^5 and 2^19,
// find the corresponding array index, by rounding
// the size up to the next power of 2, corresponding
// to the free list pointed to by array[index]
static unsigned int getIndex(unsigned int size)
{
	assert(size >= 32);

	if( size == 32 )
		return 0;
	else if( size <= 64 )
		return 1;
	else if( size <= 128 )
		return 2;
	else if( size <= 256 )
		return 3;
	else if( size <= 512 )
		return 4;
	else if( size <= 1024 )
		return 5;
	else if( size <= 2048 )
		return 6;
	else if( size <= 4096 )
		return 7;
	else if( size <= 8192 )
		return 8;
	else if( size <= 16384 )
		return 9;
	else if( size <= 32768 )
		return 10;
	else if( size <= 65536 )
		return 11;
	else if( size <= 131072 )
		return 12;
	else if( size <= 262144 )
		return 13;
	else // if( size <= 524288 )
		return 14;

}


// given a pointer to a block header
// or footer, zero out the allocated
// bit and return the size
static unsigned int getSize(char* bp)
{
	return *(uintptr_t*)bp & ~1;
}


// given a pointer to a block header
// or footer, return the value of
// the lowest bit, corresponding to
// the allocation of the block
static enum Status getAlloc(char* bp)
{
	if( 0 == ( (*(uintptr_t*)bp) & 1 ) )
		return FREE;
	else
		return ALLOCATED;
}


// given a pointer to the first byte in a block,
// set the header and footer to the size
// and allocation parameter
static void setSizeAlloc(char* bp, unsigned int size, enum Status alloc)
{
	char* lastHeader = bp + size - 8;
	if( ALLOCATED == alloc )
	{
		*(uintptr_t*)bp = (size | 1);
		*(uintptr_t*)lastHeader = (size | 1);
	}
	else if( FREE == alloc )
	{
		*(uintptr_t*)bp = (size & ~1);
		*(uintptr_t*)lastHeader = (size & ~1);
	}
}


// given a pointer to the first byte in a block
// return a pointer to the previous block in the free list
static char* getPrev(char* bp)
{
	unsigned long long pointer = *((uintptr_t*)bp + 1);
	return (char*) pointer;
}


// given a pointer to the first byte in a block,
// set the pointer to the previous block in the list
static void setPrev(char* bp, char* prev)
{
	*((uintptr_t*)bp + 1) = (unsigned long long)prev;
}


// given a pointer to the first byte in a block
// return a pointer to the next block in the free list
static char* getNext(char* bp)
{
	unsigned int size = getSize(bp);
	bp += size - 16;
	unsigned long long pointer = *(uintptr_t*)bp;
	return (char*) pointer;
}


// given a pointer to the first byte in a block,
// set the pointer to the next block in the list
static void setNext(char* bp, char* next)
{
	unsigned int size = getSize(bp);
	bp += size - 16;
	*(uintptr_t*)bp = (unsigned long long)next;
}


// remove the block pointed to by bp
// from the free list by skipping over it
static void removeFromList(char* bp)
{
	unsigned int size = getSize(bp);
	unsigned int index = getIndex(size);
	char* nextPtr = getNext(bp);
	char* prevPtr = getPrev(bp);

	if( !prevPtr )
	{
		array[index] = nextPtr;
	}
	else
	{
		setNext(prevPtr, nextPtr);
	}
	if( nextPtr )
	{
		setPrev(nextPtr, prevPtr);
	}

	setNext(bp, NULL);
	setPrev(bp, NULL);
}


/**********************************************************
 * mm_init
 * Initialize the heap to ensure 16 byte alignment,
 * and initialize the free lists to empty
 **********************************************************/
 int mm_init(void)
 {
	 int i = 0;
	 for(; i < arrayLength; i++)
	 {
		 array[i] = NULL;
	 }

	 // want to start heap where it is 8 byte aligned
	 // but not 16 byte aligned, so that the data section
	 // of an allocated block will be 16 byte aligned, since
	 // the header at the front of the block takes 8 bytes
	 // note: all blocks are multiples of 16 bytes, so
	 //		  if the first block is aligned properly,
	 //		  all subsequent blocks will be as well
	 unsigned long long nextHeapSpot = (unsigned long long)mem_heap_hi() + 1;
	 while( (nextHeapSpot & 0xF) != 0x8 )
	 {
		 mem_sbrk(8);
		 nextHeapSpot = (unsigned long long)mem_heap_hi() + 1;
	 }

	 heapStart = (char*) nextHeapSpot;
	 return 0;
 }

/**********************************************************
 * coalesce
 * Covers the 4 cases discussed in the text:
 * - both neighbours are allocated
 * - the next block is available for coalescing
 * - the previous block is available for coalescing
 * - both neighbours are available for coalescing
 *
 * Returns a pointer to the first byte in the largest
 * contiguous free block possible, where the entire
 * block has been remove from all possible free lists
 * and has the correct size set
 **********************************************************/
void* coalesce(void *bp)
{
	unsigned int size = getSize(bp);

	char* prevFooter = bp - 8;
	char* nextHeader = bp + size;

	enum Status nextAlloc, prevAlloc;

	if( mem_heap_hi() == nextHeader - 1 )
	{
		// if the next block is outside of the heap,
		// treat it as allocated and don't check it
		nextAlloc = ALLOCATED;
	}
	else
	{
		nextAlloc = getAlloc(nextHeader);
	}

	if( heapStart == bp )
	{
		// if the previous block is below the
		// start of the heap, treat it as allocated
		// and don't check it
		prevAlloc = ALLOCATED;
	}
	else
	{
		prevAlloc = getAlloc(prevFooter);
	}


	if( ALLOCATED == prevAlloc && ALLOCATED == nextAlloc )
	{
		return bp;
	}
	else if( FREE == prevAlloc && ALLOCATED == nextAlloc )
	{
		unsigned int prevSize = getSize(prevFooter);
		unsigned int totalSize = prevSize + size;
		char* prevHeader = bp - prevSize;

		// STEP 1: Remove Previous block from respective list
		removeFromList(prevHeader);

		// STEP 2: Set the size in both blocks to the total size
		setSizeAlloc(prevHeader, totalSize, FREE);

		return prevHeader;
	}
	else if( ALLOCATED == prevAlloc && FREE == nextAlloc )
	{
		unsigned int nextSize = getSize(nextHeader);
		unsigned int totalSize = nextSize + size;

		// STEP 1: Remove Next block from respective list
		removeFromList(nextHeader);

		// STEP 2: Set the size in both blocks to the total size
		setSizeAlloc(bp, totalSize, FREE);

		return bp;
	}
	else
	{
		unsigned int prevSize = getSize(prevFooter);
		unsigned int nextSize = getSize(nextHeader);
		unsigned int totalSize = prevSize + size + nextSize;
		char* prevHeader = bp - prevSize;

		// STEP 1: Remove Previous and Next block from respective list
		removeFromList(prevHeader);
		removeFromList(nextHeader);

		// STEP 2: Set the size in all three blocks to the total size
		setSizeAlloc(prevHeader, totalSize, FREE);

		return prevHeader;
	}
}

/**********************************************************
 * extend_heap
 * Extend the heap by one block, where the size of the block
 * corresponds to the largest block allowable in the free list
 * noted by the index passed in
 *
 * returns a pointer to the beginning of the last new block
 * created
 **********************************************************/
char* extend_heap(unsigned int index)
{
	// number of blocks to extend the heap by (for small requests
	// over extend the heap, to save from calling mem_sbrk too
	// many times)
	unsigned int numBlocks = (index < 3) ? 16 : 1;

	unsigned int blockSize = 1 << (index + 5);

    char *bp;

    if ( (bp = mem_sbrk(numBlocks * blockSize)) == (void *)-1 )
        return NULL;

    array[index] = bp;

    int i = 0;
    char* iter = bp;
    char* prevPtr = NULL;
    for(; i < numBlocks; i++)
    {
    	// for each of the numBlocks that we just created,
    	// set the next and previous pointers to maintain
    	// the linked list, and set the size/allocated field
    	setSizeAlloc(iter, blockSize, FREE);
    	setPrev(iter, prevPtr);
    	char* nextPtr = ( (numBlocks - 1) == i) ? NULL : iter + blockSize;
    	setNext(iter, nextPtr);
    	prevPtr = iter;
    	iter += blockSize;
    }

    return prevPtr;
}


/**********************************************************
 * find_fit
 * Traverse the corresponding free list, searching for a
 * block to fit totalSize
 * Return NULL if no free blocks can handle that size
 **********************************************************/
void* find_fit(unsigned int totalSize, unsigned int arrayIndex)
{
    char* iter = (char*) array[arrayIndex];
    while( iter && totalSize > getSize(iter) )
    {
    	iter = getNext(iter);
    }

    // Can either be NULL, if no suitable block found
    // or a pointer to the suitable block
    return iter;
}

/**********************************************************
 * place
 * Given a block in a free list, we want to prepare this
 * block to be returned by mm_malloc
 *
 * Remove the block from its free list, set it to allocated,
 * clear the next/previous pointers, and return a pointer
 * to just the data portion (skip the header)
 *
 * If their is enough unneeded space in the chosen block
 * to make a new block, split the two blocks, and "free"
 * the unused portion so that it is stored appropriately
 **********************************************************/
char* place(char* bp, unsigned int totalSizeNeeded, unsigned int arrayIndex)
{
	// marks the current block as allocated
	// returns a pointer to the data section of this block

	unsigned int blockSize = getSize(bp);

	// STEP 1: remove from list
	removeFromList(bp);

	if( totalSizeNeeded + 32 <= blockSize )
	{
		// then we split it up and free
		unsigned int extraSize = blockSize - totalSizeNeeded;
		blockSize = totalSizeNeeded;

		char* toFree = bp + blockSize;
		setSizeAlloc(toFree, extraSize, ALLOCATED);
		setSizeAlloc(bp, blockSize, ALLOCATED);

		setNext(bp, NULL);
		setPrev(bp, NULL);
		setNext(toFree, NULL);
		setPrev(toFree, NULL);

		// free the portion of the block that
		// isn't needed
		mm_free(toFree + 8);
	}
	else
	{
		// STEP 2: clear pointer fields in block, set to allocated
		setSizeAlloc(bp, blockSize, ALLOCATED);
		setNext(bp, NULL);
		setPrev(bp, NULL);
	}

	// STEP 3: return pointer to data segment only
	return bp + 8;
}

/**********************************************************
 * mm_free
 * Coalesce the block with its neighbouring blocks, and
 * insert it at the beginning of appropriate free list
 **********************************************************/
void mm_free(void *bp)
{
    if(bp == NULL){
      return;
    }

    char* blockPointer = (char*)bp - 8;

    // call coalesce, block pointer may now point to header of bigger block
    blockPointer = coalesce(blockPointer);

    unsigned int blockSize = getSize(blockPointer);

    unsigned int arrayIndex = getIndex(blockSize);
    char* oldHead = array[arrayIndex];

    setPrev(blockPointer, NULL);
    array[arrayIndex] = blockPointer;

    setNext(blockPointer, oldHead);
    if(oldHead)
    	setPrev(oldHead, blockPointer);

    setSizeAlloc(blockPointer, blockSize, FREE);
}


/**********************************************************
 * mm_malloc
 * Translate the request size to a block size, and determine
 * which free list this corresponds to
 *
 * Search the free list for the first block that fits,
 * moving to the next higher list if no blocks are found
 *
 * If a fit is found, the block is prepared by place()
 *
 * If no fit is found in any of the lists, the heap is
 * extended to meet the request
 **********************************************************/
void *mm_malloc(size_t size)
{
    /* Ignore spurious requests */
    if ( 0 == size )
        return NULL;

    unsigned int roundedSize = roundUp(size); 		// round to nearest 16
    unsigned int totalSize = roundedSize + 16; 		// how much we need in total
    unsigned int arrayIndex = getIndex(totalSize);  // find appropriate list index for size

    for(; arrayIndex < arrayLength; arrayIndex++)
    {
        if( NULL == array[arrayIndex] )
        {
        	continue;
        }

        // guaranteed that free list has entries in it
        // but maybe they don't fit?
        // Search the free list for a fit
        char* bp = find_fit(totalSize, arrayIndex);
        if ( bp )
        {
        	return place(bp, totalSize, arrayIndex);
        }

    }

    // either all lists are empty, or we couldn't find
    // any blocks that fit, either way,
    // just force allocate for this request

    // STEP 0: Find original array index
    arrayIndex = getIndex(totalSize);

    // STEP 1: Save original list for original index
    void* oldBeginning = array[arrayIndex];

    // STEP 2: Call extend heap
    void* newEnd = extend_heap(arrayIndex);
    if( !newEnd )
    	return NULL;

    // STEP 3: Append old list to new list
    // 		   new->next = old
    //		   old->prev = new
    setNext(newEnd, oldBeginning);
    if( oldBeginning )
    	setPrev(oldBeginning, newEnd);

    // STEP 4: find_fit and place
    char* bp = find_fit(totalSize, arrayIndex);
    if ( bp )
    {
        return place(bp, totalSize, arrayIndex);
    }
    else
    {
    	assert(0);
    	return NULL;
    }

}

/**********************************************************
 * mm_realloc
 * If the new data size is smaller than the old data size,
 * the pointer is immediately returned, as the block size
 * doesn't change, but we not have some unused bytes
 *
 * If the new data size is larger, we try to join this
 * allocated block with any neighbouring free blocks.  If
 * this new coalesced block is large enough, we shift the
 * data to be at the start of the new block, and return
 *
 * If this new coalesced block still can't meet the request,
 * we just free this block, and allocated a new block that
 * is large enough, and copy over the old data to the new block.
 *********************************************************/
void *mm_realloc(void *ptr, size_t size)
{
	/* If size == 0 then this is just free, and we return NULL. */
	if(size == 0){
	  mm_free(ptr);
	  return NULL;
	}
	/* If oldptr is NULL, then this is just malloc. */
	if (ptr == NULL)
	  return (mm_malloc(size));

	char* blockHeader = (char*)ptr - 8;
	unsigned int blockSize = getSize(blockHeader);
	unsigned int oldDataSize = blockSize - 16;

	unsigned int newDataSize = (unsigned int)size;

	if( newDataSize == oldDataSize )
		return ptr;

	else if( newDataSize < oldDataSize )
	{
		if( (oldDataSize - newDataSize) >= 32 )
		{
			// can split off the new unused bytes
			// and make a new block, but not worthwhile
			// performance-wise
			return ptr;
		}
		else
		{
			// not releasing enough bytes to make a new block
			return ptr;
		}
	}
	else //( newDataSize > oldDataSize )
	{
		char* biggestBlock = coalesce(blockHeader);
		unsigned int newSize = getSize(biggestBlock);
		setSizeAlloc(biggestBlock, newSize, ALLOCATED);

		if( newSize - 16 >= newDataSize )
		{
			// can split off the unused bytes
			// and make a new block
			memmove(biggestBlock + 8, ptr, oldDataSize);

			return biggestBlock + 8;
		}
		else
		{
			// just malloc/free
			char* newBlock = (char*)mm_malloc(newDataSize);
			if( !newBlock )
				return NULL;

			memcpy(newBlock, ptr, oldDataSize);

			mm_free(biggestBlock + 8);

			return newBlock;
		}
	}

}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistant.
 *********************************************************/
int mm_check(void)
{
	char* pBlock;
	char* heapHigh = mem_heap_hi();

	// Check if every block in free list marked as free?
	// For each free list, iterate through all blocks
	// in the free list, checking that each one is
	// marked as FREE
	//
	// This also serves to check that all of the size fields
	// and next/previous pointers in the free lists are accurate,
	// or else we wouldn't be able to successfully traverse
	// the lists
	unsigned int i = 0;
	for(; i < arrayLength; i++)
	{
		// Iterate through each free list until we get to the
		// last block in the list
		pBlock = array[i];
		char* saveFirstBlock = pBlock;
		while( pBlock && getNext(pBlock) )
		{
			if( ALLOCATED == getAlloc(pBlock) )
				return 0;

			pBlock = getNext(pBlock);
		}

		// Once we reach the last block, we want to see if we
		// can then iterate backwards through the list
		// and reach the first block in the list again
		//
		// This proves that all of the previous pointers are
		// correct, or else we would access memory out of bounds
		// or dereference a NULL pointer before arriving back
		// where we started

		while( pBlock && pBlock != saveFirstBlock )
		{
			pBlock = getPrev(pBlock);
		}

		if( pBlock != saveFirstBlock )
			return 0;
	}

	// Iterate through the entire heap from start to finish,
	// one block at a time, checking multiple things
	// (see comments inside the loop)
	// pointers in heap block point to valid heap addresses?
	// contiguous free blocks that escaped coalescing?
	// every free block in free list?
	pBlock = heapStart;
	unsigned int consecutiveFreeBlocks = 0;

	while( heapHigh != pBlock - 1)
	{
		if( FREE == getAlloc(pBlock) )
		{
			consecutiveFreeBlocks++;

			// For every free block we find in the heap,
			// use the size to determine which free list
			// it should be on, and iterate through that
			// free list, looking for this block.
			//
			// If we reach the end of the free list without
			// finding it, return an error
			unsigned int index = getIndex(getSize(pBlock));
			char* pFreeListIter = array[index];

			while( pFreeListIter && pFreeListIter != pBlock )
				pFreeListIter = getNext(pFreeListIter);

			if( pFreeListIter != pBlock )
				return 0;
		}

		// If we see 2 consecutive free blocks, they somehow
		// escaped coalescing, and we return an error
		if( 2 == consecutiveFreeBlocks )
			return 0;

		// If, at any point, our pointer points to memory
		// outside of the heap, return an error
		//
		// This would indicate that a size field of a
		// free or allocated block is incorrect
		if( pBlock < heapStart || pBlock > heapHigh )
			return 0;

		// Check to see that the size field in the
		// header of a block matches the field in the
		// footer
		unsigned int size = getSize(pBlock);
		if( size != getSize(pBlock + size - 8) )
			return 0;

		// Check to see that the allocated field in the
		// header of a block matches the field in the
		// footer
		enum Status status = getAlloc(pBlock);
		if( status != getAlloc(pBlock + size - 8) )
			return 0;

		pBlock = getNext(pBlock);
	}

	return 1;
}

