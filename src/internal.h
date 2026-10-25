#ifndef SG2GR_INTERNAL_H
#define SG2GR_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "sg2gr.h"

#ifdef SG2GR_FREESTANDING
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
#else
#include <stdlib.h>
#include <string.h>
#endif

/* Growable, always NUL-terminated byte buffer. Allocation failure is sticky. */
typedef struct {
    char *p;
    size_t len, cap;
    int oom;
} sg_buf;

void sg_buf_init(sg_buf *b);
void sg_buf_free(sg_buf *b);
void sg_buf_clear(sg_buf *b);
int sg_buf_put(sg_buf *b, const char *s, size_t n);
int sg_buf_puts(sg_buf *b, const char *s);
int sg_buf_putc(sg_buf *b, char c);
int sg_buf_put_utf8(sg_buf *b, uint32_t cp);
const char *sg_buf_str(const sg_buf *b); /* never NULL */
char *sg_buf_take(sg_buf *b);            /* transfers ownership; NULL on OOM */

char *sg_dup(const char *s, size_t n);
void sg_set_err(sg2gr_error *err, sg2gr_status st, const char *msg,
                const char *label, size_t num);

/* RFC 4180 CSV. Row 0 is the header; every row is normalised to ncols cells. */
typedef struct {
    char **cells; /* nrows * ncols NUL-terminated strings */
    size_t nrows, ncols;
} sg_table;

typedef enum { SG_CSV_OK = 0, SG_CSV_NOMEM, SG_CSV_UNTERMINATED } sg_csv_status;

sg_csv_status sg_csv_parse(const char *s, size_t n, sg_table *t, size_t *err_line);
void sg_table_free(sg_table *t);

typedef struct {
    char *prefix;
    char *isbn;   /* NULL = not overridden */
    char *format; /* NULL = not overridden */
    int exclude;
} sg_override;

struct sg2gr_overrides {
    sg_override *e;
    size_t n;
};

#endif
