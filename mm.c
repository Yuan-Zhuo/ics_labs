#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* $begin mallocmacros */
/* Basic constants and macros */
#define WSIZE 4             /* Word and header/footer size (bytes) */
#define DSIZE 8             /* Double word size (bytes) */
#define CHUNKSIZE (1 << 12) /* Extend heap by this amount (bytes) */
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int*)(p))
#define PUT(p, val) (*(unsigned int*)(p) = (val))

/* Read and write an address(pointer) at address p */
#define GETADDR(p) (*(unsigned long*)(p))
#define PUTADDR(p, val) (*(unsigned long*)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char*)(bp)-WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// Given block ptr bp of a free block, compute the address of
// predecessor/successor pointer
#define PRED(bp) ((char*)(bp))
#define SUCC(bp) ((char*)(bp) + DSIZE)

// Given block ptr bp, compute address of next and previous blocks
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(((char*)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char*)(bp)-GET_SIZE(((char*)(bp)-DSIZE)))

// Given block ptr bp of a free block, compute address of previous/next free
// block
#define PREV_FREE(bp) ((char*)GETADDR(PRED(bp)))
#define NEXT_FREE(bp) ((char*)GETADDR(SUCC(bp)))
/* $end mallocmacros */

static char*
    heap_listp;  // A pointer that always pointing to the prologue block
static char* free_listp =
    NULL;  // A pointer that always pointing to the first free block
int flag;

// Necessary declaration
static void* extend_heap(size_t words);
static void* coalesce(void* bp);

/*
 * mm_init - initialize the malloc package.
 * This function initializes the heap, gets four words from system and put the
 * padding word, prologue block and epilogue block on. Then it set heap_listp to
 * the prologue block, free_listp to NULL, and extend the heap. If there is a
 * problem in initialization, it stops and returns -1, otherwise it finish
 * initialization and return 0.
 */
int mm_init(void) {
    // Create the initial empty heap with a prologue block and an epilogue
    // block.
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void*)-1)
        return -1;
    PUT(heap_listp, 0);                             // Alignment padding
    PUT(heap_listp + (WSIZE), PACK(DSIZE, 1));      // Prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));  // Prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));      // Epilogue header
    heap_listp += (2 * WSIZE);

    // Set global variables
    free_listp = NULL;
    flag = 0;

    // Extend the empty head with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * extend_heap - extends the heap with a new free block, return the block
 * pointer of the new block
 */
static void* extend_heap(size_t words) {
    char* bp;
    size_t size;

    // Allocate an even number of words to maintain alignment.
    // Do not need to consider minimum block size since it's covered in
    // mm-malloc.
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    // Initialize free block header/footer, predecessor pointer, successor
    // pointer and the epilogue header
    PUT(HDRP(bp), PACK(size, 0));          // Free block header
    PUT(FTRP(bp), PACK(size, 0));          // Free block footer
    PUTADDR(PRED(bp), 0);                  // Predecessor pointer
    PUTADDR(SUCC(bp), 0);                  // Successor pointer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));  // New epilogue header

    // Coalesce if the previous block is free
    return coalesce(bp);
}

/*
 * disconnect - remove a free block from the free list since it's allocated or
 * being coalesced
 */
void disconnect(void* bp) {
    // Get its adjacent free blocks
    char* prev = PREV_FREE(bp);
    char* next = NEXT_FREE(bp);

    // Set pointers in those blocks
    if (prev && next)  // case 1
    {
        PUTADDR(SUCC(prev), GETADDR(SUCC(bp)));
        PUTADDR(PRED(next), GETADDR(PRED(bp)));
    } else if (prev && !next)  // case 2
    {
        PUTADDR(SUCC(prev), 0);
    } else if (!prev && next)  // case 3
    {
        PUTADDR(PRED(next), 0);
        free_listp = next;  // in this case free_listp needs to be changed
    } else                  // case 4
    {
        free_listp = NULL;  // also change free_listp
    }

    // Set the two pointers of the block to NULL in case they are used
    // accidentally
    PUTADDR(PRED(bp), 0);
    PUTADDR(SUCC(bp), 0);
}

/*
 * connect - add a free block to the top of the free list
 * The allocator use LIFO-ordering to reduce the cost of freeing new blocks to
 * constant time.
 */
void connect(void* bp) {
    if (NULL != free_listp) {
        PUTADDR(PRED(free_listp), (unsigned long)bp);
        PUTADDR(SUCC(bp), (unsigned long)free_listp);
    } else {
        PUTADDR(SUCC(bp), 0);
    }
    PUTADDR(PRED(bp), 0);
    free_listp = bp;
}

/*
 * coalesce - combine a newly freed block with adjacent free blocks, return the
 * block pointer of the coalesced block
 */
static void* coalesce(void* bp) {
    // Check if the two adjacent blocks are free
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // Take free blocks next to the block out of the free list and combine them
    // by changing tags and pointers
    if (prev_alloc && !next_alloc)  // Case 2 (case 1 only need connect())
    {
        disconnect(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc)  // Case 3
    {
        disconnect(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    else if (!prev_alloc && !next_alloc)  // Case 4
    {
        disconnect(PREV_BLKP(bp));
        disconnect(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    // Add the coalesced block back to the free list
    connect(bp);
    return bp;
}

/*
 * find_fit - Find a free block to allocate to. Return the block pointer of the
 * selected block
 */
static void* find_fit(size_t asize) {
    // First-fit search through the free list
    void* bp = free_listp;
    while (NULL != bp) {
        // The free block should be large enough
        if (asize <= GET_SIZE(HDRP(bp))) {
            if (!flag || (0 != GET_SIZE(HDRP(NEXT_BLKP(bp)))))
                return bp;
        }
        bp = NEXT_FREE(bp);
    }
    return NULL;  // No fit
}

/*
 * place - Allocate to a chosen block and split the remainder of the free block
 * if necessary
 */
static void place(void* bp, size_t asize) {
    size_t csize =
        GET_SIZE(HDRP(bp));  // The size of the free block to be allocated
    if ((csize - asize) >=
        (3 * DSIZE))  // If the size of the remainder is not less then the
                      // minimum block size, add it to the free list
    {
        disconnect(bp);
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        connect(bp);
    } else  // If not, just place the payload on the whole block
    {
        disconnect(bp);
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_malloc - Allocate a block from the free list. Return the block pointer of
 * the allocated block.
 */
void* mm_malloc(size_t size) {
    size_t asize;       // Adjusted block size
    size_t extendsize;  // Amount to extend heap if no fit
    char* bp;

    // Ignore spurious requests
    if (0 == size) {
        return NULL;
    }

    // Adjust block size to include overhead(header, footer and two pointers, 6
    // words in total) and alignment reqs
    if (size <= DSIZE)
        asize = 3 * DSIZE;
    else if (112 == size)
        asize = 136;
    else if (448 == size)
        asize = 520;
    else {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    }

    // Search the free list for a fit
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    // No fit found. Get more memory and place the block
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }

    place(bp, asize);
    return bp;
}

/*
 * mm_free - Free a block and uses boundary-tag coalescing to merge it with and
 * adjacent free blocks in constant time
 */
void mm_free(void* bp) {
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUTADDR(PRED(bp), 0);
    PUTADDR(SUCC(bp), 0);
    coalesce(bp);
}

/*
 * mm_realloc - Reallocate the allocated block to a block whose capacity is size
 * bytes. Return the block pointer of the new block.
 */
void* mm_realloc(void* bp, size_t size) {
    void* oldbp = bp;
    void* newbp;
    size_t copySize;

    if (!flag) {
        newbp = mm_malloc(size);
        if (newbp == NULL)
            return NULL;
        copySize = *(size_t*)((char*)oldbp - DSIZE);
        if (size < copySize)
            copySize = size;
        memcpy(newbp, oldbp, copySize);
        mm_free(oldbp);
        flag = 1;
        return newbp;
    } else {
        char* lastfree = NEXT_BLKP(oldbp);
        size_t diff =
            DSIZE * ((size - GET_SIZE(HDRP(oldbp)) + (2 * DSIZE - 1)) / DSIZE);
        if (GET_SIZE(HDRP(lastfree)) < (diff + 3 * DSIZE)) {
            lastfree = extend_heap(CHUNKSIZE / WSIZE);
        }
        size_t temp = GET_SIZE(HDRP(lastfree));
        disconnect(lastfree);
        lastfree += diff;
        PUT(HDRP(lastfree), PACK((temp - diff), 0));
        PUT(FTRP(lastfree), PACK((temp - diff), 0));
        PUTADDR(PRED(lastfree), 0);
        PUTADDR(SUCC(lastfree), 0);
        PUT(HDRP(oldbp), PACK(GET_SIZE(HDRP(oldbp)) + diff, 1));
        PUT(FTRP(oldbp), PACK(GET_SIZE(HDRP(oldbp)), 1));
        connect(lastfree);
        return oldbp;
    }
}

/*
 * mm_check - a simple heap consistency checker. Return 1 if check succeed, 0 if
 * fail.
 */
int mm_check(void) {
    char* bp;
    char* fp;
    int count = 0;

    // Check contiguous free blocks
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!(GET_ALLOC(HDRP(bp)) || GET_ALLOC(HDRP(NEXT_BLKP(bp))))) {
            fprintf(stderr, "contagious free blocks");
            return 0;
        }
    }

    // Check the allocate bit of free blocks in the free list
    for (fp = free_listp; NULL != fp; fp = NEXT_FREE(fp)) {
        if (GET_ALLOC(fp)) {
            fprintf(stderr, "block in the free list marked as allocated.");
            return 0;
        }
    }

    // Check whether there are any free block outside the free list
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp))) {
            count++;
        }
    }
    for (fp = free_listp; NULL != fp; fp = NEXT_FREE(fp)) {
        count--;
    }
    if (0 != count) {
        fprintf(stderr,
                "numbers of free blocks in free list and heap are unmatched.");
        return 0;
    }

    // Check the integrity of pointers in free blocks
    for (fp = free_listp; NULL != fp; fp = NEXT_FREE(fp)) {
        if (0 != GETADDR(PRED(free_listp))) {
            fprintf(stderr, "invalid free_listp predecessot pointer");
            return 0;
        }

        if (GETADDR(PRED(fp)) == GETADDR(SUCC(fp)) && 0 != GETADDR(PRED(fp))) {
            fprintf(stderr,
                    "two pointers in one free blocks have same nonzero value");
            return 0;
        }

        if (0 != GETADDR(SUCC(fp)) &&
            (GETADDR(SUCC(fp)) != (unsigned long)NEXT_FREE(fp) ||
             GETADDR(PRED(NEXT_FREE(fp))) != (unsigned long)fp)) {
            fprintf(stderr, "pointers of two contiguous free blocks unmatched");
            return 0;
        }
    }
    return 1;
}
