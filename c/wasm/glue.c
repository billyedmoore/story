/*
 * Flat, JS-friendly entry point: every argument is a number (pointer or
 * length) so it can be called from WebAssembly.instance.exports directly.
 */
#include "sg2gr.h"

#define SG2GR_WASM_STATS 4

/*
 * csv / ov_json are buffers the caller allocated with sg2gr_alloc (ov_len 0 =
 * no overrides). On success returns 0 and fills *out / *out_len (release with
 * sg2gr_free) and stats[] = {rows_in, rows_out, rows_excluded, rows_blank_status}.
 * *blank_titles gets the newline-separated titles of books with no read status
 * (or NULL); release it with sg2gr_free too.
 * On failure returns a non-zero sg2gr_status and a message in errbuf, which
 * must have SG2GR_ERRMSG_MAX bytes.
 */
SG2GR_API int sg2gr_wasm_convert(const char *csv, size_t csv_len, const char *ov_json,
                                 size_t ov_len, char **out, size_t *out_len,
                                 unsigned *stats, char **blank_titles, char *errbuf) {
    sg2gr_overrides *ov = NULL;
    sg2gr_stats st;
    sg2gr_error err;
    sg2gr_status rc;
    size_t i;

    err.status = SG2GR_OK;
    err.message[0] = '\0';
    if (ov_len) {
        rc = sg2gr_overrides_parse(ov_json, ov_len, &ov, &err);
        if (rc) goto done;
    }
    rc = sg2gr_convert(csv, csv_len, ov, out, out_len, &st, &err);
    if (!rc && stats) {
        stats[0] = (unsigned)st.rows_in;
        stats[1] = (unsigned)st.rows_out;
        stats[2] = (unsigned)st.rows_excluded;
        stats[3] = (unsigned)st.rows_blank_status;
    }
    if (!rc) {
        if (blank_titles) {
            *blank_titles = st.blank_status_titles;
            st.blank_status_titles = NULL;
        }
        sg2gr_stats_free(&st);
    }
done:
    sg2gr_overrides_free(ov);
    for (i = 0; errbuf && i < SG2GR_ERRMSG_MAX; i++) {
        errbuf[i] = err.message[i];
        if (!err.message[i]) break;
    }
    return (int)rc;
}
