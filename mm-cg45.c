/*
 * mm.c - A dynamic memory allocator implementation using segregated free lists,
 *        boundary-tag coalescing, and block splitting for the CMU 15-213 Malloc Lab.
 * 
 * Block format:
 * [ Header | (Payload or Free pointers) | Footer ]
 * Header and Footer: 8 bytes each (size and allocation flags).
 * - Header stores the block size (aligned to 8 bytes) in the upper bits.
 * - It uses the lowest bit (ALLOC flag) to indicate if the block is allocated (1) or free (0).
 * - It uses the second lowest bit (PREV_ALLOC flag) to indicate if the previous block is allocated (1) or free (0).
 * - For allocated blocks, no footer is stored (to reduce overhead); the footer is only used in free blocks.
 * - Free blocks: contain pointers to next and previous free blocks in the segregated list, stored in the payload area.
 * 
 * Segregated free lists:
 * The allocator maintains an array of free lists, where each list holds free blocks of certain size ranges.
 * Smaller size classes hold blocks up to a certain size, larger classes hold increasingly larger blocks (typically powers of two ranges).
 * This allows faster search for an appropriate free block on allocation requests.
 * 
 * Coalescing:
 * When a block is freed, it is coalesced (merged) with adjacent free blocks (if any) immediately to reduce external fragmentation.
 * Coalescing uses the boundary tags (footer of previous free block and header of next free block) and the header flags.
 * The allocator uses immediate coalescing (on every free) to maintain larger free blocks.
 * 
 * Allocation strategy:
 * - When allocating, the request size is adjusted to include overhead and alignment to 8 bytes.
 * - A search is done in the appropriate size class list for a free block that fits.
 * - If a suitable block is found, it is placed (possibly splitting it if significantly larger than needed).
 * - If none is found, the heap is extended by a fixed chunk and the allocation retried.
 * The search uses a first-fit approach within the segregated list classes for speed (it scans within a size class and then larger classes if needed).
 * 
 * Reallocation (realloc):
 * - If the new size is smaller, the block is shrunk and the excess space is freed (with coalescing).
 * - If the new size is larger, the allocator tries to expand the block in place by absorbing the next free block or extending the heap.
 * - If it cannot expand in place, it falls back to allocating a new block and copying the data.
 * 
 * Alignment: All blocks are aligned to 8 bytes. The block size always is a multiple of 8.
 * Minimum block size: Enough to fit the header, footer (for free blocks), and free list pointers.
 * (On 64-bit, this is 32 bytes; on 32-bit, 16 bytes).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include "mm.h"
#include "memlib.h"

team_t team = {
    /* Team name */
    "Gabu-chan and her datenshis",
    /* First member's full name */
    "Tenma Gabriel White",
    /* First member's email address */
    "tenmwhite@cs.stonybrook.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* Basic constants and macros */
#define WSIZE sizeof(size_t)             /* Word and header/footer size (bytes) */
#define DSIZE (2 * WSIZE)                /* Double word size (alignment) */
#define ALIGNMENT 8                     /* Alignment requirement (bytes) */
#define CHUNKSIZE (1 << 12)             /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Align 'size' to nearest multiple of ALIGNMENT (8 bytes) */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~((size_t)0x7))

/* Pack a size and allocation bit (and prev allocation bit) into a single value */
#define PACK(size, prev_alloc, alloc)   ((size) | ((prev_alloc) ? 0x2 : 0x0) | ((alloc) ? 0x1 : 0x0))

/* Read and write a word at address p */
#define GET(p)       (*(size_t *)(p))
#define PUT(p, val)  (*(size_t *)(p) = (val))

/* Read the size and allocation fields from address p (p is address of header/footer) */
#define GET_SIZE(p)      (GET(p) & ~(size_t)0x7)    /* Block size (ignore lower 3 bits) */
#define GET_ALLOC(p)     (GET(p) & 0x1)             /* 1 if block is allocated, 0 if free */
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)     /* 1 if previous block is allocated, 0 if free */

/* Given block payload pointer bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block payload pointer bp, compute address of next and previous block headers */
#define NEXT_HDRP(bp)  ((char *)(bp) + GET_SIZE(HDRP(bp)) - WSIZE)
#define PREV_HDRP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Free list pointers (stored in free block's payload) */
#define NEXT_FREE(bp)  (*(void **)(bp))
#define PREV_FREE(bp)  (*(void **)((char *)(bp) + sizeof(void *)))

/* Number of segregated free lists */
#define LIST_COUNT 16

/* Global variables */
static void *seg_free_lists[LIST_COUNT];   /* Array of free list heads for each size class */
static char *heap_start = 0;               /* Pointer to first block in heap (after prologue) */

/* Function prototypes for internal helper functions */
static int get_list_index(size_t size);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);
static void *coalesce(void *bp);
static void *extend_heap(size_t bytes);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/*
 * mm_init - Initialize the memory manager, including segregated free lists and initial heap setup.
 */
int mm_init(void) {
    // Initialize segregated free list heads
    for (int i = 0; i < LIST_COUNT; i++) {
        seg_free_lists[i] = NULL;
    }

    // Allocate initial heap: alignment padding, prologue block, and epilogue header
    size_t initial_size = 4 * WSIZE;
    char *heap = (char *)mem_sbrk(initial_size);
    if (heap == (char *)-1) {
        return -1;
    }
    // Alignment padding
    PUT(heap, 0);
    // Prologue block (allocated, size = DSIZE)
    PUT(heap + 1*WSIZE, PACK(DSIZE, 1, 1));   // Prologue header (prev_alloc=1, alloc=1)
    PUT(heap + 2*WSIZE, PACK(DSIZE, 1, 1));   // Prologue footer
    // Epilogue header
    PUT(heap + 3*WSIZE, PACK(0, 1, 1));       // Epilogue header (prev_alloc=1, alloc=1)
    heap_start = heap + 2*WSIZE;             // heap_start points to prologue's payload

    // Extend the heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE) == NULL) {
        return -1;
    }
    return 0;
}

/*
 * extend_heap - Extend heap by 'bytes'. Creates a new free block and coalesces with previous free block if possible.
 * Returns pointer to the payload of the new free block (coalesced), or NULL if memory allocation fails.
 */
static void *extend_heap(size_t bytes) {
    // Align the extended size
    size_t asize = ALIGN(bytes);
    if (asize < DSIZE) {
        asize = DSIZE;
    }
    char *bp = (char *)mem_sbrk(asize);
    if ((long)bp == -1) {
        return NULL;
    }
    // bp points to start of new memory (old epilogue)
    char *new_block_hdr = bp;
    // Get prev_alloc status from the old epilogue header
    int prev_alloc = GET_PREV_ALLOC(new_block_hdr);
    // Create new free block's header and footer
    PUT(new_block_hdr, PACK(asize, prev_alloc, 0));
    char *new_block_bp = new_block_hdr + WSIZE;
    char *new_block_ftr = new_block_hdr + asize - WSIZE;
    PUT(new_block_ftr, PACK(asize, prev_alloc, 0));
    // New epilogue header
    char *new_epilogue = new_block_hdr + asize;
    PUT(new_epilogue, PACK(0, 0, 1));
    
    // Coalesce with previous block if it was free
    if (!prev_alloc) {
        new_block_bp = coalesce(new_block_bp);
    }
    // Insert the new free block into appropriate free list
    insert_free_block(new_block_bp);
    return new_block_bp;
}

/*
 * get_list_index - Choose an index in the segregated free list array based on block size.
 * Uses power-of-two size class grouping.
 */
static int get_list_index(size_t size) {
    int index = 0;
    size_t limit = 32;
    while (index < LIST_COUNT - 1 && size > limit) {
        limit <<= 1;
        index++;
    }
    return index;
}

/*
 * insert_free_block - Insert free block 'bp' into segregated free list.
 * Insertion is at the beginning of the list for simplicity (LIFO order).
 */
static void insert_free_block(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int index = get_list_index(size);
    void *head = seg_free_lists[index];
    if (head != NULL) {
        PREV_FREE(head) = bp;
    }
    NEXT_FREE(bp) = head;
    PREV_FREE(bp) = NULL;
    seg_free_lists[index] = bp;
}

/*
 * remove_free_block - Remove free block 'bp' from segregated free list.
 */
static void remove_free_block(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int index = get_list_index(size);
    void *prev = PREV_FREE(bp);
    void *next = NEXT_FREE(bp);
    if (prev != NULL) {
        NEXT_FREE(prev) = next;
    } else {
        seg_free_lists[index] = next;
    }
    if (next != NULL) {
        PREV_FREE(next) = prev;
    }
}

/*
 * coalesce - Coalesce free block 'bp' with adjacent free blocks if possible.
 * Returns pointer to the coalesced free block (payload).
 */
static void *coalesce(void *bp) {
    char *hdr = HDRP(bp);
    size_t size = GET_SIZE(hdr);
    int prev_alloc = GET_PREV_ALLOC(hdr);
    char *next_hdr = hdr + size;
    int next_alloc = GET_ALLOC(next_hdr);

    if (!prev_alloc) {
        // Merge with previous free block
        char *prev_ftr = hdr - WSIZE;
        size_t prev_size = GET_SIZE(prev_ftr);
        char *prev_hdr = hdr - prev_size;
        void *prev_bp = prev_hdr + WSIZE;
        remove_free_block(prev_bp);
        size += prev_size;
        hdr = prev_hdr;
        bp = prev_hdr + WSIZE;
        prev_alloc = GET_PREV_ALLOC(prev_hdr);
    }
    if (!next_alloc) {
        // Merge with next free block
        void *next_bp = next_hdr + WSIZE;
        size_t next_size = GET_SIZE(next_hdr);
        remove_free_block(next_bp);
        size += next_size;
        next_hdr = hdr + size;
    }
    // Write new coalesced block's header and footer
    PUT(hdr, PACK(size, prev_alloc, 0));
    char *ftr = hdr + size - WSIZE;
    PUT(ftr, PACK(size, prev_alloc, 0));
    // Update next block's prev_alloc flag
    if ((size_t)GET_SIZE(next_hdr) != 0) {
        // Next block is not epilogue
        PUT(next_hdr, GET(next_hdr) & ~0x2);
    } else {
        // Next block is epilogue
        PUT(next_hdr, PACK(0, 0, 1));
    }
    return bp;
}

/*
 * find_fit - Find a fit for a block of size 'asize' in the free lists.
 * Searches appropriate size class list and upwards for a free block >= asize.
 * Returns payload pointer of the found free block or NULL if no fit found.
 */
static void *find_fit(size_t asize) {
    int index = get_list_index(asize);
    for (int i = index; i < LIST_COUNT; i++) {
        void *bp = seg_free_lists[i];
        while (bp != NULL) {
            if (GET_SIZE(HDRP(bp)) >= asize) {
                return bp;
            }
            bp = NEXT_FREE(bp);
        }
    }
    return NULL;
}

/*
 * place - Place a block of size 'asize' at the start of free block 'bp'.
 * Splits the block if the remainder would be at least the minimum block size.
 */
static void place(void *bp, size_t asize) {
    size_t current_size = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    remove_free_block(bp);
    if ((current_size - asize) >= (DSIZE * 2)) {
        // Split the block
        PUT(HDRP(bp), PACK(asize, prev_alloc, 1));
        char *new_hdr = (char *)HDRP(bp) + asize;
        size_t remain_size = current_size - asize;
        PUT(new_hdr, PACK(remain_size, 1, 0));
        void *new_bp = new_hdr + WSIZE;
        char *new_ftr = new_hdr + remain_size - WSIZE;
        PUT(new_ftr, PACK(remain_size, 1, 0));
        // Update next block's prev_alloc flag
        char *next_hdr = new_hdr + remain_size;
        if ((size_t)GET_SIZE(next_hdr) != 0) {
            PUT(next_hdr, GET(next_hdr) & ~0x2);
        } else {
            PUT(next_hdr, PACK(0, 0, 1));
        }
        insert_free_block(new_bp);
    } else {
        // Use entire block without splitting
        PUT(HDRP(bp), PACK(current_size, prev_alloc, 1));
        char *next_hdr = (char *)HDRP(bp) + current_size;
        if ((size_t)GET_SIZE(next_hdr) != 0) {
            PUT(next_hdr, GET(next_hdr) | 0x2);
        } else {
            PUT(next_hdr, PACK(0, 1, 1));
        }
    }
}

/*
 * mm_malloc - Allocate a block of at least 'size' bytes.
 */
void *mm_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size_t asize;
    if (size <= DSIZE) {
        asize = DSIZE * 2;
    } else {
        asize = ALIGN(size + WSIZE);
        if (asize < DSIZE * 2) {
            asize = DSIZE * 2;
        }
    }
    void *bp = find_fit(asize);
    if (bp != NULL) {
        place(bp, asize);
        return bp;
    }
    size_t extend_size = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extend_size)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Free a block pointed by ptr. Coalesce with neighbors and insert into free list.
 */
void mm_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    char *hdr = HDRP(ptr);
    size_t size = GET_SIZE(hdr);
    int prev_alloc = GET_PREV_ALLOC(hdr);
    PUT(hdr, PACK(size, prev_alloc, 0));
    char *ftr = (char *)ptr + size - DSIZE;
    PUT(ftr, PACK(size, prev_alloc, 0));
    char *next_hdr = hdr + size;
    if ((size_t)GET_SIZE(next_hdr) != 0) {
        PUT(next_hdr, GET(next_hdr) & ~0x2);
    } else {
        PUT(next_hdr, PACK(0, 0, 1));
    }
    void *coalesced_bp = coalesce(ptr);
    insert_free_block(coalesced_bp);
}

/*
 * mm_realloc - Reallocate block ptr to size bytes.
 * Expands in place if possible, otherwise allocates new block and frees old block.
 */
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return mm_malloc(size);
    }
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }
    size_t asize;
    if (size <= DSIZE) {
        asize = DSIZE * 2;
    } else {
        asize = ALIGN(size + WSIZE);
        if (asize < DSIZE * 2) {
            asize = DSIZE * 2;
        }
    }
    char *hdr = HDRP(ptr);
    size_t old_size = GET_SIZE(hdr);
    int prev_alloc = GET_PREV_ALLOC(hdr);
    if (asize <= old_size) {
        size_t remaining = old_size - asize;
        if (remaining >= DSIZE * 2) {
            char *new_hdr = hdr + asize;
            PUT(hdr, PACK(asize, prev_alloc, 1));
            PUT(new_hdr, PACK(remaining, 1, 0));
            void *new_bp = new_hdr + WSIZE;
            char *new_ftr = new_hdr + remaining - WSIZE;
            PUT(new_ftr, PACK(remaining, 1, 0));
            char *next_hdr = new_hdr + remaining;
            if ((size_t)GET_SIZE(next_hdr) != 0) {
                PUT(next_hdr, GET(next_hdr) & ~0x2);
            } else {
                PUT(next_hdr, PACK(0, 0, 1));
            }
            void *coalesced_bp = coalesce(new_bp);
            insert_free_block(coalesced_bp);
        }
        return ptr;
    }
    char *next_hdr = hdr + old_size;
    int next_alloc = GET_ALLOC(next_hdr);
    size_t next_size = GET_SIZE(next_hdr);
    // Try to expand into next free block
    if (!next_alloc && (old_size + next_size >= asize)) {
        void *next_bp = next_hdr + WSIZE;
        remove_free_block(next_bp);
        size_t combined_size = old_size + next_size;
        PUT(hdr, PACK(combined_size, prev_alloc, 1));
        size_t remaining = combined_size - asize;
        if (remaining >= DSIZE * 2) {
            char *split_hdr = hdr + asize;
            PUT(split_hdr, PACK(remaining, 1, 0));
            void *split_bp = split_hdr + WSIZE;
            char *split_ftr = split_hdr + remaining - WSIZE;
            PUT(split_ftr, PACK(remaining, 1, 0));
            char *next2_hdr = split_hdr + remaining;
            if ((size_t)GET_SIZE(next2_hdr) != 0) {
                PUT(next2_hdr, GET(next2_hdr) & ~0x2);
            } else {
                PUT(next2_hdr, PACK(0, 0, 1));
            }
            insert_free_block(split_bp);
        } else {
            char *next2_hdr = hdr + (old_size + next_size);
            if ((size_t)GET_SIZE(next2_hdr) != 0) {
                PUT(next2_hdr, GET(next2_hdr) | 0x2);
            } else {
                PUT(next2_hdr, PACK(0, 1, 1));
            }
        }
        return ptr;
    }
    // If last block and not enough space, extend the heap
    if ((size_t)GET_SIZE(next_hdr) == 0) {
        size_t extend_size = MAX(asize - old_size, CHUNKSIZE);
        if (extend_heap(extend_size) == NULL) {
            return NULL;
        }
        next_hdr = hdr + old_size;
        next_size = GET_SIZE(next_hdr);
        void *next_bp = next_hdr + WSIZE;
        remove_free_block(next_bp);
        size_t combined_size = old_size + next_size;
        PUT(hdr, PACK(combined_size, prev_alloc, 1));
        size_t remaining = combined_size - asize;
        if (remaining >= DSIZE * 2) {
            char *split_hdr = hdr + asize;
            PUT(split_hdr, PACK(remaining, 1, 0));
            void *split_bp = split_hdr + WSIZE;
            char *split_ftr = split_hdr + remaining - WSIZE;
            PUT(split_ftr, PACK(remaining, 1, 0));
            char *next2_hdr = split_hdr + remaining;
            if ((size_t)GET_SIZE(next2_hdr) != 0) {
                PUT(next2_hdr, GET(next2_hdr) & ~0x2);
            } else {
                PUT(next2_hdr, PACK(0, 0, 1));
            }
            insert_free_block(split_bp);
        } else {
            char *next2_hdr = hdr + combined_size;
            if ((size_t)GET_SIZE(next2_hdr) != 0) {
                PUT(next2_hdr, GET(next2_hdr) | 0x2);
            } else {
                PUT(next2_hdr, PACK(0, 1, 1));
            }
        }
        return ptr;
    }
    // Otherwise, need to allocate new block
    void *new_ptr = mm_malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }
    size_t copy_size = GET_SIZE(HDRP(ptr)) - WSIZE;
    if (size < copy_size) {
        copy_size = size;
    }
    memcpy(new_ptr, ptr, copy_size);
    mm_free(ptr);
    return new_ptr;
}
