int printf(char *str, ...);

#include "test_define.h"

#ifndef test001nop
#define test001nop
#define test001int int
int test001()
{
    test001int a = 0;
    if (a != 0) printf("[ERROR] test001:1: a != 0\n");

#define test001test \
    if (a != 0) printf("[ERROR] test001:2: a != 0\n");
    test001test;

    if (test001header != 42) printf("[ERROR] test001:3: test001header != 42\n");
    if (test001iret(334) != 334)
        printf("[ERROR] test001:4: test001iret(334) != 334\n");
}
#ifndef test001nop
#define test001nop
#define test001int int
int test001()
{
    test001int a = 0;
    if (a != 0) printf("[ERROR] test001:1: a != 0\n");

#define test001test \
    if (a != 0) printf("[ERROR] test001:2: a != 0\n");
    test001test;

    if (test001header != 42) printf("[ERROR] test001:3: test001header != 42\n");
    if (test001iret(334) != 334)
        printf("[ERROR] test001:4: test001iret(334) != 334\n");
}
#endif
#endif

#ifndef test001nop
#define test001nop
#define test001int int
int test001()
{
    test001int a = 0;
    if (a != 0) printf("[ERROR] test001:1: a != 0\n");

#define test001test \
    if (a != 0) printf("[ERROR] test001:2: a != 0\n");
    test001test;

    if (test001header != 42) printf("[ERROR] test001:3: test001header != 42\n");
    if (test001iret(334) != 334)
        printf("[ERROR] test001:4: test001iret(334) != 334\n");
}
#endif

int main() { test001(); }
