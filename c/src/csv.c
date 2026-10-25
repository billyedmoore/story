#include "internal.h"

typedef struct {
    char **v;
    size_t len, cap;
} ptrvec;

static int vec_push(ptrvec *a, char *p) {
    if (a->len == a->cap) {
        size_t cap = a->cap ? a->cap * 2 : 256;
        char **v = realloc(a->v, cap * sizeof *v);
        if (!v) return -1;
        a->v = v;
        a->cap = cap;
    }
    a->v[a->len++] = p;
    return 0;
}

void sg_table_free(sg_table *t) {
    size_t i, n = t->nrows * t->ncols;
    for (i = 0; i < n; i++) free(t->cells[i]);
    free(t->cells);
    t->cells = NULL;
    t->nrows = t->ncols = 0;
}

/* Finish the current row: pad or truncate it to the header's width. */
static int end_row(ptrvec *cells, size_t *row_cells, sg_table *t) {
    if (t->nrows == 0) t->ncols = *row_cells;
    while (*row_cells < t->ncols) {
        char *e = sg_dup("", 0);
        if (!e || vec_push(cells, e)) {
            free(e);
            return -1;
        }
        (*row_cells)++;
    }
    while (*row_cells > t->ncols) {
        free(cells->v[--cells->len]);
        (*row_cells)--;
    }
    t->nrows++;
    *row_cells = 0;
    return 0;
}

sg_csv_status sg_csv_parse(const char *s, size_t n, sg_table *t, size_t *err_line) {
    ptrvec cells = {0, 0, 0};
    sg_buf f;
    size_t i = 0, row_cells = 0, line = 1;
    sg_csv_status st = SG_CSV_OK;

    t->cells = NULL;
    t->nrows = t->ncols = 0;
    sg_buf_init(&f);
    if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        i = 3;

    while (i < n) {
        int quoted = 0;
        char *cell;
        sg_buf_clear(&f);

        if (s[i] == '"') {
            quoted = 1;
            i++;
            for (;;) {
                if (i >= n) {
                    st = SG_CSV_UNTERMINATED;
                    goto done;
                }
                if (s[i] == '"') {
                    if (i + 1 < n && s[i + 1] == '"') {
                        sg_buf_putc(&f, '"');
                        i += 2;
                        continue;
                    }
                    i++;
                    break;
                }
                if (s[i] == '\n') line++;
                sg_buf_putc(&f, s[i++]);
            }
        }
        /* Unquoted text; also tolerates stray text after a closing quote. */
        while (i < n && s[i] != ',' && s[i] != '\r' && s[i] != '\n')
            sg_buf_putc(&f, s[i++]);

        if (!quoted && f.len == 0 && row_cells == 0 && (i >= n || s[i] != ',')) {
            /* blank line */
            if (i < n) {
                i += (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') ? 2 : 1;
                line++;
            }
            continue;
        }

        if (f.oom) {
            st = SG_CSV_NOMEM;
            goto done;
        }
        cell = sg_dup(sg_buf_str(&f), f.len);
        if (!cell || vec_push(&cells, cell)) {
            free(cell);
            st = SG_CSV_NOMEM;
            goto done;
        }
        row_cells++;

        if (i < n && s[i] == ',') {
            i++;
            continue;
        }
        if (end_row(&cells, &row_cells, t)) {
            st = SG_CSV_NOMEM;
            goto done;
        }
        if (i < n) {
            i += (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') ? 2 : 1;
            line++;
        }
    }
    if (row_cells > 0 && end_row(&cells, &row_cells, t)) st = SG_CSV_NOMEM;

done:
    sg_buf_free(&f);
    if (st != SG_CSV_OK) {
        size_t k;
        for (k = 0; k < cells.len; k++) free(cells.v[k]);
        free(cells.v);
        t->nrows = t->ncols = 0;
        if (err_line) *err_line = line;
        return st;
    }
    t->cells = cells.v;
    return SG_CSV_OK;
}
