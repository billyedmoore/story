#include "internal.h"

void sg_buf_init(sg_buf *b) {
    b->p = NULL;
    b->len = b->cap = 0;
    b->oom = 0;
}

void sg_buf_free(sg_buf *b) {
    free(b->p);
    sg_buf_init(b);
}

void sg_buf_clear(sg_buf *b) {
    b->len = 0;
    if (b->p) b->p[0] = '\0';
}

static int grow(sg_buf *b, size_t extra) {
    size_t want, cap;
    char *p;
    if (b->oom) return -1;
    if (extra > (size_t)-1 - b->len - 1) {
        b->oom = 1;
        return -1;
    }
    want = b->len + extra + 1;
    if (want <= b->cap) return 0;
    cap = b->cap ? b->cap : 64;
    while (cap < want) {
        if (cap > (size_t)-1 / 2) {
            cap = want;
            break;
        }
        cap *= 2;
    }
    p = realloc(b->p, cap);
    if (!p) {
        b->oom = 1;
        return -1;
    }
    b->p = p;
    b->cap = cap;
    return 0;
}

int sg_buf_put(sg_buf *b, const char *s, size_t n) {
    if (grow(b, n)) return -1;
    if (n) memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
    return 0;
}

int sg_buf_puts(sg_buf *b, const char *s) { return sg_buf_put(b, s, strlen(s)); }
int sg_buf_putc(sg_buf *b, char c) { return sg_buf_put(b, &c, 1); }

int sg_buf_put_utf8(sg_buf *b, uint32_t cp) {
    char t[4];
    size_t n;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        t[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        t[0] = (char)(0xC0 | (cp >> 6));
        t[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        t[0] = (char)(0xE0 | (cp >> 12));
        t[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        t[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        t[0] = (char)(0xF0 | (cp >> 18));
        t[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        t[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        t[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    return sg_buf_put(b, t, n);
}

const char *sg_buf_str(const sg_buf *b) { return b->p ? b->p : ""; }

char *sg_buf_take(sg_buf *b) {
    char *p;
    if (b->oom) {
        sg_buf_free(b);
        return NULL;
    }
    if (!b->p && sg_buf_put(b, "", 0)) return NULL;
    p = b->p;
    sg_buf_init(b);
    return p;
}

char *sg_dup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (p) {
        if (n) memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

/* Writes "msg (label N)" into err->message, truncating to fit. */
void sg_set_err(sg2gr_error *err, sg2gr_status st, const char *msg,
                const char *label, size_t num) {
    size_t i = 0, k;
    char digits[24];
    if (!err) return;
    err->status = st;
#define PUT(c) do { if (i < SG2GR_ERRMSG_MAX - 1) err->message[i++] = (c); } while (0)
    for (k = 0; msg[k]; k++) PUT(msg[k]);
    if (label) {
        PUT(' ');
        PUT('(');
        for (k = 0; label[k]; k++) PUT(label[k]);
        PUT(' ');
        k = 0;
        do {
            digits[k++] = (char)('0' + num % 10);
            num /= 10;
        } while (num);
        while (k) PUT(digits[--k]);
        PUT(')');
    }
#undef PUT
    err->message[i] = '\0';
}
