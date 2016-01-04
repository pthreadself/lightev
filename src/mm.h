#ifndef _MM_H
#define _MM_H

typedef void mm_free_func_t(void * p);
typedef void * mm_malloc_func_t(size_t n);
typedef void * mm_realloc_func_t(void * p, size_t n);

int lightev_mm_setup(mm_malloc_func_t m, mm_realloc_func_t r, mm_free_func_t f);

void * lightev_mm_malloc(size_t sz);

void lightev_mm_free(void * ptr);

void * lightev_mm_realloc(void *ptr, size_t sz);

#endif