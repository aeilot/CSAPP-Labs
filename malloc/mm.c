/*
 * mm.c
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
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

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)


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
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // Used for free blocks only

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // Used for free blocks only 

/* Explicit Free List Helper Functions */
#define PREV(bp) ((char *)(bp))
#define NEXT(bp) ((char *)(bp) + DSIZE)
// A 64-bit pointer takes 8 bytes.

#define GET_PREV(bp) (*(char **)(PREV(bp)))
#define GET_NEXT(bp) (*(char **)(NEXT(bp)))

#define SET_PREV(bp, val) (*(char **)(PREV(bp)) = (char *)(val))
#define SET_NEXT(bp, val) (*(char **)(NEXT(bp)) = (char *)(val))

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */  
static char *free_list = 0; /* Pointer to free list */

static void* extend_heap(size_t words);
static void* coalesce(char* bp);
static void insert_node(char* bp);
static void delete_node(char* bp);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void set_prev_alloc(void *bp);
static void clear_prev_alloc(void *bp);

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
    free_list = 0;
    heap_listp = 0;
    if((heap_listp = mem_sbrk(4*WSIZE))==(void*)-1) return -1;
    // Alignment
    PUT(heap_listp, 0);
    // Prolog
    PUT(heap_listp + (1*WSIZE), PACK(2*WSIZE, 1, 1));
    PUT(heap_listp + (2*WSIZE), PACK_F(2*WSIZE, 1));
    // Epilog
    PUT(heap_listp + (3*WSIZE), PACK(0,1,1));
    // Move header
    heap_listp += (2*WSIZE);
    // Extend List
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1;
    return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;      

    if (heap_listp == 0){
        mm_init();
    }
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    if (size <= 2*DSIZE)                                          
        asize = 3*DSIZE;                                        
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); 

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  
        place(bp, asize);                  
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);                 
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  
    place(bp, asize);                                 
    return bp;
}

/*
 * free
 */
void free (void *ptr) {
    if(!ptr) return;
    size_t prev_a = GET_PREV_ALLOC(HDRP(ptr));
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, prev_a, 0));
    PUT(FTRP(ptr), PACK_F(size, 0));
    clear_prev_alloc(NEXT_BLKP(ptr));
    if(size >= 6*WSIZE) {
        insert_node(ptr);
        coalesce(ptr);
    }
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *ptr, size_t size) {
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return malloc(size);
    }

    newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    free(ptr);

    return newptr;
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) {
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

static void* extend_heap(size_t words){
    char* bp;
    size_t size;
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if(size < 6*WSIZE) size = 6*WSIZE;
    if((bp = mem_sbrk(size)) == (void*)-1) return NULL;
    // mem_sbrk gets the old program break, a pointer pointing to the bottom of the heap. (it's not the old epilogue)
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), PACK(size, prev_alloc, 0));
    PUT(FTRP(bp), PACK_F(size, 0));
    SET_NEXT(bp, NULL);
    SET_PREV(bp, NULL);
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0, 1));
    insert_node(bp);
    return coalesce(bp);
}

static void delete_node(char* bp) {
    if (bp == NULL) return;

    char* nxt = GET_NEXT(bp);
    char* prev = GET_PREV(bp);

    if (nxt != NULL) {
        SET_PREV(nxt, prev);
    }

    if (prev != NULL) {
        SET_NEXT(prev, nxt);
    } else {
        free_list = nxt;
    }
}

static void insert_node(char* bp) {
    if(bp == NULL) return;
    if(free_list == 0) {
        free_list = bp;
        SET_PREV(bp, NULL);
        SET_NEXT(bp, NULL);
        return;
    }
    SET_NEXT(bp, free_list);
    SET_PREV(free_list, bp);
    SET_PREV(bp, NULL);
    free_list = bp;
}

static void* coalesce(char* bp) {
    if(bp==NULL) return NULL;
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        return bp;
    }

    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        delete_node(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, prev_alloc, 0));
        PUT(FTRP(bp), PACK_F(size,0));
    }

    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        size_t pp_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK_F(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, pp_alloc, 0));
        delete_node(bp);
        bp = PREV_BLKP(bp);
    }

    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(HDRP(NEXT_BLKP(bp)));
        size_t pp_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, pp_alloc, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK_F(size, 0));
        delete_node(bp);
        delete_node(NEXT_BLKP(bp)); 
        bp = PREV_BLKP(bp);
    }

    return bp;
}

static void set_prev_alloc(void *bp) {
    PUT(HDRP(bp), GET(HDRP(bp)) | 0x2);
}

static void clear_prev_alloc(void *bp) {
    PUT(HDRP(bp), GET(HDRP(bp)) & ~0x2);
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
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
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));   

    if ((csize - asize) >= (3*DSIZE)) { 
        size_t alloc_p = GET_PREV_ALLOC(HDRP(bp));
        PUT(HDRP(bp), PACK(asize, alloc_p, 1));
        delete_node(bp);
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 1, 0));
        PUT(FTRP(bp), PACK_F(csize-asize, 0));
        insert_node(bp);
    }
    else { 
        size_t alloc_p = GET_PREV_ALLOC(HDRP(bp));
        PUT(HDRP(bp), PACK(csize, alloc_p, 1));
        delete_node(bp);
        set_prev_alloc(NEXT_BLKP(bp));
    }
}

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void *find_fit(size_t asize)
{
    /* First-fit search */
    void *bp;

    for (bp = free_list; bp!=NULL; bp = GET_NEXT(bp)) {
        if (asize <= GET_SIZE(HDRP(bp))) {
            return bp;
        }
    }
    return NULL; /* No fit */
}