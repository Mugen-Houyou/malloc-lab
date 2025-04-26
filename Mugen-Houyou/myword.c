#include <stdio.h>
#include <limits.h>

int main(void) {
    size_t ptr_size = sizeof(void*);             // 포인터 크기 (바이트 단위)
    size_t word_bits = ptr_size * CHAR_BIT;      // 비트 단위

    printf("Pointer size: %zu bytes\n", ptr_size);
    printf("Word size : %zu bits\n", word_bits);

    return 0;
}
