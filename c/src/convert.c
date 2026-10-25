#include "internal.h"

typedef struct {
    const char *p;
    size_t n;
} slice;

enum {
    C_TITLE, C_AUTHORS, C_CONTRIBUTORS, C_ISBN, C_FORMAT, C_STATUS, C_ADDED,
    C_LASTREAD, C_READCOUNT, C_RATING, C_REVIEW, C_TAGS, C_OWNED, C_NCOLS
};

static const char *const SG_COLS[C_NCOLS] = {
    "Title", "Authors", "Contributors", "ISBN/UID", "Format", "Read Status",
    "Date Added", "Last Date Read", "Read Count", "Star Rating", "Review",
    "Tags", "Owned?"
};

static const char GR_HEADER[] =
    "Book Id,Title,Author,Author l-f,Additional Authors,ISBN,ISBN13,My Rating,"
    "Average Rating,Publisher,Binding,Number of Pages,Year Published,"
    "Original Publication Year,Date Read,Date Added,Bookshelves,"
    "Bookshelves with positions,Exclusive Shelf,My Review,Spoiler,Private Notes,"
    "Read Count,Owned Copies\n";

/* ---- slices ---------------------------------------------------------- */

static slice mk(const char *s) {
    slice r;
    r.p = s;
    r.n = strlen(s);
    return r;
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static slice trim(slice s) {
    while (s.n && is_space(s.p[0])) {
        s.p++;
        s.n--;
    }
    while (s.n && is_space(s.p[s.n - 1])) s.n--;
    return s;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int eq_ci(slice s, const char *lit) {
    size_t i, n = strlen(lit);
    if (s.n != n) return 0;
    for (i = 0; i < n; i++)
        if (lower(s.p[i]) != lit[i]) return 0;
    return 1;
}

/* Next comma-separated, trimmed, non-empty item. Returns 0 at the end. */
static int next_item(slice *rest, slice *item) {
    while (rest->n) {
        size_t k = 0;
        slice it;
        while (k < rest->n && rest->p[k] != ',') k++;
        it.p = rest->p;
        it.n = k;
        rest->p += (k < rest->n) ? k + 1 : k;
        rest->n -= (k < rest->n) ? k + 1 : k;
        it = trim(it);
        if (it.n) {
            *item = it;
            return 1;
        }
    }
    return 0;
}

/* ---- output ----------------------------------------------------------- */

static void put_csv(sg_buf *out, const char *s, size_t n) {
    size_t i;
    int quote = 0;
    for (i = 0; i < n; i++)
        if (s[i] == ',' || s[i] == '"' || s[i] == '\n' || s[i] == '\r') {
            quote = 1;
            break;
        }
    if (!quote) {
        sg_buf_put(out, s, n);
        return;
    }
    sg_buf_putc(out, '"');
    for (i = 0; i < n; i++) {
        if (s[i] == '"') sg_buf_putc(out, '"');
        sg_buf_putc(out, s[i]);
    }
    sg_buf_putc(out, '"');
}

static void field(sg_buf *out, int *first, const char *s, size_t n) {
    if (!*first) sg_buf_putc(out, ',');
    *first = 0;
    put_csv(out, s, n);
}

static void field_s(sg_buf *out, int *first, const char *s) { field(out, first, s, strlen(s)); }
static void field_sl(sg_buf *out, int *first, slice s) { field(out, first, s.p, s.n); }

/* ---- field mappers ----------------------------------------------------- */

/* "Name (Translator)" -> "Name" */
static slice strip_role(slice s) {
    s = trim(s);
    if (s.n && s.p[s.n - 1] == ')') {
        size_t k = s.n;
        while (k > 0 && s.p[k - 1] != '(') k--;
        if (k > 1) {
            s.n = k - 1;
            s = trim(s);
        }
    }
    return s;
}

/* "Jane Q. Doe" -> "Doe, Jane Q." */
static void put_last_first(sg_buf *b, slice name) {
    size_t k = name.n;
    while (k > 0 && name.p[k - 1] != ' ') k--;
    if (k == 0) {
        sg_buf_put(b, name.p, name.n);
        return;
    }
    sg_buf_put(b, name.p + k, name.n - k);
    sg_buf_puts(b, ", ");
    sg_buf_put(b, name.p, trim((slice){name.p, k}).n);
}

static void join_item(sg_buf *b, slice it) {
    if (b->len) sg_buf_puts(b, ", ");
    sg_buf_put(b, it.p, it.n);
}

/* Splits an ISBN/UID into ISBN-10 / ISBN-13; anything else (e.g. ASIN) is dropped. */
static void split_isbn(slice s, slice *i10, slice *i13, char scratch[16]) {
    size_t i, k = 0;
    int digits_only = 1;
    s = trim(s);
    for (i = 0; i < s.n && k < 15; i++) {
        if (s.p[i] == '-' || s.p[i] == ' ') continue;
        scratch[k++] = s.p[i];
    }
    scratch[k] = '\0';
    i10->p = i13->p = scratch;
    i10->n = i13->n = 0;
    for (i = 0; i < k; i++)
        if (scratch[i] < '0' || scratch[i] > '9') digits_only = 0;
    if (k == 13 && digits_only) {
        i13->n = 13;
    } else if (k == 10) {
        for (i = 0; i < 9; i++)
            if (scratch[i] < '0' || scratch[i] > '9') return;
        if ((scratch[9] >= '0' && scratch[9] <= '9') || scratch[9] == 'X' || scratch[9] == 'x')
            i10->n = 10;
    }
}

static void put_isbn_field(sg_buf *out, int *first, slice v) {
    sg_buf t;
    sg_buf_init(&t);
    sg_buf_puts(&t, "=\"");
    sg_buf_put(&t, v.p, v.n);
    sg_buf_putc(&t, '"');
    field(out, first, sg_buf_str(&t), t.len);
    sg_buf_free(&t);
}

/* StoryGraph allows quarter stars, Goodreads only whole ones: round halves up. */
static int whole_stars(slice s) {
    int v = 0;
    size_t i = 0;
    s = trim(s);
    while (i < s.n && s.p[i] >= '0' && s.p[i] <= '9') {
        if (v < 100) v = v * 10 + (s.p[i] - '0');
        i++;
    }
    if (i + 1 < s.n && s.p[i] == '.' && s.p[i + 1] >= '5' && s.p[i + 1] <= '9') v++;
    return v > 5 ? 5 : v;
}

static const char *binding(slice f) {
    f = trim(f);
    if (eq_ci(f, "hardcover")) return "Hardcover";
    if (eq_ci(f, "paperback")) return "Paperback";
    if (eq_ci(f, "digital")) return "Kindle Edition";
    if (eq_ci(f, "audio")) return "Audiobook";
    return "";
}

static int entity(slice s, size_t i, uint32_t *cp, size_t *used) {
    static const struct { const char *name; uint32_t cp; } named[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '}
    };
    size_t k, end = i + 1;
    while (end < s.n && end - i < 10 && s.p[end] != ';' && s.p[end] != '&' && s.p[end] != '<')
        end++;
    if (end >= s.n || s.p[end] != ';' || end == i + 1) return 0;
    if (s.p[i + 1] == '#') {
        uint32_t v = 0;
        int hex = end > i + 2 && (s.p[i + 2] == 'x' || s.p[i + 2] == 'X');
        size_t d = i + 2 + (hex ? 1 : 0);
        if (d >= end) return 0;
        for (; d < end; d++) {
            char c = s.p[d];
            uint32_t digit;
            if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
            else if (hex && lower(c) >= 'a' && lower(c) <= 'f') digit = (uint32_t)(lower(c) - 'a' + 10);
            else return 0;
            v = v * (hex ? 16 : 10) + digit;
            if (v > 0x10FFFF) return 0;
        }
        *cp = v;
        *used = end - i + 1;
        return 1;
    }
    for (k = 0; k < sizeof named / sizeof named[0]; k++) {
        size_t nl = strlen(named[k].name);
        if (nl == end - i - 1 && memcmp(s.p + i + 1, named[k].name, nl) == 0) {
            *cp = named[k].cp;
            *used = end - i + 1;
            return 1;
        }
    }
    return 0;
}

static int strcmp_lit(const char *a, const char *b) {
    size_t n = strlen(a);
    return n == strlen(b) ? memcmp(a, b, n) : 1;
}

/* Newlines are never emitted at the start of the text, nor doubled by block ends. */
static void newline_once(sg_buf *b) {
    if (b->len && b->p[b->len - 1] != '\n') sg_buf_putc(b, '\n');
}

/* StoryGraph reviews are rich-text HTML; Goodreads gets plain text. */
static void html_to_text(sg_buf *b, slice s) {
    size_t i = 0;
    while (i < s.n) {
        char c = s.p[i];
        if (c == '<') {
            size_t j = i + 1, nstart, nend;
            int closing = 0;
            char name[8];
            size_t k;
            while (j < s.n && s.p[j] != '>') j++;
            if (j < s.n) {
                if (s.p[i + 1] == '/') closing = 1;
                nstart = i + 1 + (size_t)closing;
                nend = nstart;
                while (nend < j && nend - nstart < 7 && s.p[nend] != ' ' &&
                       s.p[nend] != '/' && s.p[nend] != '>')
                    nend++;
                for (k = 0; k < nend - nstart; k++) name[k] = lower(s.p[nstart + k]);
                name[k] = '\0';
                if (!strcmp_lit(name, "br")) {
                    if (b->len) sg_buf_putc(b, '\n');
                }
                else if (closing && (!strcmp_lit(name, "div") || !strcmp_lit(name, "p") ||
                                     !strcmp_lit(name, "li") || !strcmp_lit(name, "blockquote")))
                    newline_once(b);
                i = j + 1;
                continue;
            }
        } else if (c == '&') {
            uint32_t cp;
            size_t used;
            if (entity(s, i, &cp, &used)) {
                sg_buf_put_utf8(b, cp);
                i += used;
                continue;
            }
        }
        sg_buf_putc(b, c);
        i++;
    }
    while (b->len && is_space(b->p[b->len - 1])) b->p[--b->len] = '\0';
}

/* ---- conversion ------------------------------------------------------- */

static int find_col(const sg_table *t, const char *name) {
    size_t c;
    slice want = mk(name);
    for (c = 0; c < t->ncols; c++) {
        slice h = trim(mk(t->cells[c]));
        if (h.n == want.n && memcmp(h.p, want.p, h.n) == 0) return (int)c;
    }
    return -1;
}

static slice cell(const sg_table *t, size_t row, const int *col, int which) {
    return col[which] < 0 ? mk("") : mk(t->cells[row * t->ncols + (size_t)col[which]]);
}

static int prefix_match(const char *title, const char *prefix) {
    size_t n = strlen(prefix);
    return strlen(title) >= n && memcmp(title, prefix, n) == 0;
}

static void write_book(sg_buf *out, size_t book_id, slice title, slice authors_s,
                       slice contributors_s, slice isbn_s, slice format_s,
                       slice status_s, slice added, slice lastread, slice readcount,
                       slice rating, slice review, slice tags, slice owned) {
    int first = 1;
    char num[24], scratch[16];
    size_t nl = 0;
    slice i10, i13, rest, item, shown_author = mk("");
    const char *shelf = "to-read", *custom = NULL;
    sg_buf lf, extra, shelves, text;
    sg_buf_init(&lf);
    sg_buf_init(&extra);
    sg_buf_init(&shelves);
    sg_buf_init(&text);

    {   /* Book Id */
        size_t v = book_id, k = 0;
        char tmp[24];
        do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (k) num[nl++] = tmp[--k];
        num[nl] = '\0';
    }

    rest = authors_s;
    if (next_item(&rest, &item)) {
        shown_author = item;
        put_last_first(&lf, item);
        while (next_item(&rest, &item)) join_item(&extra, item);
    }
    rest = contributors_s;
    while (next_item(&rest, &item)) {
        item = strip_role(item);
        if (item.n) join_item(&extra, item);
    }

    split_isbn(isbn_s, &i10, &i13, scratch);

    status_s = trim(status_s);
    if (eq_ci(status_s, "read")) shelf = "read";
    else if (eq_ci(status_s, "currently-reading")) shelf = "currently-reading";
    else if (eq_ci(status_s, "to-read")) shelf = "to-read";
    else if (eq_ci(status_s, "did-not-finish")) { shelf = "read"; custom = "did-not-finish"; }
    else if (eq_ci(status_s, "paused")) { shelf = "currently-reading"; custom = "paused"; }

    if (custom) sg_buf_puts(&shelves, custom);
    rest = tags;
    while (next_item(&rest, &item)) join_item(&shelves, item);

    html_to_text(&text, review);

    field(out, &first, num, nl);
    field_sl(out, &first, title);
    field_sl(out, &first, shown_author);
    field(out, &first, sg_buf_str(&lf), lf.len);
    field(out, &first, sg_buf_str(&extra), extra.len);
    put_isbn_field(out, &first, i10);
    put_isbn_field(out, &first, i13);
    {
        char r[2];
        r[0] = (char)('0' + whole_stars(rating));
        r[1] = '\0';
        field_s(out, &first, r);
    }
    field_s(out, &first, "");                  /* Average Rating */
    field_s(out, &first, "");                  /* Publisher */
    field_s(out, &first, binding(format_s));
    field_s(out, &first, "");                  /* Number of Pages */
    field_s(out, &first, "");                  /* Year Published */
    field_s(out, &first, "");                  /* Original Publication Year */
    if (!custom && !strcmp_lit(shelf, "read")) field_sl(out, &first, trim(lastread));
    else field_s(out, &first, "");
    field_sl(out, &first, trim(added));
    field(out, &first, sg_buf_str(&shelves), shelves.len);
    field_s(out, &first, "");                  /* Bookshelves with positions */
    field_s(out, &first, shelf);
    field(out, &first, sg_buf_str(&text), text.len);
    field_s(out, &first, "");                  /* Spoiler */
    field_s(out, &first, "");                  /* Private Notes */
    readcount = trim(readcount);
    if (readcount.n) field_sl(out, &first, readcount);
    else field_s(out, &first, "0");
    field_s(out, &first, eq_ci(trim(owned), "yes") ? "1" : "0");
    sg_buf_putc(out, '\n');

    sg_buf_free(&lf);
    sg_buf_free(&extra);
    sg_buf_free(&shelves);
    sg_buf_free(&text);
}

sg2gr_status sg2gr_convert(const char *csv, size_t csv_len, const sg2gr_overrides *ov,
                           char **out, size_t *out_len, sg2gr_stats *stats,
                           sg2gr_error *err) {
    sg_table t;
    sg_buf o, blank;
    sg2gr_stats st;
    int col[C_NCOLS];
    size_t r, e, line = 0;
    int c;

    memset(&st, 0, sizeof st);
    if (err) {
        err->status = SG2GR_OK;
        err->message[0] = '\0';
    }
    if (stats) *stats = st;
    if (!csv || !out) {
        sg_set_err(err, SG2GR_ERR_ARGS, "null argument", NULL, 0);
        return SG2GR_ERR_ARGS;
    }
    *out = NULL;
    if (out_len) *out_len = 0;

    switch (sg_csv_parse(csv, csv_len, &t, &line)) {
    case SG_CSV_OK: break;
    case SG_CSV_NOMEM:
        sg_set_err(err, SG2GR_ERR_NOMEM, "out of memory", NULL, 0);
        return SG2GR_ERR_NOMEM;
    default:
        sg_set_err(err, SG2GR_ERR_CSV, "unterminated quoted field", "line", line);
        return SG2GR_ERR_CSV;
    }

    for (c = 0; c < C_NCOLS; c++) col[c] = find_col(&t, SG_COLS[c]);
    if (t.nrows == 0 || col[C_TITLE] < 0 || col[C_STATUS] < 0) {
        sg_table_free(&t);
        sg_set_err(err, SG2GR_ERR_NOT_STORYGRAPH,
                   "not a StoryGraph export (missing Title / Read Status column)", NULL, 0);
        return SG2GR_ERR_NOT_STORYGRAPH;
    }

    sg_buf_init(&o);
    sg_buf_init(&blank);
    sg_buf_puts(&o, GR_HEADER);

    for (r = 1; r < t.nrows; r++) {
        const char *title = t.cells[r * t.ncols + (size_t)col[C_TITLE]];
        slice isbn = cell(&t, r, col, C_ISBN), format = cell(&t, r, col, C_FORMAT);
        int excluded = 0;
        st.rows_in++;

        for (e = 0; ov && e < ov->n; e++) {
            if (!prefix_match(title, ov->e[e].prefix)) continue;
            if (ov->e[e].exclude) excluded = 1;
            if (ov->e[e].isbn) isbn = mk(ov->e[e].isbn);
            if (ov->e[e].format) format = mk(ov->e[e].format);
        }
        if (excluded) {
            st.rows_excluded++;
            continue;
        }
        if (!trim(cell(&t, r, col, C_STATUS)).n) {
            size_t k;
            st.rows_blank_status++;
            if (blank.len) sg_buf_putc(&blank, '\n');
            for (k = 0; title[k]; k++) sg_buf_putc(&blank, title[k] == '\n' || title[k] == '\r' ? ' ' : title[k]);
        }

        write_book(&o, ++st.rows_out, mk(title), cell(&t, r, col, C_AUTHORS),
                   cell(&t, r, col, C_CONTRIBUTORS), isbn, format,
                   cell(&t, r, col, C_STATUS), cell(&t, r, col, C_ADDED),
                   cell(&t, r, col, C_LASTREAD), cell(&t, r, col, C_READCOUNT),
                   cell(&t, r, col, C_RATING), cell(&t, r, col, C_REVIEW),
                   cell(&t, r, col, C_TAGS), cell(&t, r, col, C_OWNED));
        if (o.oom || blank.oom) break;
    }
    sg_table_free(&t);

    {
        size_t n = o.len;
        char *buf = blank.oom ? NULL : sg_buf_take(&o);
        if (!buf) {
            sg_buf_free(&o);
            sg_buf_free(&blank);
            sg_set_err(err, SG2GR_ERR_NOMEM, "out of memory", NULL, 0);
            return SG2GR_ERR_NOMEM;
        }
        *out = buf;
        if (out_len) *out_len = n;
    }
    if (stats) {
        st.blank_status_titles = blank.len ? sg_buf_take(&blank) : NULL;
        *stats = st;
    }
    sg_buf_free(&blank);
    return SG2GR_OK;
}

void sg2gr_stats_free(sg2gr_stats *stats) {
    if (!stats) return;
    free(stats->blank_status_titles);
    stats->blank_status_titles = NULL;
}

void *sg2gr_alloc(size_t n) { return malloc(n ? n : 1); }
void sg2gr_free(void *p) { free(p); }
const char *sg2gr_version(void) { return SG2GR_VERSION; }
