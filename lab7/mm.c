/*
 * malloc lab:
 * 1. baesed on the implement on csapp book
 * 2. segregated free list
 * 3. immediate coalescing
 * 4. implicit free list
 * 5. a little optimization for traces
 * 6. mm_check added
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

#define ALIGNMENT 8

/* minimun block size is 16 bytes (8 payload and 4+4 header and footer)*/
#define MIN_BLK_SZ 16

/* rounds UP to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

/* rounds DOWN to the nearest multiple of ALIGNMENT */
#define DOWN_ALIGN(p) ((unsigned long)(p) & ~0x7ul)

/* number of small block groups - 15 */
#define N_SBLK 15

/* number of large block groups - 9 */
#define N_LBLK 12

/* initial heap: 512 bytes */
#define INIT_SIZE (512)

/* cast to void* pointer */
#define VPTR(p) ((void*)(p))

/* dereference a pointer as uint* */
#define EVAL4B(p) (*((unsigned int*)(p)))

/* dereference "next" pointer */
#define L_NEXT(p) (*((void**)(p)))

/* dereference "prev" pointer */
#define L_PREV(p) (*((void**)(p) + 1))

/* dereference header */
#define HEADER(p) (*(((unsigned int*)(p)) - 1))

/* small block index from block size */
#define SBLK_IDX(sz) (((sz) >> 3) - 2)

/* small block size from index */
#define SBLK_SZ(idx) (((idx) + 2u) << 3)

/* get payload size of a block */
#define PAYLD_SZ(p) (HEADER(p) & ~7u)

/* get size of a block (include header, footer) */
#define BLK_SZ(p) ((HEADER(p) & ~7u) + 8)

#define TAG_BLOCK(blk, num) (HEADER(blk) |= num)

/* elements of smlblkl_p (small blocks list's pointer) are headers of single
 * linked list of small fix-sized blocks, of size 16, 24, .... 128
 */
static void** smlblkl_p;

/* elements of lrgblkl_p (small blocks list's pointer) are headers of double
 * linked list of large vary-sized blocks, of size {~256}, ....{32768+}
 */
static void** lrgblkl_p;

/* memory start block*/
static void* prologue;

/* memory end block */
static void* epilogue;

/* large block min size from index */
inline static unsigned int LBLK_SZ(int idx) {
    switch (idx) {
        case 0:
            return 136;
        case 1:
            return 264;
        case 2:
            return 520;
        case 3:
            return 776;
        case 4:
            return 1288;
        case 5:
            return 2056;
        case 6:
            return 3336;
        case 7:
            return 5384;
        case 8:
            return 8712;
        case 9:
            return 14088;
        case 10:
            return 22792;
        case 11:
            return 36872;
        default:
            return 0;
    }
}

/* obtain index for large blocks */
inline static int LBLK_IDX(int sz) {
    if (sz <= 256)
        return 0;
    else if (sz <= 512)
        return 1;
    else if (sz <= 768)
        return 2;
    else if (sz <= 1280)
        return 3;
    else if (sz <= 2048)
        return 4;
    else if (sz <= 3328)
        return 5;
    else if (sz <= 5376)
        return 6;
    else if (sz <= 8704)
        return 7;
    else if (sz <= 14080)
        return 8;
    else if (sz <= 22784)
        return 9;
    else if (sz <= 36864)
        return 10;
    else
        return 11;
}

/*
 * print_error:
 * print error msg
 * evaluate memory of ptr
 */
void print_error(void* ptr) {
    long val = *(long*)ptr;
    printf("0x%lx\n", val);
}

/*
 * mm_init - initialise list heads, all NULL except one large block almost
 * INIT_SIZE
 *
 */
int mm_init(void) {
    unsigned int i, sz;
    void** p = (void**)mem_sbrk(INIT_SIZE);

    /* initialise small block pointers to NULL */
    smlblkl_p = p;
    for (i = 0; i < N_SBLK; i++, p++)
        *p = VPTR(NULL);

    /* initialise large block pointers to NULL (except for the largest one) */
    lrgblkl_p = p;
    for (i = 0; i < N_LBLK; i++, p++)
        *p = VPTR(NULL);
    p++;

    /* mark the start of all blocks */
    HEADER(p) = 1u;
    EVAL4B(p) = 1u;
    prologue = p;
    p++;

    /* mark the end */
    epilogue = VPTR(DOWN_ALIGN(mem_heap_hi() + 1));

    /* make one large block */
    sz = (unsigned int)(epilogue - VPTR(p) - 8);
    HEADER(p) = sz | 2;
    L_NEXT(p) = VPTR(NULL);
    L_PREV(p) = VPTR(NULL);
    EVAL4B(epilogue - 8) = sz;
    sz += 8;

    if (SBLK_IDX(sz) < N_SBLK)
        smlblkl_p[SBLK_IDX(sz)] = p;
    else
        lrgblkl_p[LBLK_IDX(sz)] = p;

    /* mark the end of all blocks */
    if (mem_heap_hi() + 1 - epilogue < 4)
        mem_sbrk((int)(4 + epilogue - mem_heap_hi() - 1));
    HEADER(epilogue) = 1u;
    EVAL4B(epilogue) = 1u;
    return 0;
}

/* for small blocks, always pick first block */
void* find_sblk(int* idx) {
    int i = *idx;
    for (; i < N_SBLK; i++)
        if (smlblkl_p[i] != NULL)
            break;
    if (i == N_SBLK)
        return NULL;
    *idx = i;
    return smlblkl_p[i];
}

/* find a suitable large block */
static void* find_lblk(unsigned int blk_sz, int* idx) {
    void* blk_p;
    for (; *idx < N_LBLK; (*idx)++) {
        blk_p = lrgblkl_p[*idx];
        while (blk_p != NULL) {
            if (BLK_SZ(blk_p) >= blk_sz)
                return blk_p;
            blk_p = L_NEXT(blk_p);
        }
    }
    return NULL;
}

/* detach first element of list */
static inline void detach_1st_sblk(int idx, void* blk) {
    smlblkl_p[idx] = L_NEXT(blk);
}

/* detach a large block from list */
static inline void detach_lblk(int idx, void* blk) {
    void* prev = L_PREV(blk);
    void* next = L_NEXT(blk);
    if (prev)
        L_NEXT(prev) = next;
    else
        lrgblkl_p[idx] = next;
    if (next)
        L_PREV(next) = prev;
}

/* rewrite header and footer */
static inline void make_blk(int blk_sz, void* blk) {
    HEADER(blk) = blk_sz - 8;
    EVAL4B(blk + blk_sz - 8) = blk_sz - 8;
}

/* shorten a block to blk_sz, return the remaining parts */
static inline void* shorten(int blk_sz, void* blk) {
    void* ret = blk + blk_sz;
    int prev_alloc = HEADER(blk) & 2;
    make_blk(blk_sz, blk);
    if (prev_alloc)
        TAG_BLOCK(blk, 0b10);
    return ret;
}

/* attach a small block to front of the list */
static inline void attach_sblk(int idx, void* blk) {
    L_NEXT(blk) = smlblkl_p[idx];
    smlblkl_p[idx] = blk;
}

/* attach a large block to front of the list */
static inline void attach_lblk(int idx, void* blk) {
    void* next = lrgblkl_p[idx];
    lrgblkl_p[idx] = blk;
    L_PREV(blk) = VPTR(NULL);
    L_NEXT(blk) = next;
    if (next)
        L_PREV(next) = blk;
}

/* mark a block as allocated */
static inline void mark_used(void* blk) {
    TAG_BLOCK(blk, 1);
    TAG_BLOCK(blk + BLK_SZ(blk), 0b10);
}

/* mark a block as free */
static inline void mark_unused(void* blk) {
    HEADER(blk) &= ~1;
    /* restore footer */
    EVAL4B(blk + PAYLD_SZ(blk)) = PAYLD_SZ(blk);
    HEADER(blk + BLK_SZ(blk)) &= ~2;
}

/* find and detach a specific small block pointed by blk */
static inline void find_del_sblk(int idx, void* blk) {
    void* ptr = smlblkl_p[idx];
    if (ptr == blk) {
        detach_1st_sblk(idx, blk);
        return;
    }
    /* iterate through list */
    while (L_NEXT(ptr)) {
        if (L_NEXT(ptr) == blk) {
            L_NEXT(ptr) = L_NEXT(blk);
            return;
        }
        ptr = L_NEXT(ptr);
    }
}

/*
 * mm_malloc - Always allocate a block whose size is a multiple of the
 * alignment.
 */
void* mm_malloc(size_t size) {
    /* make use of footer */
    int blk_sz = ALIGN(size + 4);
    if (blk_sz < 16)
        blk_sz = 16;
    void* ptr;
    int idx;
    int real_idx;
    int diff_sz;
    int prev_alloc = 0;
    /* find small blk */
    if (blk_sz <= 128) {
        idx = SBLK_IDX(blk_sz);
        real_idx = idx;
        ptr = find_sblk(&real_idx);
        if (ptr) {
            detach_1st_sblk(real_idx, ptr);
            /* split the blk */
            diff_sz = SBLK_SZ(real_idx - idx - 2);
            if (diff_sz >= MIN_BLK_SZ) {
                void* leftover = shorten(SBLK_SZ(idx), ptr);
                make_blk(diff_sz, leftover);
                attach_sblk(real_idx - idx - 2, leftover);
            }
            mark_used(ptr);
            return ptr;
        }
    }
    /* find large blk */
    idx = LBLK_IDX(blk_sz);
    ptr = find_lblk(blk_sz, &idx);
    if (ptr) {
        detach_lblk(idx, ptr);
        diff_sz = BLK_SZ(ptr) - blk_sz;
        if (diff_sz >= MIN_BLK_SZ) {
            void* leftover = shorten(blk_sz, ptr);
            make_blk(diff_sz, leftover);
            if (SBLK_IDX(diff_sz) < N_SBLK)
                attach_sblk(SBLK_IDX(diff_sz), leftover);
            else
                attach_lblk(LBLK_IDX(diff_sz), leftover);
        }
        mark_used(ptr);
        return ptr;
    }
    /* increase heap */
    prev_alloc = (HEADER(epilogue) & 2);
    diff_sz = blk_sz + (int)(epilogue - mem_heap_hi()) - 1 + 8;
    if (mem_sbrk(diff_sz) == (void*)-1)
        return NULL;
    ptr = epilogue;
    make_blk(blk_sz, ptr);
    mark_used(ptr);
    if (prev_alloc)
        TAG_BLOCK(ptr, 0b10);

    /* mark new end */
    epilogue = ptr + blk_sz;
    HEADER(epilogue) = 3u;
    EVAL4B(epilogue) = 1u;
    return ptr;
}

/*
 * mm_free- coalesce immediately
 */
void mm_free(void* ptr) {
    // boundary conditions
    if (!ptr)
        return;

    int blk_sz = BLK_SZ(ptr);
    int merged_sz = blk_sz;
    int prev_alloc = 0;
    void* merged = ptr;
    unsigned int mark;
    mark_unused(ptr);

    // consider to coalesce the previous block
    if ((HEADER(ptr) & 2) == 0) {
        mark = EVAL4B(ptr - 8) + 8;
        merged = ptr - mark;
        if ((HEADER(merged) & 2))
            prev_alloc = 1;
        if (SBLK_IDX(mark) < N_SBLK)
            find_del_sblk(SBLK_IDX(mark), merged);
        else
            detach_lblk(LBLK_IDX(mark), merged);
        merged_sz += mark;
    } else {
        prev_alloc = 1;
    }

    // consider to coalesce the next block
    mark = HEADER(ptr + blk_sz);
    if ((mark & 1) == 0) {
        mark += 8;
        if (SBLK_IDX(mark) < N_SBLK)
            find_del_sblk(SBLK_IDX(mark), ptr + blk_sz);
        else
            detach_lblk(LBLK_IDX(mark), ptr + blk_sz);
        merged_sz += mark;
    }
    make_blk(merged_sz, merged);
    if (prev_alloc)
        TAG_BLOCK(merged, 0b10);

    if (SBLK_IDX(merged_sz) < N_SBLK)
        attach_sblk(SBLK_IDX(merged_sz), merged);
    else
        attach_lblk(LBLK_IDX(merged_sz), merged);
}

/*
 * mm_realloc - consider the cases which pdf has mentioned
 */
void* mm_realloc(void* ptr, size_t size) {
    void* newptr;

    // case 1
    if (!ptr)
        return mm_malloc(size);

    // case 2 and boundary condition
    if (!size) {
        mm_free(ptr);
        return NULL;
    }

    // case 3
    if (PAYLD_SZ(ptr) + 4 >= size)
        return ptr;

    if (!(newptr = mm_malloc(size)))
        return NULL;

    // copy the necessary block
    memcpy(newptr, ptr, PAYLD_SZ(ptr) + 4);
    mm_free(ptr);
    return newptr;
}

/*
 * mm_check: check the heap
 */
void mm_check(int verbose) {
    void* blk = prologue;
    void* high = mem_heap_hi() + 1;
    void* next;
    unsigned int blk_sz;

    if (epilogue + 4 > high) {
        printf("boundary error.");
        print_error(NULL);
    }

    /* test small block free list */
    for (int i = 0; i < N_SBLK; i++) {
        blk = smlblkl_p[i];
        while (blk) {
            if (blk <= prologue || blk >= epilogue) {
                printf("boundary error. 0x%lx\n", (unsigned long)blk);
                print_error(NULL);
            }
            if (HEADER(blk) & 1) {
                printf("allocate location error. 0x%lx\n", (unsigned long)blk);
                print_error(NULL);
            }
            blk_sz = BLK_SZ(blk);
            if (blk_sz != SBLK_SZ(i)) {
                printf("block size error. 0x%lx\n", (unsigned long)blk);
                print_error(NULL);
            }
            blk = L_NEXT(blk);
        }
    }

    /* test large block free list */
    for (int i = 0; i < N_LBLK; i++) {
        blk = lrgblkl_p[i];
        if (blk && L_PREV(blk)) {
            printf("no prev ptr. 0x%lx\n", (unsigned long)blk);
            print_error(NULL);
        }
        while (blk) {
            if (blk <= prologue || blk >= epilogue) {
                printf("boundary error. 0x%lx\n", (unsigned long)blk);
                print_error(NULL);
            }
            if (HEADER(blk) & 1) {
                printf("allocate location error. 0x%lx\n", (unsigned long)blk);
                print_error(NULL);
            }
            blk_sz = BLK_SZ(blk);
            if (blk_sz < LBLK_SZ(i) ||
                (i + 1 < N_LBLK && blk_sz >= LBLK_SZ(i + 1))) {
                printf("block size error. 0x%lx\n", (unsigned long)blk);
                print_error(NULL);
            }
            if (L_PREV(blk) && L_NEXT(L_PREV(blk)) != blk) {
                printf("link error. 0x%lx\n", (unsigned long)blk);
                print_error(NULL);
            }
            blk = L_NEXT(blk);
        }
    }

    /* test header/footer of all blocks */
    blk = prologue;
    while (true) {
        if (blk < prologue) {
            printf("outside low boundary 0x%lx\n", (unsigned long)blk);
            print_error(NULL);
        }
        if (((unsigned long)blk & 7ul)) {
            printf("unaligned blk at 0x%lx\n", (unsigned long)blk);
            print_error(NULL);
        }
        blk_sz = BLK_SZ(blk);
        next = blk + blk_sz;
        if (next > epilogue) {
            printf("next blk outside high boundary 0x%lx. corrupted header.\n",
                   (unsigned long)blk);
            print_error(NULL);
        }
        if ((HEADER(blk) & 2) == 0 && HEADER(blk) != EVAL4B(next - 8)) {
            printf("footer/header unequal. 0x%lx\n", (unsigned long)blk);
            print_error(NULL);
        }
        blk = next;
        if (next == epilogue)
            break;
    }
}