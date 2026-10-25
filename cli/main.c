#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sg2gr.h"

static char *read_all(FILE *f, size_t *len) {
    size_t cap = 1 << 16, n = 0, got;
    char *buf = malloc(cap);
    while (buf && (got = fread(buf + n, 1, cap - n, f)) > 0) {
        n += got;
        if (n == cap) {
            char *g = realloc(buf, cap *= 2);
            if (!g) {
                free(buf);
                return NULL;
            }
            buf = g;
        }
    }
    *len = n;
    return buf;
}

static char *read_path(const char *path, size_t *len) {
    FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    char *buf;
    if (!f) {
        fprintf(stderr, "sg2gr: cannot open %s\n", path);
        return NULL;
    }
    buf = read_all(f, len);
    if (f != stdin) fclose(f);
    if (!buf) fprintf(stderr, "sg2gr: out of memory reading %s\n", path);
    return buf;
}

static void usage(FILE *to) {
    fprintf(to,
            "usage: sg2gr INPUT [-o OUTPUT] [--overrides JSON]\n"
            "  INPUT        StoryGraph export CSV ('-' for stdin)\n"
            "  -o OUTPUT    Goodreads CSV to write (default: stdout)\n"
            "  --overrides  JSON of title prefix -> ISBN or {isbn, format, exclude}\n");
}

int main(int argc, char **argv) {
    const char *in_path = NULL, *out_path = NULL, *ov_path = NULL;
    char *in, *out = NULL, *ov_json;
    size_t in_len, out_len, ov_len;
    sg2gr_overrides *ov = NULL;
    sg2gr_stats st = {0};
    sg2gr_error err;
    int i, rc = 1;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout);
            return 0;
        } else if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) && i + 1 < argc) {
            out_path = argv[++i];
        } else if (!strcmp(argv[i], "--overrides") && i + 1 < argc) {
            ov_path = argv[++i];
        } else if (!in_path && (argv[i][0] != '-' || !argv[i][1])) {
            in_path = argv[i];
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (!in_path) {
        usage(stderr);
        return 2;
    }

    if (!(in = read_path(in_path, &in_len))) return 1;
    if (ov_path) {
        if (!(ov_json = read_path(ov_path, &ov_len))) {
            free(in);
            return 1;
        }
        if (sg2gr_overrides_parse(ov_json, ov_len, &ov, &err)) {
            fprintf(stderr, "sg2gr: %s: %s\n", ov_path, err.message);
            free(ov_json);
            free(in);
            return 1;
        }
        free(ov_json);
    }

    if (sg2gr_convert(in, in_len, ov, &out, &out_len, &st, &err)) {
        fprintf(stderr, "sg2gr: %s\n", err.message);
    } else {
        FILE *f = out_path ? fopen(out_path, "wb") : stdout;
        if (!f) {
            fprintf(stderr, "sg2gr: cannot write %s\n", out_path);
        } else {
            fwrite(out, 1, out_len, f);
            if (f != stdout) fclose(f);
            fprintf(stderr, "Converted %zu books", st.rows_out);
            if (st.rows_excluded) fprintf(stderr, ", %zu excluded", st.rows_excluded);
            if (st.rows_blank_status)
                fprintf(stderr, ", %zu with no read status (imported as to-read)", st.rows_blank_status);
            fputc('\n', stderr);
            if (st.blank_status_titles) {
                char *line = st.blank_status_titles;
                while (line) {
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = '\0';
                    fprintf(stderr, "  no read status: %s\n", line);
                    line = nl ? nl + 1 : NULL;
                }
            }
            rc = 0;
        }
    }
    sg2gr_stats_free(&st);
    sg2gr_free(out);
    sg2gr_overrides_free(ov);
    free(in);
    return rc;
}
