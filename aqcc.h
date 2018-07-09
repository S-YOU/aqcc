#pragma once
#ifndef AQCC_AQCC_H

#include <stdlib.h>

typedef struct {
    size_t size, rsved_size;
    void **data;
} Vector;

typedef struct {
    Vector *tokens;
    size_t idx;
} TokenSeq;

enum {
    AST_ADD,
    AST_SUB,
    AST_MUL,
    AST_DIV,
    AST_REM,
    AST_INT,
};

typedef struct AST AST;
struct AST {
    int kind;

    union {
        int ival;

        struct {
            AST *lhs, *rhs;
        };
    };
};

// error("msg", __FILE__, __LINE__);
void error(const char *msg, const char *filename, int lineno);

void *safe_malloc(size_t size);
void *safe_realloc(void *ptr, size_t size);

Vector *new_vector();
void vector_push_back(Vector *this, void *item);
void *vector_get(Vector *this, size_t i);

AST *parse_expr(TokenSeq *tokseq);

#endif
