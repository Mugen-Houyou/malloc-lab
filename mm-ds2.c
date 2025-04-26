#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

 
 /*********************************************************
  * NOTE TO STUDENTS: Before you do anything else, please
  * provide your team information in the following struct.
  ********************************************************/
 
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

#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define ALIGNMENT 8
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)
#define MAX_HEAP_BLOCKS (1<<12)
#define MIN_BLOCK 16

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))
#define PREV_FREE(bp) (*(void **)(bp))
#define NEXT_FREE(bp) (*(void **)((char *)(bp) + WSIZE))

#define NUM_LISTS 12

static void *seg_list[NUM_LISTS];
static char *heap_listp;

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void add_free_block(void *bp);
static void remove_free_block(void *bp);
static int get_index(size_t size);

int mm_init(void) {
    for (int i = 0; i < NUM_LISTS; i++) seg_list[i] = NULL;
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) return -1;
    PUT(heap_listp, 0);
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 2*WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 3*WSIZE, PACK(0, 1));
    heap_listp += 2*WSIZE;
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1;
    return 0;
}

void *mm_malloc(size_t size) {
    if (size == 0) return NULL;
    size_t asize = ALIGN(size + WSIZE);
    if (asize < MIN_BLOCK) asize = MIN_BLOCK;

    void *bp = find_fit(asize);
    if (bp) {
        place(bp, asize);
        return bp;
    }

    size_t extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) return NULL;
    place(bp, asize);
    return bp;
}

void mm_free(void *bp) {
    if (bp == NULL) return;
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }

    size_t old_size = GET_SIZE(HDRP(ptr));
    size_t new_size = ALIGN(size + WSIZE);
    if (new_size == old_size) return ptr;

    if (new_size < old_size) {
        size_t diff = old_size - new_size;
        if (diff >= MIN_BLOCK) {
            PUT(HDRP(ptr), PACK(new_size, 1));
            PUT(FTRP(ptr), PACK(new_size, 1));
            void *new_bp = NEXT_BLKP(ptr);
            PUT(HDRP(new_bp), PACK(diff, 0));
            PUT(FTRP(new_bp), PACK(diff, 0));
            coalesce(new_bp);
        }
        return ptr;
    }

    void *next = NEXT_BLKP(ptr);
    if (!GET_ALLOC(HDRP(next)) && (old_size + GET_SIZE(HDRP(next)) >= new_size)) {
        size_t total = old_size + GET_SIZE(HDRP(next));
        remove_free_block(next);
        PUT(HDRP(ptr), PACK(total, 1));
        PUT(FTRP(ptr), PACK(total, 1));
        size_t rem = total - new_size;
        if (rem >= MIN_BLOCK) {
            PUT(HDRP(ptr), PACK(new_size, 1));
            PUT(FTRP(ptr), PACK(new_size, 1));
            void *new_bp = NEXT_BLKP(ptr);
            PUT(HDRP(new_bp), PACK(rem, 0));
            PUT(FTRP(new_bp), PACK(rem, 0));
            coalesce(new_bp);
        }
        return ptr;
    }

    void *new_ptr = mm_malloc(size);
    if (new_ptr == NULL) return NULL;
    memcpy(new_ptr, ptr, old_size - WSIZE);
    mm_free(ptr);
    return new_ptr;
}

static void *extend_heap(size_t words) {
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    void *bp = mem_sbrk(size);
    if (bp == (void *)-1) return NULL;
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    return coalesce(bp);
}

static void *coalesce(void *bp) {
    void *prev = PREV_BLKP(bp);
    void *next = NEXT_BLKP(bp);
    size_t prev_alloc = GET_ALLOC(HDRP(prev));
    size_t next_alloc = GET_ALLOC(HDRP(next));
    size_t size = GET_SIZE(HDRP(bp));

    if (!prev_alloc) {
        remove_free_block(prev);
        size += GET_SIZE(HDRP(prev));
        bp = prev;
    }
    if (!next_alloc) {
        remove_free_block(next);
        size += GET_SIZE(HDRP(next));
    }
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    add_free_block(bp);
    return bp;
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    remove_free_block(bp);
    if (csize - asize >= MIN_BLOCK) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *new_bp = NEXT_BLKP(bp);
        PUT(HDRP(new_bp), PACK(csize - asize, 0));
        PUT(FTRP(new_bp), PACK(csize - asize, 0));
        coalesce(new_bp);
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

static void *find_fit(size_t asize) {
    int idx = get_index(asize);
    while (idx < NUM_LISTS) {
        void *bp = seg_list[idx];
        while (bp) {
            if (GET_SIZE(HDRP(bp)) >= asize) return bp;
            bp = NEXT_FREE(bp);
        }
        idx++;
    }
    return NULL;
}

static void add_free_block(void *bp) {
    int idx = get_index(GET_SIZE(HDRP(bp)));
    NEXT_FREE(bp) = seg_list[idx];
    PREV_FREE(bp) = NULL;
    if (seg_list[idx]) PREV_FREE(seg_list[idx]) = bp;
    seg_list[idx] = bp;
}

static void remove_free_block(void *bp) {
    int idx = get_index(GET_SIZE(HDRP(bp)));
    if (PREV_FREE(bp)) {
        NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
    } else {
        seg_list[idx] = NEXT_FREE(bp);
    }
    if (NEXT_FREE(bp)) PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
}

static int get_index(size_t size) {
    size = size < 16 ? 16 : size;
    int idx = 0;
    while (size > (1 << (idx + 4)) && idx < NUM_LISTS - 1) idx++;
    return idx;
}


 
static void mm_checkheap(int line) {
    char *bp = heap_listp;
    int errors = 0;

    /* 1) 프롤로그 검사 */
    if (GET(HDRP(bp)) != PACK(DSIZE, 1))
        printf("Bad prologue header at %p (line %d)\n", bp, line);
    if (GET(FTRP(bp)) != PACK(DSIZE, 1))
        printf("Bad prologue footer at %p (line %d)\n", bp, line);

    /* 2) 본문 블록 순회 */
    for (size_t count = 0; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp), ++count) {
        size_t h = GET(HDRP(bp));
        size_t f = GET(FTRP(bp));
        size_t hsize  = GET_SIZE(HDRP(bp));
        size_t halloc = GET_ALLOC(HDRP(bp));
        size_t fsize  = GET_SIZE(FTRP(bp));
        size_t falloc = GET_ALLOC(FTRP(bp));

        /* 2-1) 에필로그 도달 시 종료 */
        if (hsize == 0) {
            if (h != PACK(0, 1))
                printf("Bad epilogue header at %p (line %d)\n", bp, line);
            break;
        }

        /* 2-2) 크기, 할당 비트 일치 검사 */
        if (hsize != fsize)
            printf("Header/Footer size mismatch at %p (line %d)\n", bp, line);
        if (halloc != falloc)
            printf("Header/Footer alloc mismatch at %p (line %d)\n", bp, line);

        /* 2-3) 정렬(Alignment) 검사 */
        if (((size_t)bp % DSIZE) != 0)
            printf("Alignment error at %p (line %d)\n", bp, line);

        /* 2-4) 최소 블록 크기 검사 */
        if (hsize < DSIZE)
            printf("Block too small at %p (line %d)\n", bp, line);

        /* 2-5) free 블록 주변 할당 상태 검사 */
        if (!halloc) {
            if (GET_ALLOC(HDRP(PREV_BLKP(bp))))
                printf("Free block %p has allocated previous block (line %d)\n", bp, line);
            if (GET_ALLOC(HDRP(NEXT_BLKP(bp))))
                printf("Free block %p has allocated next block (line %d)\n", bp, line);
        }

        /* 2-6) 힙 경계 검사 */
        if (HDRP(bp) < (char *)mem_heap_lo() ||
            FTRP(bp) > (char *)mem_heap_hi())
            printf("Block %p out of heap bounds (line %d)\n", bp, line);

            
        /* (추가) 인접 free 블록 없음 */
        if (!halloc && !GET_ALLOC(HDRP(NEXT_BLKP(bp)))) {
            printf("Uncoalesced free blocks at %p & %p (line %d)\n", bp, NEXT_BLKP(bp), line);
            errors++;
        }

        /* (추가) 순회 카운트 한계 */
        if (count > MAX_HEAP_BLOCKS) {
            printf("Possible heap cycle detected at %p (line %d)\n", bp, line);
            errors++;
            break;
        }
    }

    if (errors)
        printf("mm_checkheap: %d error(s) detected (line %d)\n",errors, line);
}