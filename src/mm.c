#include <stdlib.h>
#include "mm.h"

static void (*_lightev_mm_free)(void * p) = NULL;
static void *(*_lightev_mm_malloc)(size_t n) = NULL;
static void *(*_lightev_mm_realloc)(void * p, size_t n) = NULL;



int lightev_mm_setup(mm_malloc_func_t m, mm_realloc_func_t r, mm_free_func_t f) {
    _lightev_mm_malloc = m;
    _lightev_mm_realloc = r;
    _lightev_mm_free = f;
}

void * lightev_mm_malloc(size_t sz)
{
	if (_lightev_mm_malloc)
		return _lightev_mm_malloc(sz);
	else
		return malloc(sz);
}


void lightev_mm_free(void * ptr)
{
	if (_lightev_mm_free)
		_lightev_mm_free(ptr);
	else
		free(ptr);
}

void * lightev_mm_realloc(void *ptr, size_t sz)
{
	if (_lightev_mm_realloc)
		return _lightev_mm_realloc(ptr, sz);
	else
		return realloc(ptr, sz);
}