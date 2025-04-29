/**
 * mm.c v0.3: Explicit allocator & __ free list & first-fit 탐색 기반 & mm_realloc 개선판.
 * - header/footer로 크기, 할당 비트 관리
 * - free는 coalescing으로 인접 빈 블록을 병합
 * - realloc은 in-place shrink/expand 적용
 * - split으로 남는 공간 분할
 */
#include <time.h>
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
#define WSIZE 4             // 워드 단위, 헤더/푸터 크기
#define WTYPE u_int32_t     // 워드의 타입
#define DSIZE 8             // 더블 워드
#define PTR_SIZE         sizeof(void*)          // 포인터 크기
#define MIN_BLOCK_SIZE   ((WSIZE*2 + PTR_SIZE*2 + (DSIZE-1)) / DSIZE * DSIZE)
// #define MIN_BLOCK_SIZE 24   // explicit free list일 때 블록의 최소 사이즈 - header(4B) + prev(4B) + next(4B) + footer(4B) = 16B. 64비트 아키텍처면 header(4B) + prev(8B) + next(8B) + footer(4B) = 24B.
// #define MIN_BLOCK_SIZE 16   // 블록의 최소 사이즈 - 즉 2*DSIZE
#define ALIGNMENT 8         // Payload Alignment - 위 MIN_BLOCK_SIZE는 이 숫자의 배수여야 함.
#define BYTE char           // Byte type
#define CHUNKSIZE (1 << 12) // 청크 크기
#define MAX_HEAP_BLOCKS (1 << 12) // mm_heapcheck에서, 힙 블록 무한루프 감지용. MIN_BLOCK_SIZE랑은 상관 없는 개념이며 단위도 다름. 위는 bytes, 이건 2^12 blocks.


/* 유틸 매크로 */
// /* Move the address ptr by offset bytes */
// #define MOVE_BYTE(ptr, offset) ((WTYPE *)((BYTE *)(ptr) + (offset)))
// /* Move the address ptr by offset words */
// #define MOVE_WORD(ptr, offset) ((WTYPE *)(ptr) + (offset))
// /* Read a word from address ptr */
// #define READ_WORD(ptr) (*(WTYPE *)(ptr))
// /* Write a word value to address ptr */
// #define WRITE_WORD(ptr, value) (*(WTYPE *)(ptr) = (value))
// /* Pack the size, prev-allocated and allocation bits into a word */
// #define PACKT(size, prev, alloc) ((size) | (prev << 1) | (alloc))
// /* Read the size from header/footer word at address Hptr */
// #define READ_SIZE(Hptr) (READ_WORD(Hptr) & ~0x7) // ~0x7  == 111111...1000 ==> 하위 3비트 제외한 나머지만 남김 (즉, 블록 크기만 남김)
// /* Read the allocation-bit from header/footer word at address Hptr */
// #define READ_ALLOC(Hptr) (READ_WORD(Hptr) & 0x1) // 0x1  == 000000...0001 ==> 최하위 비트만 남김 (즉, 할당 여부만 남김)
// /* Read the prev-allocated-bit from header/footer word at address Hptr */
// #define READ_PREV_ALLOC(Hptr) ((READ_WORD(Hptr) & 0x2) >> 1)
// /* Write the size, prev-allocated and allocation bits to the word at address Hptr */
// #define WRITE(Hptr, size, prev, alloc) (WRITE_WORD((Hptr), PACKT((size), (prev), (alloc))))
// /* Write the size to the word at address Hptr */
// #define WRITE_SIZE(Hptr, size) (WRITE((Hptr), (size), READ_PREV_ALLOC(Hptr), READ_ALLOC(Hptr)))
// /* Write allocation-bit to the word at address Hptr */
// #define WRITE_ALLOC(Hptr, alloc)  (WRITE((Hptr), READ_SIZE(Hptr), READ_PREV_ALLOC(Hptr), alloc))
// /* Write prev-allocated-bit to the word at address Hptr */
// #define WRITE_PREV_ALLOC(Hptr, prev) (WRITE((Hptr), READ_SIZE(Hptr), prev, READ_ALLOC(Hptr)))
// /* Get the header-word pointer from the payload pointer pp */
// #define HEADER(pp) (MOVE_WORD(pp, -1))
// /* Read the block size at the payload pp */
// #define BLOCK_SIZE(pp) (READ_SIZE(HEADER(pp)))
// /* Get the footer-word pointer from the payload pointer pp */
// #define FOOTER(pp) (MOVE_BYTE(pp, (BLOCK_SIZE(pp) - DSIZE)))
// /* Gets the block allocation status (alloc-bit) */
// #define GET_ALLOCT(pp) (READ_ALLOC(HEADER(pp)))
// /* Gets the previous block allocation status (prev-alloc-bit) */
// #define GET_PREV_ALLOC(pp) (READ_PREV_ALLOC(HEADER(pp)))
// /* Check if the block of the payload pp is free */
// #define IS_FREE(pp) (!(GET_ALLOC(pp)))
// /* Check if the *previous* block of the payload pp is free */
// #define IS_PREV_FREE(pp) (!(GET_PREV_ALLOC(pp)))
// /* Get next block payload pointer from pp (current payload pointer) */
// #define NEXT_BLOCK(pp) (MOVE_BYTE(pp, BLOCK_SIZE(pp)))
// /* Get previous block payload pointer from pp (current payload pointer) */
// #define PREV_BLOCK(pp) (MOVE_BYTE(pp, - READ_SIZE(MOVE_WORD(pp, -2))))

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

/* Explicit free list 구현을 위함 */
#define PRED_PTR(bp)    ((char **)(bp))                        // 이전 블록의 위치가 든 위치
#define SUCC_PTR(bp)    ((char **)((char *)(bp) + sizeof(void*)))  // 다음 블록의 위치가 든 위치
#define GET_PRED(bp)    (*(PRED_PTR(bp)))   // 이전 블록 위치를 얻기 
#define GET_SUCC(bp)    (*(SUCC_PTR(bp)))   // 다음 블록 위치를 얻기
#define SET_PRED(bp, p) (*(PRED_PTR(bp)) = (p))  // 이전 블록 위치를 설정
#define SET_SUCC(bp, q) (*(SUCC_PTR(bp)) = (q))  // 다음 블록 위치를 설정

// Segregated free list 구현을 위함.
#define CLASS_CNT 10          // Segregated free list 구현용. 블록 크기 클래스 수.


/* DEBUG 플래그 옵션 - `Makefile`의 `-DDEBUG` */
#ifdef DEBUG
    static void mm_checkheap(int line) ;
    #define CHKHEAP(line) (mm_checkheap(line))
#else
    #define CHKHEAP(line)
#endif


/* 전역 변수 */
static char *heap_listp = NULL; // 맨 처음 블록 포인터
static void *free_list_head = NULL; // Explicit free list의 출발점
static void *seg_head[CLASS_CNT];  // Segregated free list에서, 각 클래스의 free-list 헤드


/** 참고: 함수에 `static`는 왜 붙이는가? 
 *        - 내부 연결(internal linkage)을 의미. 
 *        - 같은 소스 파일(translation unit) 안에서는 호출할 수 있지만, 다른 파일에서는 보이지 않는다(링커가 못 찾음).
 *        - 이게 없으면, 외부 소스 파일에서 extern void extend_heap(); 식으로 참조 가능
 * 
 ** 참고: 함수에 `inline`는 왜 붙이는가? 
 *        - 컴파일러에 “이 함수를 호출하는 곳에 본문을 직접 삽입(inline expansion)해도 좋다”고 알림.
 *        - 걍 '좋다'는 거지, 반드시 인라인 삽입을 보장하진 않음.
*/

/**
 * class_index: 크기를 받아서 어느 클래스인지 돌려줌.
 * Segregated free list용.
 */
static inline int class_index(size_t size){
    if (size <= 16) return 0;
    if (size <= 32) return 1;
    if (size <= 64) return 2;
    if (size <= 128) return 3;
    if (size <= 256) return 4;
    if (size <= 512) return 5;
    if (size <= 1024) return 6;
    if (size <= 2048) return 7;
    if (size <= 4096) return 8;
    return 9; // 4096 초과 
}

/** 
 * adjust_block: 크기를 MIN_BLOCK_SIZE 단위로 맞추되, 헤더 & 푸터(16 바이트) 포함치
 */
static inline size_t adjust_block(size_t size) {
    size_t asize = DSIZE * ((size + DSIZE + (DSIZE-1)) / DSIZE);
    return (asize < MIN_BLOCK_SIZE) ? MIN_BLOCK_SIZE : asize;
}

/**
 * insert_node: 빈 블록 `bp`를 explicit free list의 머리에 LIFO로 삽입
 */
static void insert_node(void* bp){
    SET_SUCC(bp, free_list_head);
    SET_PRED(bp, NULL);

    if (free_list_head != NULL)
        SET_PRED(free_list_head, bp);

    free_list_head = bp;
}

/**
 * remove_node: 빈 블록 `bp`를 리스트에서 제거
 */
static void remove_node(void* bp){
    void *pred = GET_PRED(bp);
    void *succ = GET_SUCC(bp);

    
    /* 1) bp의 predecessor가 있으면, 그 successor를 bp의 successor로 */
    if (pred != NULL) {
        SET_SUCC(pred, succ);
    } else {
        /* bp가 헤드였다면 헤드를 successor로 교체 */
        free_list_head = succ;
    }

    if (succ != NULL) {
        SET_PRED(succ, pred);
    }
}

/**
 * find_fit: 해당 asize에 맞는 곳 찾기 (first-fit 탐색)
 */
static void *find_fit(size_t asize){ // 얘는 기존의 first-fit 탐색
    void *bp;

    for (bp = free_list_head; bp != NULL; bp = GET_SUCC(bp)){
        if (GET_SIZE(HDRP(bp)) >= asize)
            return bp;
    }
    return NULL; // 못 찾았다
}

/**
 * place: asize 바이트를 bp에 할당
 * 1) free list에서 제거
 * 2) 분할 가능 시 split
 * 3) header/footer 마킹
 */
static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));

    /* 1) 할당 전 리스트에서 제거 */
    remove_node(bp); // free 리스트에서 블록을 즉시 제거 => 할당 중인 상태가 리스트에 남지 않도록 함

    /* 2) 분할이 가능 */
    if ((csize - asize) >= MIN_BLOCK_SIZE){
        SET_HEADER(bp, asize, 1);
        SET_FOOTER(bp, asize, 1);

        bp = NEXT_BLKP(bp);

        SET_HEADER(bp, csize - asize, 0);
        SET_FOOTER(bp, csize - asize, 0);

        /* 꼬리 블록을 free list에 삽입 */
        insert_node(bp); // 남은 부분을 free list에 다시 추가

    /* 3) 분할이 불가능 */
    }else{ 
        SET_HEADER(bp, csize, 1);
        SET_FOOTER(bp, csize, 1);
    }

}

/**
 *  coalesce: boundary tag의 합치기 및 병합된 블록의 포인터를 반환
 */
static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    
    /* 1) 병합 대상이 될 수 있는 인접 free 블록은 리스트에서 제거 */
    if (!prev_alloc)
        remove_node(PREV_BLKP(bp));
    if (!next_alloc)
        remove_node(NEXT_BLKP(bp));

    /* 2) 실제 메모리상 병합 */
    if (prev_alloc && next_alloc){ // 케이스 1: 앞, 뒤 블록 모두 alloc
        // return bp;
    } else if (prev_alloc && !next_alloc){ // 케이스 2: 앞 alloc, 뒷 free
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        SET_HEADER(bp,size,0);
        SET_FOOTER(bp,size,0);
    } else if (!prev_alloc && next_alloc){ // 케이스 3: 앞 free, 뒷 alloc
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        SET_HEADER(bp,size,0);
        SET_FOOTER(bp,size,0);
    } else { // 케이스 4: 앞, 뒤 블록 모두 free
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        bp = PREV_BLKP(bp);
        SET_HEADER(bp, size, 0);
        SET_FOOTER(bp, size, 0);
    }

    // // 아래는 next-fit 시 필요 부분
    // last_alloctd = bp;

    return bp;
}

void mm_free(void *bp){
    size_t size = GET_SIZE((HDRP(bp)));

    SET_HEADER(bp, size, 0);
    SET_FOOTER(bp, size, 0);

    bp = coalesce(bp);

    insert_node(bp);
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
 * mm_realloc (개선판): 새 블록 할당하고 이전 껀 해제
 *          - 이 개선판은 in-place shrink/expand가 적용됨
 */
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL)
        return mm_malloc(size);  // ptr이 NULL이면 malloc과 같은 방식으로 처리
    if (size == 0) {
        mm_free(ptr);  // size가 0이면 해당 블록을 free하고 NULL 반환
        return NULL;
    }

    size_t oldsize = GET_SIZE(HDRP(ptr));  // 기존 블록의 크기 가져오기
    size_t asize = adjust_block(size);

    if (asize <= oldsize)
        return ptr;  // 기존 크기가 충분하면 기존 포인터 그대로 반환

    void *next = NEXT_BLKP(ptr);  // 다음 블록 주소
    if (!GET_ALLOC(HDRP(next)) && (oldsize + GET_SIZE(HDRP(next))) >= asize) {
        remove_node(next);  // 만약 옆 블록이 free이고 크기가 충분하면 병합
        size_t newsize = oldsize + GET_SIZE(HDRP(next));  // 병합 후 새로운 크기
        PUT(HDRP(ptr), PACK(newsize, 1));  // 헤더 업데이트
        PUT(FTRP(ptr), PACK(newsize, 1));  // 푸터 업데이트
        return ptr;  // 병합된 블록 반환
    }

    void *newptr = mm_malloc(size);  // 병합할 수 없다면 새로운 메모리 할당
    if (newptr == NULL)
        return NULL;  // 할당 실패하면 NULL 반환

    size_t copySize = oldsize - DSIZE;  // 기존 데이터 크기

    if (size < copySize)
        copySize = size;  // 복사할 크기를 요청된 크기로 맞춤

    memcpy(newptr, ptr, copySize);  // 데이터 복사
    mm_free(ptr);  // 기존 블록은 free
    return newptr;  // 새로운 포인터 반환
}

/**
 * mm_malloc: 최소 size 바이트의 페이로드를 가진 블록 할당
 * size가 0이면 NULL을 반환
 * asize는 헤더와 정렬 요구 사항을 포함한 조정된 블록 크기
 * extendsize는 적합한 블록이 없을 때 힙을 확장할 양
 */
void *mm_malloc(size_t size){
    if (size == 0)
        return NULL;

    /* 1. 요청 크기 보정 */
    size_t asize = adjust_block(size);

    /* 2. free list에서 first-fit 탐색 */
    void *bp = find_fit(asize);
    if (bp != NULL) {

        place(bp, asize); // place 안에서 remove_node → split/insert_node
        return bp;
    }

    /* 3. 적합 블록이 없으니 힙 확장 */
    size_t extendsize = MAX(asize, CHUNKSIZE);
    bp = extend_heap(extendsize / WSIZE);   // bp는 free 블록

    if (bp == NULL)
        return NULL;

    insert_node(bp); // 새 free 블록 bp를 리스트에 넣어야만 place/remove_node가 정상 동작함!

    /* 4. 이제 바로 할당 */
    place(bp, asize);

    return bp;
}


/* ========================== Debugging Functions =============================== */
#ifdef DEBUG

static void mm_checkheap(int line) {
    char *bp;
    int errors = 0;
    fprintf(stderr, "\n[mm_checkheap @ line %d]\n", line);

    /* 0. Prologue 검사 */
    bp = heap_listp;
    if (GET_SIZE(HDRP(bp)) != DSIZE || !GET_ALLOC(HDRP(bp))) {
        fprintf(stderr, "❌ Bad prologue header at %p\n", bp);
        errors++;
    }

    /* 1. 힙 전체 순회: 블록 일관성 검사 */
    for (bp = NEXT_BLKP(bp); GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        size_t hsize  = GET_SIZE(HDRP(bp));
        size_t halloc = GET_ALLOC(HDRP(bp));
        size_t fsize  = GET_SIZE(FTRP(bp));
        size_t falloc = GET_ALLOC(FTRP(bp));

        /* 1-A. 헤더↔푸터 크기·할당 비트 일치 */
        if (hsize != fsize) {
            fprintf(stderr, "❌ Size mismatch at %p: header %zu vs footer %zu\n", bp, hsize, fsize);
            errors++;
        }
        if (halloc != falloc) {
            fprintf(stderr, "❌ Alloc bit mismatch at %p: header %zu vs footer %zu\n", bp, halloc, falloc);
            errors++;
        }

        /* 1-B. 정렬 검사 */
        if (((uintptr_t)bp % ALIGNMENT) != 0) {
            fprintf(stderr, "❌ Alignment error at %p\n", bp);
            errors++;
        }

        /* 1-C. 최소 크기 검사 */
        if (hsize < MIN_BLOCK_SIZE) {
            fprintf(stderr, "❌ Block too small at %p: size %zu\n", bp, hsize);
            errors++;
        }

        /* 1-D. 힙 경계 검사 */
        if ((char *)HDRP(bp) < (char *)mem_heap_lo() ||
            (char *)FTRP(bp) > (char *)mem_heap_hi()) {
            fprintf(stderr, "❌ Block %p out of heap bounds\n", bp);
            errors++;
        }

        /* 1-E. 연속된 두 free 블록 금지 */
        if (!halloc) {
            void *nxt = NEXT_BLKP(bp);
            if (GET_SIZE(HDRP(nxt)) > 0 && !GET_ALLOC(HDRP(nxt))) {
                fprintf(stderr, "❌ Two consecutive free blocks at %p and %p\n", bp, nxt);
                errors++;
            }
        }
    }

    /* 2. Epilogue 검사 */
    if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp))) {
        fprintf(stderr, "❌ Bad epilogue header at %p\n", bp);
        errors++;
    }
        
    /* 3. Free list 일관성 검사 (단일 → 분리 리스트 버전) */
    for (int i = 0; i < LISTS; i++) {
        void *f;
        int count = 0;
        for (f = seg_free_head[i]; f != NULL; f = GET_SUCC(f)) {
            /* 3-A. alloc 비트 확인 */
            if (GET_ALLOC(HDRP(f))) {
                fprintf(stderr, "❌ [list %d] block %p marked allocated\n", i, f);
                errors++;
            }
            /* 3-B. 클래스 인덱스 일치 확인 */
            int idx = class_index(GET_SIZE(HDRP(f)));
            if (idx != i) {
                fprintf(stderr, "❌ block %p in list %d should be in list %d\n",
                        f, i, idx);
                errors++;
            }
            /* 3-C. pred/succ 일관성 */
            void *p = GET_PRED(f), *s = GET_SUCC(f);
            if (p && GET_SUCC(p) != f) {
                fprintf(stderr, "❌ [list %d] pred(%p)->succ != %p\n", i, p, f);
                errors++;
            }
            if (s && GET_PRED(s) != f) {
                fprintf(stderr, "❌ [list %d] succ(%p)->pred != %p\n", i, s, f);
                errors++;
            }
            /* 3-D. 무한 루프 방지 */
            if (++count > MAX_HEAP_BLOCKS) {
                fprintf(stderr, "❌ [list %d] cycle detected\n", i);
                errors++;
                break;
            }
        }
    }
        
    /* 4. 힙상의 모든 free 블록이 정확히 한 분리 리스트에 있어야 함 */
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp))) {
            int found = 0;
            for (int i = 0; i < LISTS && !found; i++) {
                for (void *f = seg_free_head[i]; f != NULL; f = GET_SUCC(f)) {
                    if (f == bp) { found = 1; break; }
                }
            }
            if (!found) {
                fprintf(stderr, "❌ Free block %p not in any free list\n", bp);
                errors++;
            }
        }
    }

    if (errors)
        fprintf(stderr, "[mm_checkheap] %d error(s) detected\n", errors);
}

#endif  /* DEBUG */

/* ========================== End of Debugging Functions =============================== */
