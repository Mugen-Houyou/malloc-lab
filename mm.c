/**
 * mm.c v0.2: Explicit allocator & implicit free list & first-fit 탐색 기반 예제.
 * - header/footer로 크기, 할당 비트 관리
 * - free는 coalescing으로 인접 빈 블록을 병합
 * - realloc은 걍 새 블록 할당 후 복사
 * - split으로 남는 공간 분할
 */

#include <stdio.h>
#include <stdlib.h>
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


/* 기본 상수, 매크로 */
#define WSIZE 8 // 워드 단위, 헤더/푸터 크기
#define DSIZE 16  // 더블
#define CHUNKSIZE (1 << 12) // 청크 크기
#define MAX_HEAP_BLOCKS (1 << 12) // mm_heapcheck의 힙블록 무한루프 감지용. 위는 2^12 bytes, 이건 2^12 blocks.

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc)) // size 및 할당 비트를 워드 1개에 패킹
#define GET(p) (*(size_t *)(p)) // 주소 p에 있는 워드를 읽기
#define PUT(p, val) (*(size_t *)(p) = (val)) // 주소 p에 있는 워드를 쓰기
#define GET_SIZE(p) (GET(p) & ~0x7) // 헤더/푸터에서 크기 비트 읽기
#define GET_ALLOC(p) (GET(p) & 0x1) // 헤더/푸터에서 할당 비트 읽기
#define HDRP(bp) ((char *)(bp) - WSIZE) // 블록 포인터 bp에 대하여, 헤더의 주소를 계산
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 블록 포인터 bp에 대하여, 푸터의 주소를 계산
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // 블록 포인터 bp에 대하여, 앞 블록의 주소를 계산
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // 블록 포인터 bp에 대하여, 다음 블록의 주소를 계산

/* DEBUG 플래그 옵션 - `Makefile`의 `-DDEBUG` */
#ifdef DEBUG 
# define CHKHEAP(lineno) mm_checkheap(lineno)
#else
# define CHKHEAP(l) 
#endif


/* 전역 변수, 함수 시그니처 */
static char *heap_listp = NULL; // 맨 처음 블록 포인터
static char *last_alloctd = NULL; // 마지막 할당 위치 포인터

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void mm_checkheap(int lineno);


/* 메모리 관리자 초기화 */
int mm_init(void){
    /* 빈 힙 생성 */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);                            /* 정렬 패딩 */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* 프롤로그 헤더 */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* 프롤로그 푸터 */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* 에필로그 헤더 */
    heap_listp += (2 * WSIZE);

    /* CHUNKSIZE에 맞추어 빈 힙을 확장 */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

/* extend_heap: 힙을 확장 및 작업 후 해당 위치를 포인터로 반환 */
static void *extend_heap(size_t words){
    char *bp;
    size_t size;

    // words는 2의 배수로 맞추기 - 정렬을 위해
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;

    // 힙을 확장한 후, 새 블록의 헤더/푸터 초기화
    PUT(HDRP(bp), PACK(size, 0));         /* block header 해제 */
    PUT(FTRP(bp), PACK(size, 0));         /* block footer 해제 */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* 새로운 epilogue header */

    // 이전 블록이 free이면 병합
    return coalesce(bp);
}

/**
 *  coalesce: boundary tag의 합치기 및 병합된 블록의 포인터를 반환
 */
static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) // 케이스 1
        return bp;

    if (prev_alloc && !next_alloc){ // 케이스 2
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc){ // 케이스 3
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else { // 케이스 4
        size += (GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    // 아래는 next-fit 시 필요 부분
    last_alloctd = bp;

    return bp;
}

static void *find_fit_first_fit(size_t asize){
    void *bp;

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        if (!GET_ALLOC(HDRP(bp)) && (GET_SIZE(HDRP(bp)) >= asize))
            return bp;
    }
    return NULL; // 못 찾았다
}

/**
 * find_fit: 해당 asize에 맞는 곳 찾기 (next-fit 탐색)
 */
static void *find_fit(size_t asize){
    void *bp;
    
    if (last_alloctd == NULL)
        last_alloctd = heap_listp;
    

    for (bp = last_alloctd; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        if (!GET_ALLOC(HDRP(bp)) && (GET_SIZE(HDRP(bp)) >= asize))
            return bp;
    }
    return NULL; // 못 찾았다
}

/**
 * place: asize 바이트를 bp에 배치
 * 만약 남는 공간이 최소 블록 크기 이상이면 분할
 */
static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));

    if ((csize - asize) >= (2 * DSIZE)){ // 나눌 수 있음
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    }else{ // 못 나눔
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

static void mm_checkheap_v0_1(int lineno){
    printf("Line No.: %d, Heap (%p):\n", lineno, heap_listp);
}

static void mm_checkheap_v0_2(int line) {
    char *bp = heap_listp;
    for (; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        /* 헤더/푸터 불일치 */
        if (GET(HDRP(bp)) != GET(FTRP(bp)))
            printf("헤더/푸터 불일치: %p (line %d)\n", bp, line);
        /* 블록 크기 불일치 체크 */
        if (GET_SIZE(HDRP(bp)) != GET_SIZE(FTRP(bp))) // 헤더/푸터의 크기 비트와 할당 비트가 모두 일치?
            printf("Header/Footer size mismatch at %p (line %d)\n", bp, line);
        if (GET_ALLOC(HDRP(bp)) != GET_ALLOC(FTRP(bp))) // 헤더/푸터의 크기만 일치(alloc 비트는 무시)?
            printf("할당 비트 불일치: %p (line %d)\n", bp, line);
        /* free → prev/next 블록 할당 여부 체크 */
        if (!GET_ALLOC(HDRP(bp))) {
            if (GET_ALLOC(HDRP(PREV_BLKP(bp))) == 1)
                printf("Free block %p has allocated previous block (line %d)\n", bp, line);
            if (GET_ALLOC(HDRP(NEXT_BLKP(bp))) == 1)
                printf("Free block %p has allocated next block (line %d)\n", bp, line);
        }
        /* 전체 힙 순회 중 byte-by-byte fence 검사 */
        if (bp < heap_listp || bp > mem_heap_hi())
            printf("힙 포인터 경계 이상: bp < heap_listp || bp > mem_heap_hi() at %p (line %d)\n", bp, line);
        if (GET_SIZE(HDRP(bp)) == 0)
            printf("힙 포인터가 NULL at %p (line %d)\n", bp, line); if (((size_t)bp % DSIZE) != 0)
            printf("Alignment error at %p (line %d)\n", bp, line);
        /* 블록 크기 체크 */
        if (GET_SIZE(HDRP(bp)) < DSIZE)
            printf("블록 크기 불일치: GET_SIZE(HDRP(bp)) < DSIZE at %p (line %d)\n", bp, line);
    }
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


/**
 * mm_malloc: 최소 size 바이트의 페이로드를 가진 블록 할당
 * size가 0이면 NULL을 반환
 * asize는 헤더와 정렬 요구 사항을 포함한 조정된 블록 크기
 * extendsize는 적합한 블록이 없을 때 힙을 확장할 양
 */
void *mm_malloc(size_t size){
    CHKHEAP(__LINE__);    /* 진입 전 힙 상태 확인 */
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;
    asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE); // 블록 사이즈를 조정하여 overhead와 alignment의 요구사항을 포함

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        CHKHEAP(__LINE__); /* place 직후 확인 */
        return bp;
    }

    // 넣을 곳 없으니 힙을 확장, 이때, WSIZE로 나눠줘야 함
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    place(bp, asize);

    CHKHEAP(__LINE__);    /* 확장 후 다시 확인 */
    return bp;
}

/**
 * mm_free: 블록 해제
 */
void mm_free(void *bp){
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    coalesce(bp); // 이전, 다음 블록 free이면 병합
}

/**
 * mm_realloc: 새 블록 할당하고 이전 껀 해제
 */
void *mm_realloc(void *ptr, size_t size){
    CHKHEAP(__LINE__);    /* 진입 전 힙 상태 확인 */
    if (ptr == NULL)
        return mm_malloc(size);

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    void *newptr = mm_malloc(size);
    if (!newptr)
        return NULL;

    size_t oldsize = GET_SIZE(HDRP(ptr)) - DSIZE; // 헤더에서 블록 크기 읽기
    if (size < oldsize)
        oldsize = size;

    memcpy(newptr, ptr, oldsize); // 걍 그대로 복사
    mm_free(ptr);

    return newptr;
}
