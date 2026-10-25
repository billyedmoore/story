#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(got, want) do { checks++; const char *g_ = (got), *w_ = (want); \
    if (strcmp(g_, w_)) { failures++; \
    fprintf(stderr, "FAIL %s:%d:\n   got  [%s]\n   want [%s]\n", __FILE__, __LINE__, g_, w_); } } while (0)

static const char *HDR =
    "Title,Authors,Contributors,ISBN/UID,Format,Read Status,Date Added,Last Date Read,"
    "Read Count,Star Rating,Review,Tags,Owned?\n";

/* Convert HDR + one row and return the Goodreads field `col` of the first book. */
static char *g_out;

static const char *convert_field(const char *row, const char *colname, const sg2gr_overrides *ov,
                                 sg2gr_stats *stats) {
    sg_buf in;
    sg_table t;
    size_t line, c, want = (size_t)-1;
    static char val[512];
    sg2gr_error err;

    free(g_out);
    g_out = NULL;
    sg_buf_init(&in);
    sg_buf_puts(&in, HDR);
    sg_buf_puts(&in, row);
    CHECK(sg2gr_convert(in.p, in.len, ov, &g_out, NULL, stats, &err) == SG2GR_OK);
    sg_buf_free(&in);
    if (!g_out) return "<convert failed>";
    CHECK(sg_csv_parse(g_out, strlen(g_out), &t, &line) == SG_CSV_OK);
    for (c = 0; c < t.ncols; c++)
        if (!strcmp(t.cells[c], colname)) want = c;
    if (want == (size_t)-1 || t.nrows < 2) {
        sg_table_free(&t);
        return "<no such column/row>";
    }
    strncpy(val, t.cells[t.ncols + want], sizeof val - 1);
    sg_table_free(&t);
    return val;
}

static void test_csv(void) {
    sg_table t;
    size_t line = 0;
    const char *a = "\xEF\xBB\xBF" "a,b,c\r\n1,\"x,\"\"y\"\"\nz\",3\r\n\r\n4,5\n6,7,8,9\n";
    CHECK(sg_csv_parse(a, strlen(a), &t, &line) == SG_CSV_OK);
    CHECK(t.nrows == 4 && t.ncols == 3);
    CHECK_STR(t.cells[0], "a");           /* BOM skipped */
    CHECK_STR(t.cells[4], "x,\"y\"\nz");  /* quotes + embedded newline */
    CHECK_STR(t.cells[8], "");            /* short row padded */
    CHECK_STR(t.cells[11], "8");          /* long row truncated */
    sg_table_free(&t);

    CHECK(sg_csv_parse("a,b\n\"oops", 9, &t, &line) == SG_CSV_UNTERMINATED);
    CHECK(sg_csv_parse("", 0, &t, &line) == SG_CSV_OK && t.nrows == 0);
    sg_table_free(&t);
    CHECK(sg_csv_parse("a,b\n1,", 6, &t, &line) == SG_CSV_OK && t.nrows == 2);
    CHECK_STR(t.cells[3], "");            /* trailing comma at EOF */
    sg_table_free(&t);
}

static void test_overrides(void) {
    sg2gr_overrides *ov = NULL;
    sg2gr_error err;
    const char *good = "{\"A\":\"123\",\"B\":{\"isbn\":\"9\",\"format\":\"paperback\",\"x\":[1,{\"y\":null}]},"
                       "\"C\":{\"exclude\":true},\"\\u00e9\\ud83d\\ude00\":\"1\"}";
    CHECK(sg2gr_overrides_parse(good, strlen(good), &ov, &err) == SG2GR_OK);
    CHECK(ov && ov->n == 4);
    CHECK_STR(ov->e[0].isbn, "123");
    CHECK_STR(ov->e[1].format, "paperback");
    CHECK(ov->e[2].exclude == 1);
    CHECK_STR(ov->e[3].prefix, "\xC3\xA9\xF0\x9F\x98\x80");
    sg2gr_overrides_free(ov);

    CHECK(sg2gr_overrides_parse("{}", 2, &ov, &err) == SG2GR_OK && ov->n == 0);
    sg2gr_overrides_free(ov);
    {
        const char *bad[] = {"", "[]", "{\"a\":1}", "{\"a\":\"1\",}", "{\"a\":\"1\"} x",
                             "{\"\":\"1\"}", "{\"a\":{\"exclude\":\"yes\"}}", "{\"a\":\"\\q\"}",
                             "{\"a\":{\"isbn\":5}}", "{\"a\":\"unterminated"};
        size_t i;
        for (i = 0; i < sizeof bad / sizeof *bad; i++) {
            ov = (sg2gr_overrides *)1;
            CHECK(sg2gr_overrides_parse(bad[i], strlen(bad[i]), &ov, &err) == SG2GR_ERR_JSON);
            CHECK(ov == NULL && err.message[0]);
        }
    }
    CHECK(sg2gr_overrides_parse("{\"a\":\"1\"", 8, &ov, &err) == SG2GR_ERR_JSON);
    CHECK_STR(err.message, "expected ',' or '}' (byte 8)");
}

static void test_mapping(void) {
    sg2gr_stats st;
    sg2gr_overrides *ov;

    CHECK_STR(convert_field("T,Jane Q. Doe,,,,read,,,,,,,\n", "Author l-f", NULL, NULL), "Doe, Jane Q.");
    CHECK_STR(convert_field("T,\"A One, B Two\",\"C Three (Translator), D Four (Illustrator)\",,,read,,,,,,,\n",
                            "Additional Authors", NULL, NULL), "B Two, C Three, D Four");
    CHECK_STR(convert_field("T,A,,,,read,,,,,,,\n", "Additional Authors", NULL, NULL), "");

    /* ISBNs: 13, 10 (with X), hyphens, junk/ASIN */
    CHECK_STR(convert_field("T,A,,978-1-63557-563-7,,read,,,,,,,\n", "ISBN13", NULL, NULL), "=\"9781635575637\"");
    CHECK_STR(convert_field("T,A,,978-1-63557-563-7,,read,,,,,,,\n", "ISBN", NULL, NULL), "=\"\"");
    CHECK_STR(convert_field("T,A,,043942089X,,read,,,,,,,\n", "ISBN", NULL, NULL), "=\"043942089X\"");
    CHECK_STR(convert_field("T,A,,B0036NDRG2,,read,,,,,,,\n", "ISBN", NULL, NULL), "=\"\"");
    CHECK_STR(convert_field("T,A,,B0036NDRG2,,read,,,,,,,\n", "ISBN13", NULL, NULL), "=\"\"");

    /* ratings: whole stars, halves up */
    CHECK_STR(convert_field("T,A,,,,read,,,,4.0,,,\n", "My Rating", NULL, NULL), "4");
    CHECK_STR(convert_field("T,A,,,,read,,,,3.5,,,\n", "My Rating", NULL, NULL), "4");
    CHECK_STR(convert_field("T,A,,,,read,,,,4.5,,,\n", "My Rating", NULL, NULL), "5");
    CHECK_STR(convert_field("T,A,,,,read,,,,2.5,,,\n", "My Rating", NULL, NULL), "3");
    CHECK_STR(convert_field("T,A,,,,read,,,,4.25,,,\n", "My Rating", NULL, NULL), "4");
    CHECK_STR(convert_field("T,A,,,,read,,,,4.75,,,\n", "My Rating", NULL, NULL), "5");
    CHECK_STR(convert_field("T,A,,,,read,,,,,,,\n", "My Rating", NULL, NULL), "0");

    /* formats */
    CHECK_STR(convert_field("T,A,,,digital,read,,,,,,,\n", "Binding", NULL, NULL), "Kindle Edition");
    CHECK_STR(convert_field("T,A,,,AUDIO,read,,,,,,,\n", "Binding", NULL, NULL), "Audiobook");
    CHECK_STR(convert_field("T,A,,,vinyl,read,,,,,,,\n", "Binding", NULL, NULL), "");

    /* statuses, shelves and dates */
    CHECK_STR(convert_field("T,A,,,,read,2020/01/01,2020/02/02,1,,,x,\n", "Date Read", NULL, NULL), "2020/02/02");
    CHECK_STR(convert_field("T,A,,,,to-read,2020/01/01,2020/02/02,0,,,,\n", "Date Read", NULL, NULL), "");
    CHECK_STR(convert_field("T,A,,,,paused,,,,,,,\n", "Exclusive Shelf", NULL, NULL), "currently-reading");
    CHECK_STR(convert_field("T,A,,,,paused,,,,,,t1,\n", "Bookshelves", NULL, NULL), "paused, t1");
    CHECK_STR(convert_field("T,A,,,,did-not-finish,,2020/02/02,,,,,\n", "Exclusive Shelf", NULL, NULL), "read");
    CHECK_STR(convert_field("T,A,,,,did-not-finish,,2020/02/02,,,,,\n", "Date Read", NULL, NULL), "");
    CHECK_STR(convert_field("T,A,,,,did-not-finish,,,,,,\"a, b\",\n", "Bookshelves", NULL, NULL), "did-not-finish, a, b");
    CHECK_STR(convert_field("T,A,,,,nonsense,,,,,,,\n", "Exclusive Shelf", NULL, NULL), "to-read");
    CHECK_STR(convert_field("T,A,,,,read,,,,,,,Yes\n", "Owned Copies", NULL, NULL), "1");
    CHECK_STR(convert_field("T,A,,,,read,,,,,,,\n", "Read Count", NULL, NULL), "0");

    /* reviews: html -> text */
    CHECK_STR(convert_field("T,A,,,,read,,,,,\"<div>One &amp; two</div><div><br></div><div>&lt;3 &#233; &#x1F600;</div>\",,\n",
                            "My Review", NULL, NULL), "One & two\n\n<3 \xC3\xA9 \xF0\x9F\x98\x80");
    CHECK_STR(convert_field("T,A,,,,read,,,,,\"a < b & c\",,\n", "My Review", NULL, NULL), "a < b & c");
    CHECK_STR(convert_field("T,A,,,,read,,,,,\"say \"\"hi\"\", ok\",,\n", "My Review", NULL, NULL), "say \"hi\", ok");

    /* blank-status stats */
    convert_field("T,A,,,,,,,,,,,\n", "Title", NULL, &st);
    CHECK(st.rows_blank_status == 1 && st.rows_out == 1);
    CHECK(st.blank_status_titles && !strcmp(st.blank_status_titles, "T"));
    sg2gr_stats_free(&st);
    convert_field("One,A,,,,,,,,,,,\nRead,B,,,,read,,,,,,,\n\"Tw\no, x\",C,,,,,,,,,,,\n", "Title", NULL, &st);
    CHECK(st.rows_blank_status == 2);
    CHECK(st.blank_status_titles && !strcmp(st.blank_status_titles, "One\nTw o, x"));
    sg2gr_stats_free(&st);
    convert_field("T,A,,,,read,,,,,,,\n", "Title", NULL, &st);
    CHECK(st.blank_status_titles == NULL);

    /* overrides */
    {
        const char *j = "{\"Tit\":{\"isbn\":\"9780306406157\",\"format\":\"paperback\"}}";
        CHECK(sg2gr_overrides_parse(j, strlen(j), &ov, NULL) == SG2GR_OK);
    }
    CHECK_STR(convert_field("Title,A,,,digital,read,,,,,,,\n", "ISBN13", ov, NULL), "=\"9780306406157\"");
    CHECK_STR(convert_field("Title,A,,,digital,read,,,,,,,\n", "Binding", ov, NULL), "Paperback");
    CHECK_STR(convert_field("Other,A,,,digital,read,,,,,,,\n", "Binding", ov, NULL), "Kindle Edition");
    sg2gr_overrides_free(ov);
    {
        const char *j = "{\"Tit\":{\"exclude\":true}}";
        CHECK(sg2gr_overrides_parse(j, strlen(j), &ov, NULL) == SG2GR_OK);
    }
    convert_field("Title,A,,,,read,,,,,,,\nKeep,B,,,,read,,,,,,,\n", "Title", ov, &st);
    CHECK(st.rows_excluded == 1 && st.rows_out == 1 && st.rows_in == 2);
    sg2gr_overrides_free(ov);
    {   /* a prefix longer than the title must not match (or read past it) */
        const char *j = "{\"Title and more\":{\"exclude\":true}}";
        CHECK(sg2gr_overrides_parse(j, strlen(j), &ov, NULL) == SG2GR_OK);
        convert_field("Title,A,,,,read,,,,,,,\n", "Title", ov, &st);
        CHECK(st.rows_excluded == 0 && st.rows_out == 1);
        sg2gr_overrides_free(ov);
    }
}

static void test_errors(void) {
    char *out = NULL;
    sg2gr_error err;
    const char *wrong = "a,b\n1,2\n";
    CHECK(sg2gr_convert(wrong, strlen(wrong), NULL, &out, NULL, NULL, &err) == SG2GR_ERR_NOT_STORYGRAPH);
    CHECK(out == NULL && err.message[0]);
    CHECK(sg2gr_convert("a,\"b", 4, NULL, &out, NULL, NULL, &err) == SG2GR_ERR_CSV);
    CHECK_STR(err.message, "unterminated quoted field (line 1)");
    CHECK(sg2gr_convert(NULL, 0, NULL, &out, NULL, NULL, &err) == SG2GR_ERR_ARGS);
    CHECK(sg2gr_convert("", 0, NULL, &out, NULL, NULL, NULL) == SG2GR_ERR_NOT_STORYGRAPH);
    {   /* header only: valid, zero books */
        size_t n;
        CHECK(sg2gr_convert(HDR, strlen(HDR), NULL, &out, &n, NULL, NULL) == SG2GR_OK);
        CHECK(n > 0 && out[n - 1] == '\n' && strchr(out, '\n') == out + n - 1);
        sg2gr_free(out);
    }
}

int main(void) {
    test_csv();
    test_overrides();
    test_mapping();
    test_errors();
    free(g_out);
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
