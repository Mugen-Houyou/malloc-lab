#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void){
    char *p1;
    char *p2;
    int i;

    p1 = (char *)malloc(16);
    if (p1 == NULL){
        fprintf(stderr, "malloc failed for p1\n");
        return 1;
    }
    
    strcpy(p1, "Hello! This is p1.");
    free(p2);
    printf("p1 before free: %s\n", p1);
    free(p1);

    p2 = (char *)malloc(32);
    if (p2 == NULL){
        fprintf(stderr, "malloc failed for p2\n");
        return 1;
    }
    for (i = 0; i < 6; i++)
        p2[i] = 'A' + i;
    p2[6] = '\0';
	
    printf("p2 before free: %s\n", p2);
    free(p2);
    printf("p2 after double free: %s\n", p2);
    free(p2);
    free(p2);

    return 0;
}
