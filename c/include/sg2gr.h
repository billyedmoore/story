/*
 * sg2gr - convert a StoryGraph library export (CSV) to Goodreads CSV format.
 *
 * Freestanding-friendly: the library does no I/O. It takes the input as a
 * buffer and returns the output as a malloc'd buffer, so it builds natively
 * and for WebAssembly alike.
 */
#ifndef SG2GR_H
#define SG2GR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define SG2GR_API EMSCRIPTEN_KEEPALIVE
#elif defined(__wasm__)
#define SG2GR_API __attribute__((visibility("default")))
#else
#define SG2GR_API
#endif

#define SG2GR_VERSION "0.1.0"
#define SG2GR_ERRMSG_MAX 128

typedef enum {
    SG2GR_OK = 0,
    SG2GR_ERR_NOMEM,
    SG2GR_ERR_ARGS,
    SG2GR_ERR_CSV,            /* malformed CSV input */
    SG2GR_ERR_NOT_STORYGRAPH, /* CSV parsed, but required columns are missing */
    SG2GR_ERR_JSON            /* malformed overrides document */
} sg2gr_status;

typedef struct {
    sg2gr_status status;
    char message[SG2GR_ERRMSG_MAX];
} sg2gr_error;

typedef struct {
    size_t rows_in;
    size_t rows_out;
    size_t rows_excluded;
    size_t rows_blank_status; /* books with no Read Status (converted as to-read) */
    /* Titles of those books, one per line (NULL if none). Owned by the caller:
     * release with sg2gr_stats_free() after every successful sg2gr_convert(). */
    char *blank_status_titles;
} sg2gr_stats;

/*
 * Overrides: a JSON object keyed by title prefix. Each value is either an
 * ISBN string, or an object with optional "isbn", "format" and "exclude".
 *
 *   { "The Example Book": "9780306406157",
 *     "Another Title": { "isbn": "9780306406157", "format": "paperback" },
 *     "A Book To Leave Out": { "exclude": true } }
 */
typedef struct sg2gr_overrides sg2gr_overrides;

/* `err` may be NULL in every call below. */
SG2GR_API sg2gr_status sg2gr_overrides_parse(const char *json, size_t len,
                                             sg2gr_overrides **out, sg2gr_error *err);
SG2GR_API void sg2gr_overrides_free(sg2gr_overrides *ov);

/*
 * Convert a StoryGraph export. `ov` and `stats` may be NULL. On success
 * *out is a NUL-terminated buffer (length *out_len, excluding the NUL) that
 * the caller releases with sg2gr_free().
 */
SG2GR_API sg2gr_status sg2gr_convert(const char *csv, size_t csv_len,
                                     const sg2gr_overrides *ov,
                                     char **out, size_t *out_len,
                                     sg2gr_stats *stats, sg2gr_error *err);

SG2GR_API void sg2gr_stats_free(sg2gr_stats *stats);
SG2GR_API void *sg2gr_alloc(size_t n);
SG2GR_API void sg2gr_free(void *p);
SG2GR_API const char *sg2gr_version(void);

#ifdef __cplusplus
}
#endif
#endif
