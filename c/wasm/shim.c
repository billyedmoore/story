/*
 * Minimal libc for the freestanding wasm32 build: a first-fit free-list
 * allocator over linear memory plus the few mem/str routines the library
 * needs. Compiled with -fno-builtin so the loops are not turned back into
 * calls to themselves.
 */
#include <stddef.h>
#include <stdint.h>

#define ALIGN 16u
#define PAGE 65536u

typedef struct hdr {
    size_t size; /* payload bytes, multiple of ALIGN */
    size_t used;
} hdr;
#define HDR ((size_t)sizeof(hdr)) /* 16 on wasm32, so payloads stay 16-aligned */

extern unsigned char __heap_base;
static uintptr_t heap_lo, heap_top;

static void heap_init(void) {
    heap_lo = ((uintptr_t)&__heap_base + ALIGN - 1) & ~(uintptr_t)(ALIGN - 1);
    heap_top = heap_lo;
}

static int heap_extend(size_t need) {
    uintptr_t have = (uintptr_t)__builtin_wasm_memory_size(0) * PAGE;
    if (heap_top + need > have) {
        size_t pages = (heap_top + need - have + PAGE - 1) / PAGE;
        if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) return -1;
    }
    return 0;
}

void *malloc(size_t n) {
    hdr *h;
    if (!heap_lo) heap_init();
    if (n == 0) n = 1;
    if (n > (size_t)-1 / 2) return NULL;
    n = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    for (h = (hdr *)heap_lo; (uintptr_t)h < heap_top; h = (hdr *)((char *)h + HDR + h->size)) {
        if (h->used || h->size < n) continue;
        if (h->size >= n + HDR + ALIGN) { /* split */
            hdr *rest = (hdr *)((char *)h + HDR + n);
            rest->size = h->size - n - HDR;
            rest->used = 0;
            h->size = n;
        }
        h->used = 1;
        return (char *)h + HDR;
    }
    if (heap_extend(HDR + n)) return NULL;
    h = (hdr *)heap_top;
    h->size = n;
    h->used = 1;
    heap_top += HDR + n;
    return (char *)h + HDR;
}

void free(void *p) {
    hdr *h, *next;
    if (!p) return;
    ((hdr *)((char *)p - HDR))->used = 0;
    /* coalesce adjacent free blocks */
    for (h = (hdr *)heap_lo; (uintptr_t)h < heap_top; h = (hdr *)((char *)h + HDR + h->size)) {
        if (h->used) continue;
        while ((uintptr_t)(next = (hdr *)((char *)h + HDR + h->size)) < heap_top && !next->used)
            h->size += HDR + next->size;
    }
}

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dp = d;
    const unsigned char *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp < sp) {
        while (n--) *dp++ = *sp++;
    } else {
        dp += n;
        sp += n;
        while (n--) *--dp = *--sp;
    }
    return d;
}

void *memset(void *d, int c, size_t n) {
    unsigned char *dp = d;
    while (n--) *dp++ = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

void *realloc(void *p, size_t n) {
    hdr *h;
    void *q;
    if (!p) return malloc(n);
    h = (hdr *)((char *)p - HDR);
    if (n <= h->size) return p;
    if (!(q = malloc(n))) return NULL;
    memcpy(q, p, h->size);
    free(p);
    return q;
}

void *calloc(size_t a, size_t b) {
    void *p;
    if (b && a > (size_t)-1 / b) return NULL;
    p = malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}
