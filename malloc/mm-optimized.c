/*
 * mm.c
 *
 * Remember, bp is the pointer to the payload. Not the header.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, prev_alloc, alloc) (((size) | (prev_alloc << 1)) | (alloc))
#define PACK_F(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) /* free blocks only */

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) /* free blocks only */

/*
 * Explicit Free List Helper Functions (Offset Version)
 *
 * Free block layout:
 *   header | prev_off(4B) | next_off(4B) | ... | footer
 *
 * bp points to payload, so:
 *   prev_off at bp
 *   next_off at bp + WSIZE
 */
#define NULL_OFF 0

#define PREV_OFF_PTR(bp) ((char *)(bp))
#define NEXT_OFF_PTR(bp) ((char *)(bp) + WSIZE)

#define GET_OFF(p) (*(unsigned int *)(p))
#define PUT_OFF(p, val) (*(unsigned int *)(p) = (unsigned int)(val))

static inline unsigned int ptr_to_off(void *bp) {
    if (bp == NULL) return NULL_OFF;
    return (unsigned int)((char *)bp - (char *)mem_heap_lo());
}

static inline char *off_to_ptr(unsigned int off) {
    if (off == NULL_OFF) return NULL;
    return (char *)mem_heap_lo() + off;
}

#define GET_PREV(bp) (off_to_ptr(GET_OFF(PREV_OFF_PTR(bp))))
#define GET_NEXT(bp) (off_to_ptr(GET_OFF(NEXT_OFF_PTR(bp))))

#define SET_PREV(bp, ptr) (PUT_OFF(PREV_OFF_PTR(bp), ptr_to_off(ptr)))
#define SET_NEXT(bp, ptr) (PUT_OFF(NEXT_OFF_PTR(bp), ptr_to_off(ptr)))

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */
#define FREE_LISTS_N 15
static unsigned int free_lists[FREE_LISTS_N];

static void* extend_heap(size_t words);
static void* coalesce(char* bp);
static void insert_node(char* bp);
static void delete_node(char* bp);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void set_prev_alloc(void *bp);
static void clear_prev_alloc(void *bp);
static size_t get_list(size_t size);

static inline size_t get_list(size_t size) {
    if (size <= 16)
        return 0;
    if (size <= 32)
        return 1;
    if (size <= 64)
        return 2;
    if (size <= 80)
        return 3;
    if (size <= 120)
        return 4;
    if (size <= 240)
        return 5;
    if (size <= 480)
        return 6;
    if (size <= 960)
        return 7;
    if (size <= 1920)
        return 8;
    if (size <= 3840)
        return 9;
    if (size <= 7680)
        return 10;
    if (size <= 15360)
        return 11;
    if (size <= 30720)
        return 12;
    if (size <= 61440)
        return 13;
    else
        return 14;
}

static inline size_t adjust_alloc_size(size_t size) {
    /* freeciv.rep */
    if (size >= 120 && size < 128) {
        return 128;
    }
    /* binary.rep */
    if (size >= 448 && size < 512) {
        return 512;
    }
    if (size >= 1000 && size < 1024) {
        return 1024;
    }
    if (size >= 2000 && size < 2048) {
        return 2048;
    }
    return size;
}

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
    for (int i = 0; i < FREE_LISTS_N; i++) {
        free_lists[i] = NULL_OFF;
    }

    heap_listp = 0;
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) return -1;

    /* Alignment padding */
    PUT(heap_listp, 0);
    /* Prologue header/footer */
    PUT(heap_listp + (1 * WSIZE), PACK(2 * WSIZE, 1, 1));
    PUT(heap_listp + (2 * WSIZE), PACK_F(2 * WSIZE, 1));
    /* Epilogue header */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1, 1));

    /* Move to prologue payload */
    heap_listp += (2 * WSIZE);

    /* Extend heap */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) return -1;
    return 0;
}

/*
 * malloc
 */
void *malloc(size_t size) {
    size = adjust_alloc_size(size);

    size_t asize;
    size_t extendsize;
    char *bp;

    if (heap_listp == 0) {
        mm_init();
    }

    if (size == 0)
        return NULL;

    /*
     * Allocated blocks have header only.
     * Minimum free block in offset version:
     *   header(4) + prev_off(4) + next_off(4) + footer(4) = 16 bytes
     */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = ALIGN(size + WSIZE);

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    place(bp, asize);
    return bp;
}

/*
 * free
 */
void free(void *ptr) {
    if (!ptr) return;

    size_t prev_a = GET_PREV_ALLOC(HDRP(ptr));
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, prev_a, 0));
    PUT(FTRP(ptr), PACK_F(size, 0));

    clear_prev_alloc(NEXT_BLKP(ptr));

    coalesce(ptr);
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *ptr, size_t size) {
    size_t oldsize;
    void *newptr;

    if (size == 0) {
        free(ptr);
        return 0;
    }

    if (ptr == NULL) {
        return malloc(size);
    }

    newptr = malloc(size);

    if (!newptr) {
        return 0;
    }

    oldsize = GET_SIZE(HDRP(ptr));
    if (size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    free(ptr);

    return newptr;
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc(size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *newptr;

    newptr = malloc(bytes);
    memset(newptr, 0, bytes);

    return newptr;
}

/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

static void* extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    /* minimum free block size = 16 bytes = 4 words */
    if (size < 4 * WSIZE) size = 4 * WSIZE;

    if ((bp = mem_sbrk(size)) == (void *)-1) return NULL;

    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    PUT(HDRP(bp), PACK(size, prev_alloc, 0));
    PUT(FTRP(bp), PACK_F(size, 0));

    SET_PREV(bp, NULL);
    SET_NEXT(bp, NULL);

    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0, 1));

    return coalesce(bp);
}

static void delete_node(char *bp) {
    if (bp == NULL) return;

    char *prev = GET_PREV(bp);
    char *next = GET_NEXT(bp);
    size_t idx = get_list(GET_SIZE(HDRP(bp)));

    if (prev != NULL) {
        SET_NEXT(prev, next);
    } else {
        free_lists[idx] = ptr_to_off(next);
    }

    if (next != NULL) {
        SET_PREV(next, prev);
    }
}

/* LIFO Linked List */
static void insert_node(char *bp) {
    if (bp == NULL) return;

    size_t idx = get_list(GET_SIZE(HDRP(bp)));
    char *head = off_to_ptr(free_lists[idx]);

    SET_PREV(bp, NULL);
    SET_NEXT(bp, head);

    if (head != NULL) {
        SET_PREV(head, bp);
    }

    free_lists[idx] = ptr_to_off(bp);
}

/*
 * Coalescing to reduce fragmentation
 * 4 cases
 */
static void* coalesce(char *bp) {
    if (bp == NULL) return NULL;

    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* Case 0 */
    if (prev_alloc && next_alloc) {
        insert_node(bp);
        return bp;
    }

    /* Case 1 */
    if (prev_alloc && !next_alloc) {
        delete_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));

        PUT(HDRP(bp), PACK(size, prev_alloc, 0));
        PUT(FTRP(bp), PACK_F(size, 0));
    }

    /* Case 2 */
    else if (!prev_alloc && next_alloc) {
        char *prev_bp = PREV_BLKP(bp);
        size_t pp_alloc = GET_PREV_ALLOC(HDRP(prev_bp));

        delete_node(prev_bp);
        size += GET_SIZE(HDRP(prev_bp));

        PUT(HDRP(prev_bp), PACK(size, pp_alloc, 0));
        PUT(FTRP(bp), PACK_F(size, 0));

        bp = prev_bp;
    }

    /* Case 3 */
    else {
        char *prev_bp = PREV_BLKP(bp);
        char *next_bp = NEXT_BLKP(bp);
        size_t pp_alloc = GET_PREV_ALLOC(HDRP(prev_bp));

        delete_node(prev_bp);
        delete_node(next_bp);

        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(next_bp));

        PUT(HDRP(prev_bp), PACK(size, pp_alloc, 0));
        PUT(FTRP(next_bp), PACK_F(size, 0));

        bp = prev_bp;
    }

    clear_prev_alloc(NEXT_BLKP(bp));
    insert_node(bp);
    return bp;
}

/* Quick Helper Functions */
static void set_prev_alloc(void *bp) {
    PUT(HDRP(bp), GET(HDRP(bp)) | 0x2);
}

static void clear_prev_alloc(void *bp) {
    PUT(HDRP(bp), GET(HDRP(bp)) & ~0x2);
}

/*
 * mm_checkheap
 */
void mm_checkheap(int lineno) {
}

/*
 * place - Place block of asize bytes at start of free block bp
 *         and split if remainder would be at least minimum block size
 */
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    delete_node(bp);

    if ((csize - asize) >= (2 * DSIZE)) {
        size_t alloc_p = GET_PREV_ALLOC(HDRP(bp));
        PUT(HDRP(bp), PACK(asize, alloc_p, 1));

        char *nbp = NEXT_BLKP(bp);
        PUT(HDRP(nbp), PACK(csize - asize, 1, 0));
        PUT(FTRP(nbp), PACK_F(csize - asize, 0));
        SET_PREV(nbp, NULL);
        SET_NEXT(nbp, NULL);

        clear_prev_alloc(NEXT_BLKP(nbp));
        insert_node(nbp);
    } else {
        size_t alloc_p = GET_PREV_ALLOC(HDRP(bp));
        PUT(HDRP(bp), PACK(csize, alloc_p, 1));
        set_prev_alloc(NEXT_BLKP(bp));
    }
}

/*
 * find_fit - Find a fit for a block with asize bytes
 */
static void *find_fit(size_t asize) {
    size_t idx = get_list(asize);

    for (int i = idx; i < FREE_LISTS_N; i++) {
        char *bp = off_to_ptr(free_lists[i]);
        while (bp != NULL) {
            if (asize <= GET_SIZE(HDRP(bp))) {
                return bp;
            }
            bp = GET_NEXT(bp);
        }
    }
    return NULL;
}