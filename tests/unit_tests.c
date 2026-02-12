#define UNIT_TEST
#include "../genstree.c"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_prepend() {
    printf("Testing prepend...\n");
    char *res = prepend(1, "e", "0|1|");
    assert(strcmp(res, "e0|e1|") == 0);
    free(res);

    res = prepend(2, "0,", "e|1|");
    assert(strcmp(res, "0,e|0,1|") == 0);
    free(res);
    printf("test_prepend passed!\n");
}

void test_concat3() {
    printf("Testing concat3...\n");
    char *res = concat3("a", "b", "c");
    assert(strcmp(res, "abc") == 0);
    free(res);
    printf("test_concat3 passed!\n");
}

void test_count_leaves() {
    printf("Testing count_leaves...\n");
    assert(count_leaves(2, 1, 2) == 3);
    assert(count_leaves(3, 1, 3) == 5);
    assert(count_leaves(1, 2, 4) == 1);
    printf("test_count_leaves passed!\n");
}

void test_label_lth_leaf() {
    printf("Testing label_lth_leaf...\n");
    char *res = label_lth_leaf(2, 1, 2, 1);
    fprintf(stderr, "Result 1: %s\n", res);
    assert(strcmp(res, "00e,|") == 0);
    free(res);

    res = label_lth_leaf(2, 1, 2, 2);
    fprintf(stderr, "Result 2: %s\n", res);
    assert(strcmp(res, "0e,|") == 0);
    free(res);

    res = label_lth_leaf(2, 1, 2, 3);
    fprintf(stderr, "Result 3: %s\n", res);
    assert(strcmp(res, "01e,|") == 0);
    free(res);
    printf("test_label_lth_leaf passed!\n");
}

int main() {
    test_prepend();
    test_concat3();
    test_count_leaves();
    test_label_lth_leaf();
    printf("All unit tests passed!\n");
    return 0;
}
