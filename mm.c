/**
 * mm.c v0.1a: Explicit allocator & implicit free list & first-fit 탐색 기반 예제.
 * - header/footer로 크기, 할당 비트 관리
 * - free는 coalescing으로 인접 빈 블록을 병합
 * - realloc은 걍 새 블록 할당 후 복사
 * - split으로 남는 공간 분할
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

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


/* 기본 상수, 매크로 */
#define WSIZE 4 // 워드 단위, 헤더/푸터 크기
#define DSIZE 8  // 더블
#define ALIGNMENT 8                       /* Payload Alignment */
#define CHUNKSIZE (1 << 12) // 청크 크기
#define MIN_BLOCK_SIZE 2*DSIZE                  /* Minimum block size */
#define MAX_HEAP_BLOCKS (1 << 12) // mm_heapcheck의 힙블록 무한루프 감지용. 위는 2^12 bytes, 이건 2^12 blocks.

#define WTYPE u_int32_t                   /* 워드의 타입 */
#define BYTE char                         /* Byte type */


/* Move the address ptr by offset bytes */
#define MOVE_BYTE(ptr, offset) ((WTYPE *)((BYTE *)(ptr) + (offset)))
/* Move the address ptr by offset words */
#define MOVE_WORD(ptr, offset) ((WTYPE *)(ptr) + (offset))
/* Read a word from address ptr */
#define READ_WORD(ptr) (*(WTYPE *)(ptr))
/* Write a word value to address ptr */
#define WRITE_WORD(ptr, value) (*(WTYPE *)(ptr) = (value))
/* Pack the size, prev-allocated and allocation bits into a word */
#define PACKT(size, prev, alloc) ((size) | (prev << 1) | (alloc))
/* Read the size from header/footer word at address Hptr */
#define READ_SIZE(Hptr) (READ_WORD(Hptr) & ~0x7)
/* Read the allocation-bit from header/footer word at address Hptr */
#define READ_ALLOC(Hptr) (READ_WORD(Hptr) & 0x1)
/* Read the prev-allocated-bit from header/footer word at address Hptr */
#define READ_PREV_ALLOC(Hptr) ((READ_WORD(Hptr) & 0x2) >> 1)
/* Write the size, prev-allocated and allocation bits to the word at address Hptr */
#define WRITE(Hptr, size, prev, alloc) (WRITE_WORD((Hptr), PACKT((size), (prev), (alloc))))
/* Write the size to the word at address Hptr */
#define WRITE_SIZE(Hptr, size) (WRITE((Hptr), (size), READ_PREV_ALLOC(Hptr), READ_ALLOC(Hptr)))
/* Write allocation-bit to the word at address Hptr */
#define WRITE_ALLOC(Hptr, alloc)  (WRITE((Hptr), READ_SIZE(Hptr), READ_PREV_ALLOC(Hptr), alloc))
/* Write prev-allocated-bit to the word at address Hptr */
#define WRITE_PREV_ALLOC(Hptr, prev) (WRITE((Hptr), READ_SIZE(Hptr), prev, READ_ALLOC(Hptr)))

/* Get the header-word pointer from the payload pointer pp */
#define HEADER(pp) (MOVE_WORD(pp, -1))
/* Read the block size at the payload pp */
#define BLOCK_SIZE(pp) (READ_SIZE(HEADER(pp)))
/* Get the footer-word pointer from the payload pointer pp */
#define FOOTER(pp) (MOVE_BYTE(pp, (BLOCK_SIZE(pp) - DSIZE)))

/* Gets the block allocation status (alloc-bit) */
#define GET_ALLOCT(pp) (READ_ALLOC(HEADER(pp)))
/* Gets the previous block allocation status (prev-alloc-bit) */
#define GET_PREV_ALLOC(pp) (READ_PREV_ALLOC(HEADER(pp)))

/* Check if the block of the payload pp is free */
#define IS_FREE(pp) (!(GET_ALLOC(pp)))
/* Check if the *previous* block of the payload pp is free */
#define IS_PREV_FREE(pp) (!(GET_PREV_ALLOC(pp)))


/* Get next block payload pointer from pp (current payload pointer) */
#define NEXT_BLOCK(pp) (MOVE_BYTE(pp, BLOCK_SIZE(pp)))
/* Get previous block payload pointer from pp (current payload pointer) */
#define PREV_BLOCK(pp) (MOVE_BYTE(pp, - READ_SIZE(MOVE_WORD(pp, -2))))
/* Read the block size at the payload pp */

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7) // DSIZE (=8)의 배수로 올림 정렬 
#define PACK(size, alloc) ((size) | (alloc)) // size 및 할당 비트를 워드 1개에 패킹
#define GET(p) (*(unsigned int *)(p)) // 주소 p에 있는 워드를 읽기
#define PUT(p, val) (*(unsigned int *)(p) = (val)) // 주소 p에 있는 워드를 쓰기
#define GET_SIZE(p) (GET(p) & ~0x7) // 헤더/푸터에서 크기 비트 읽기
#define GET_ALLOC(p) (GET(p) & 0x1) // 헤더/푸터에서 할당 비트 읽기
#define HDRP(bp) ((char *)(bp) - WSIZE) // 블록 포인터 bp에 대하여, 헤더의 주소를 계산
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 블록 포인터 bp에 대하여, 푸터의 주소를 계산
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // 블록 포인터 bp에 대하여, 앞 블록의 주소를 계산
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // 블록 포인터 bp에 대하여, 다음 블록의 주소를 계산

/* bp: block pointer, sz: 블록 전체 크기(헤더+페이로드+푸터), alloc: 0 또는 1 */
#define SET_HEADER(bp, sz, alloc)  PUT(HDRP(bp), PACK(sz, alloc))
#define SET_FOOTER(bp, sz, alloc)  PUT(FTRP(bp), PACK(sz, alloc))

/* DEBUG 플래그 옵션 - `Makefile`의 `-DDEBUG` */
#ifdef DEBUG
  static void mm_check(int);
  static void check_seglist(int, int);
  static int check_free_list(int, int);
  #define CHKHEAP(line) (mm_check(line))
#else
  #define CHKHEAP(line)
#endif


/* 전역 변수, 함수 시그니처 */
static char *heap_listp = NULL; // 맨 처음 블록 포인터
static char *last_alloctd = NULL; // 마지막 할당 위치 포인터


/* 크기를 16바이트 단위로 맞추되, 헤더·푸터(16바이트) 포함치 */
static inline size_t adjust_block(size_t size) {
    if (size <= DSIZE)
        return 2 * DSIZE;             /* 최소 32B */
    return DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
}

/**
 * find_fit: 해당 asize에 맞는 곳 찾기 (first-fit 탐색)
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

static void *find_fit_ff(size_t asize){
    void *bp;

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
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

void mm_free(void *bp){
    size_t size = GET_SIZE((HDRP(bp)));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

static void *extend_heap(size_t words){
    char* bp;
    size_t size;

    size = (words%2) ? (words+1) * WSIZE : words*WSIZE;
    if ((long)(bp=mem_sbrk(size)) == -1)
        return NULL;

    // 힙을 확장한 후, 새 블록의 헤더/푸터 초기화
    PUT(HDRP(bp), PACK(size, 0));         /* block header 해제 */
    PUT(FTRP(bp), PACK(size, 0));         /* block footer 해제 */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* 새로운 epilogue header */

    // 이전 블록이 free이면 병합
    return coalesce(bp);
}

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

/**
 * mm_realloc: 새 블록 할당하고 이전 껀 해제
 */
void *mm_realloc(void *ptr, size_t size){
    CHKHEAP(__LINE__);    /* 진입 전 힙 상태 확인 */

    /* 얼리 리턴 */
    if (ptr == NULL)
        return mm_malloc(size);

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    /* adjusted size 계산 */
    size_t asize = adjust_block(size);

    /* 현 블록 메타 계산 */
    size_t old_size = GET_SIZE(HDRP(ptr));    // 전체 블록 크기 (헤더/푸터 포함)
    size_t payload_old = old_size - DSIZE ;   // 실제 payload 크기

    /* 축소(shrink) 처리: 블록이 충분히 클 때 */
    if (asize <= old_size) {
        size_t leftover = old_size - asize;
        if (leftover >= MIN_BLOCK_SIZE) {
            // 4.1) 현재 블록을 asize 크기로 설정 && 할당 표시.
            SET_HEADER(ptr, asize, 1);
            SET_FOOTER(ptr, asize, 1);
            // 4.2) 남은 꼬리 부분을 free 블록으로 설정 && free 표시.
            void* tail_bp = NEXT_BLKP(ptr);
            SET_HEADER(tail_bp, leftover, 0);
            SET_FOOTER(tail_bp, leftover, 0);
            coalesce(tail_bp);
        }
        return ptr;
    }

    // 5. 확장(expand) 처리: 다음 블록과 병합 시도
    void* next = NEXT_BLKP(ptr);
    int next_free = (GET_ALLOC(HDRP(next)) == 0);
    size_t next_size = GET_SIZE(HDRP(next));

    if (next_free && ((old_size + next_size) >= asize)){
        size_t merged_size = old_size + next_size;
        // (a) 병합된 블록 전체를 할당 표시
        SET_HEADER(ptr, merged_size, 1);
        SET_FOOTER(ptr, merged_size, 1);
        size_t leftover = merged_size - asize;
        /* [FIX] next-fit용 last_alloctd 보호 */
        if (last_alloctd >= ptr && last_alloctd < NEXT_BLKP(ptr))
            last_alloctd = ptr;   /* 또는 NEXT_BLKP(ptr) */
        
        if (leftover >= MIN_BLOCK_SIZE) {
            // (b) 앞부분만 asize로 재할당
            SET_HEADER(ptr, asize, 1);
            SET_FOOTER(ptr, asize, 1);
            // (c) 꼬리부분을 free 블록으로 표시
            void* tail_bp = NEXT_BLKP(ptr);
            SET_HEADER(tail_bp, leftover, 0);
            SET_FOOTER(tail_bp, leftover, 0);
            coalesce(tail_bp);
        }
        return ptr;
    }

    // 6. 마지막 보루: 새 블록 할당 + 복사 + 해제
    void * newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    size_t copy_bytes = MIN(payload_old, size);
    memcpy(newptr, ptr, copy_bytes);
    mm_free(ptr);
    return newptr;
}

void *mm_realloc_orig(void *ptr, size_t size){
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

/**
 * mm_malloc: 최소 size 바이트의 페이로드를 가진 블록 할당
 * size가 0이면 NULL을 반환
 * asize는 헤더와 정렬 요구 사항을 포함한 조정된 블록 크기
 * extendsize는 적합한 블록이 없을 때 힙을 확장할 양
 */
void *mm_malloc(size_t size){
    // CHKHEAP(__LINE__);    /* 진입 전 힙 상태 확인 */
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;

    asize = (size <= DSIZE) ? \
        2*DSIZE : \
        DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE); // 블록 사이즈를 조정하여 overhead와 alignment의 요구사항을 포함

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        // CHKHEAP(__LINE__); /* place 직후 확인 */
        return bp;
    }

    // 넣을 곳 없으니 힙을 확장, 이때, WSIZE로 나눠줘야 함
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    place(bp, asize);

    // CHKHEAP(__LINE__);    /* 확장 후 다시 확인 */
    return bp;
}


/* ========================== Debugging Functions =============================== */
#ifdef DEBUG

/*****************************************************************************
 * mm_checkheap(line) 
 *    - DEBUG 빌드에서 CHKHEAP(line) 매크로를 통해 자동 호출됨
 *    - 오류 발견 시 fprintf(stderr, ...) 후에도 계속 스캔 → 여러 문제 한 번에 보기
 ****************************************************************************/
static void mm_checkheap(int line) {
    char *bp;
    int errs = 0;

#ifdef VERBOSE
    fprintf(stderr, "\n[mm_checkheap @ line %d]\n", line);
#endif

    /* -------------------------------------------------- */
    /* 0. 프롤로그/에필로그 헤더 무결성                     */
    /* -------------------------------------------------- */
    if (GET_SIZE(HDRP(heap_listp)) != DSIZE || !GET_ALLOC(HDRP(heap_listp))) {
        fprintf(stderr, "❌ Bad prologue header (line %d)\n", line);
        ++errs;
    }

    /* -------------------------------------------------- */
    /* 1. 힙 전체 순회                                    */
    /* -------------------------------------------------- */
    for (bp = NEXT_BLKP(heap_listp);                      /* prologue의 payload */
         GET_SIZE(HDRP(bp)) > 0;               /* size==0 => epilogue */
         bp = NEXT_BLKP(bp)) {

        size_t hsize  = GET_SIZE(HDRP(bp));
        size_t halloc = GET_ALLOC(HDRP(bp));
        size_t fsize  = GET_SIZE(FTRP(bp));
        size_t falloc = GET_ALLOC(FTRP(bp));

#ifdef VERBOSE
        fprintf(stderr,
                "  [%p] size %-4zu %s\n",
                bp, hsize, halloc ? "alloc" : "free ");
#endif

        /* 1-A. 8-byte 정렬 확인 (payload 기준) */
        if ((uintptr_t)bp % ALIGNMENT) {
            fprintf(stderr, "❌ %p not 8-byte aligned\n", bp);
            ++errs;
        }

        /* 1-B. 최소 크기 & 8B 배수 */
        if (hsize < MIN_BLOCK_SIZE || hsize % ALIGNMENT) {
            fprintf(stderr, "❌ Bad block size %zu @ %p\n", hsize, bp);
            ++errs;
        }

        /* 1-C. free 블록 → 헤더·푸터 일치 확인 */
        if (!halloc && (hsize != fsize || falloc != 0)) {
            fprintf(stderr, "❌ Header/footer mismatch @ %p\n", bp);
            ++errs;
        }

        /* 1-D. 연속 두 free 블록 금지 */
        if (!halloc && !GET_ALLOC(HDRP(NEXT_BLKP(bp))) &&
            GET_SIZE(HDRP(NEXT_BLKP(bp))) > 0) {
            fprintf(stderr,
                    "❌ Two consecutive free blocks @ %p and %p\n",
                    bp, NEXT_BLKP(bp));
            ++errs;
        }

        /* 1-E. 힙 경계 검증 */
        if (bp < (char *)mem_heap_lo() || bp > (char *)mem_heap_hi()) {
            fprintf(stderr, "❌ Block %p out of heap bounds\n", bp);
            ++errs;
            break;          /* 다음 블록 주소도 이미 의미 없음 */
        }
    }

    /* -------------------------------------------------- */
    /* 2. 에필로그 헤더 검증                              */
    /* -------------------------------------------------- */
    if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp))) {
        fprintf(stderr, "❌ Bad epilogue header\n");
        ++errs;
    }

    /* -------------------------------------------------- */
    /* 3. next-fit용 last_alloctd 유효성                  */
    /* -------------------------------------------------- */
    if (last_alloctd) {
        if (last_alloctd < (char *)mem_heap_lo() ||
            last_alloctd > (char *)mem_heap_hi()) {
            fprintf(stderr, "❌ last_alloctd (%p) out of heap\n", last_alloctd);
            ++errs;
        } else {
            /* 블록 시작점인지 확인 */
            int ok = 0;
            for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
                if (bp == last_alloctd) { ok = 1; break; }
            }
            if (!ok) {
                fprintf(stderr,
                        "❌ last_alloctd (%p) does not point to block start\n",
                        last_alloctd);
                ++errs;
            }
        }
    }

#ifdef VERBOSE
    fprintf(stderr, "[mm_checkheap] done: %d error(s)\n", errs);
#endif
}

/* ────────────────────────────────────────────────────────────────────────── *
 * mm_check(line)
 *   - 호출 위치의 소스 코드 라인 번호를 받아 전체 힙 일관성 검사를 수행
 *   - 지금은 암시적(implicit) free-list만 쓰므로 check_seglist()는 빈 스텁
 *   - ‘-DDEBUG’ 로 컴파일할 때마다 CHKHEAP(line) 매크로를 통해 자동 호출됨
 * ────────────────────────────────────────────────────────────────────────── */
static void mm_check(int line)
{
    /* 1) 힙 전체를 훑으며 헤더/푸터·정렬·coalescing 등을 점검 */
    mm_checkheap(line);

    /* 2) (향후용) 명시적 free-list/세그리게이티드 리스트의 무결성 점검 */
    check_seglist(line, /*verbose=*/0);
}

/* ── 아래 두 함수는 ‘암시적 리스트’ 단계에서는 빈 껍데기 ───────────── */
static void check_seglist(int line, int verbose)
{
    (void)line;     /* 매개변수를 사용하지 않을 때 컴파일러 경고 방지 */
    (void)verbose;
    /* 현재 구현은 implicit free-list → 별도 검사 필요 없음 */
}

static int check_free_list(int idx, int verbose)
{
    (void)idx;
    (void)verbose;
    /* 오류가 없음을 0으로 나타냄(추후 구현 시 실제 에러 수 반환) */
    return 0;
}

#endif 
/* ========================== End of Debugging Functions =============================== */
