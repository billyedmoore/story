#include "internal.h"

typedef struct {
    const char *s;
    size_t n, i;
    sg2gr_error *err;
} jp;

static int fail(jp *j, const char *msg) {
    sg_set_err(j->err, SG2GR_ERR_JSON, msg, "byte", j->i);
    return SG2GR_ERR_JSON;
}

static int nomem(jp *j) {
    sg_set_err(j->err, SG2GR_ERR_NOMEM, "out of memory", NULL, 0);
    return SG2GR_ERR_NOMEM;
}

static void ws(jp *j) {
    while (j->i < j->n && (j->s[j->i] == ' ' || j->s[j->i] == '\t' ||
                           j->s[j->i] == '\n' || j->s[j->i] == '\r'))
        j->i++;
}

static int hex4(jp *j, uint32_t *out) {
    uint32_t v = 0;
    int k;
    if (j->i + 4 > j->n) return -1;
    for (k = 0; k < 4; k++) {
        char c = j->s[j->i + k];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return -1;
    }
    j->i += 4;
    *out = v;
    return 0;
}

/* Parses a JSON string at j->i into a new NUL-terminated buffer. */
static int parse_string(jp *j, char **out) {
    sg_buf b;
    sg_buf_init(&b);
    if (j->i >= j->n || j->s[j->i] != '"') return fail(j, "expected string");
    j->i++;
    for (;;) {
        unsigned char c;
        if (j->i >= j->n) {
            sg_buf_free(&b);
            return fail(j, "unterminated string");
        }
        c = (unsigned char)j->s[j->i++];
        if (c == '"') break;
        if (c < 0x20) {
            sg_buf_free(&b);
            return fail(j, "control character in string");
        }
        if (c != '\\') {
            sg_buf_putc(&b, (char)c);
            continue;
        }
        if (j->i >= j->n) {
            sg_buf_free(&b);
            return fail(j, "unterminated escape");
        }
        c = (unsigned char)j->s[j->i++];
        switch (c) {
        case '"': case '\\': case '/': sg_buf_putc(&b, (char)c); break;
        case 'b': sg_buf_putc(&b, '\b'); break;
        case 'f': sg_buf_putc(&b, '\f'); break;
        case 'n': sg_buf_putc(&b, '\n'); break;
        case 'r': sg_buf_putc(&b, '\r'); break;
        case 't': sg_buf_putc(&b, '\t'); break;
        case 'u': {
            uint32_t cp, lo;
            if (hex4(j, &cp)) {
                sg_buf_free(&b);
                return fail(j, "bad \\u escape");
            }
            if (cp >= 0xD800 && cp <= 0xDBFF && j->i + 1 < j->n &&
                j->s[j->i] == '\\' && j->s[j->i + 1] == 'u') {
                size_t save = j->i;
                j->i += 2;
                if (!hex4(j, &lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                else
                    j->i = save;
            }
            sg_buf_put_utf8(&b, cp); /* lone surrogates become U+FFFD */
            break;
        }
        default:
            sg_buf_free(&b);
            return fail(j, "bad escape");
        }
    }
    *out = sg_buf_take(&b);
    return *out ? SG2GR_OK : nomem(j);
}

static int skip_value(jp *j, int depth);

static int skip_container(jp *j, char close, int depth) {
    j->i++;
    ws(j);
    if (j->i < j->n && j->s[j->i] == close) {
        j->i++;
        return SG2GR_OK;
    }
    for (;;) {
        int rc;
        ws(j);
        if (close == '}') {
            char *k;
            if ((rc = parse_string(j, &k))) return rc;
            free(k);
            ws(j);
            if (j->i >= j->n || j->s[j->i] != ':') return fail(j, "expected ':'");
            j->i++;
        }
        if ((rc = skip_value(j, depth + 1))) return rc;
        ws(j);
        if (j->i < j->n && j->s[j->i] == ',') {
            j->i++;
            continue;
        }
        if (j->i < j->n && j->s[j->i] == close) {
            j->i++;
            return SG2GR_OK;
        }
        return fail(j, "expected ',' or closing bracket");
    }
}

static int skip_value(jp *j, int depth) {
    char c;
    if (depth > 32) return fail(j, "nesting too deep");
    ws(j);
    if (j->i >= j->n) return fail(j, "unexpected end of input");
    c = j->s[j->i];
    if (c == '"') {
        char *s;
        int rc = parse_string(j, &s);
        if (!rc) free(s);
        return rc;
    }
    if (c == '{') return skip_container(j, '}', depth);
    if (c == '[') return skip_container(j, ']', depth);
    if (c == '-' || (c >= '0' && c <= '9') || c == 't' || c == 'f' || c == 'n') {
        while (j->i < j->n && ((j->s[j->i] >= 'a' && j->s[j->i] <= 'z') ||
                               (j->s[j->i] >= '0' && j->s[j->i] <= '9') ||
                               j->s[j->i] == '-' || j->s[j->i] == '+' ||
                               j->s[j->i] == '.' || j->s[j->i] == 'E'))
            j->i++;
        return SG2GR_OK;
    }
    return fail(j, "unexpected character");
}

static int key_is(const char *k, const char *lit) {
    return strlen(k) == strlen(lit) && memcmp(k, lit, strlen(lit)) == 0;
}

static int parse_detail(jp *j, sg_override *e) {
    j->i++; /* '{' */
    ws(j);
    if (j->i < j->n && j->s[j->i] == '}') {
        j->i++;
        return SG2GR_OK;
    }
    for (;;) {
        char *k;
        int rc;
        ws(j);
        if ((rc = parse_string(j, &k))) return rc;
        ws(j);
        if (j->i >= j->n || j->s[j->i] != ':') {
            free(k);
            return fail(j, "expected ':'");
        }
        j->i++;
        ws(j);
        if (key_is(k, "isbn") || key_is(k, "format")) {
            char **slot = key_is(k, "isbn") ? &e->isbn : &e->format;
            free(*slot);
            *slot = NULL;
            rc = parse_string(j, slot);
        } else if (key_is(k, "exclude")) {
            if (j->n - j->i >= 4 && memcmp(j->s + j->i, "true", 4) == 0) {
                e->exclude = 1;
                j->i += 4;
            } else if (j->n - j->i >= 5 && memcmp(j->s + j->i, "false", 5) == 0) {
                e->exclude = 0;
                j->i += 5;
            } else {
                rc = fail(j, "\"exclude\" must be true or false");
            }
        } else {
            rc = skip_value(j, 1);
        }
        free(k);
        if (rc) return rc;
        ws(j);
        if (j->i < j->n && j->s[j->i] == ',') {
            j->i++;
            continue;
        }
        if (j->i < j->n && j->s[j->i] == '}') {
            j->i++;
            return SG2GR_OK;
        }
        return fail(j, "expected ',' or '}'");
    }
}

void sg2gr_overrides_free(sg2gr_overrides *ov) {
    size_t i;
    if (!ov) return;
    for (i = 0; i < ov->n; i++) {
        free(ov->e[i].prefix);
        free(ov->e[i].isbn);
        free(ov->e[i].format);
    }
    free(ov->e);
    free(ov);
}

sg2gr_status sg2gr_overrides_parse(const char *json, size_t len,
                                   sg2gr_overrides **out, sg2gr_error *err) {
    jp j;
    sg2gr_overrides *ov;
    int rc = SG2GR_OK;

    if (err) {
        err->status = SG2GR_OK;
        err->message[0] = '\0';
    }
    if (!json || !out) {
        sg_set_err(err, SG2GR_ERR_ARGS, "null argument", NULL, 0);
        return SG2GR_ERR_ARGS;
    }
    *out = NULL;
    j.s = json;
    j.n = len;
    j.i = 0;
    j.err = err;
    ov = calloc(1, sizeof *ov);
    if (!ov) return (sg2gr_status)nomem(&j);

    if (len >= 3 && (unsigned char)json[0] == 0xEF) j.i = 3; /* BOM */
    ws(&j);
    if (j.i >= j.n || j.s[j.i] != '{') {
        rc = fail(&j, "overrides must be a JSON object");
        goto bad;
    }
    j.i++;
    ws(&j);
    if (j.i < j.n && j.s[j.i] == '}') {
        j.i++;
        goto trailing;
    }
    for (;;) {
        sg_override ent, *grown;
        char *key;
        ws(&j);
        if ((rc = parse_string(&j, &key))) goto bad;
        if (!key[0]) {
            free(key);
            rc = fail(&j, "empty title prefix");
            goto bad;
        }
        memset(&ent, 0, sizeof ent);
        ent.prefix = key;
        ws(&j);
        if (j.i >= j.n || j.s[j.i] != ':') {
            rc = fail(&j, "expected ':'");
        } else {
            j.i++;
            ws(&j);
            if (j.i < j.n && j.s[j.i] == '"')
                rc = parse_string(&j, &ent.isbn);
            else if (j.i < j.n && j.s[j.i] == '{')
                rc = parse_detail(&j, &ent);
            else
                rc = fail(&j, "value must be an ISBN string or an object");
        }
        if (rc) {
            free(ent.prefix);
            free(ent.isbn);
            free(ent.format);
            goto bad;
        }
        grown = realloc(ov->e, (ov->n + 1) * sizeof *ov->e);
        if (!grown) {
            free(ent.prefix);
            free(ent.isbn);
            free(ent.format);
            rc = nomem(&j);
            goto bad;
        }
        ov->e = grown;
        ov->e[ov->n++] = ent;
        ws(&j);
        if (j.i < j.n && j.s[j.i] == ',') {
            j.i++;
            continue;
        }
        if (j.i < j.n && j.s[j.i] == '}') {
            j.i++;
            break;
        }
        rc = fail(&j, "expected ',' or '}'");
        goto bad;
    }
trailing:
    ws(&j);
    if (j.i != j.n) {
        rc = fail(&j, "unexpected text after JSON object");
        goto bad;
    }
    *out = ov;
    return SG2GR_OK;
bad:
    sg2gr_overrides_free(ov);
    return (sg2gr_status)rc;
}
