# sg2gr

Convert a StoryGraph library export (CSV) into Goodreads CSV format. Pure C99, no I/O in the
library, so the same code builds as a native CLI and as a ~24 KB WebAssembly module.

## Command line

    make                      # -> build/sg2gr
    build/sg2gr INPUT [-o OUTPUT] [--overrides JSON]

`INPUT` and `--overrides` accept `-` for stdin; output goes to stdout unless `-o` is given.
Progress and warnings go to stderr. Exit status: 0 ok, 1 error, 2 bad usage.

    build/sg2gr export.csv --overrides overrides.json -o goodreads.csv
    cat export.csv | build/sg2gr - > goodreads.csv

## Overrides

A JSON object keyed by title prefix. A value is an ISBN, or an object with any of
`isbn`, `format` (hardcover | paperback | digital | audio), `exclude`:

    {
      "The Example Book": "9780306406157",
      "Another Title": { "isbn": "9780306406157", "format": "paperback" },
      "A Book To Leave Out": { "exclude": true }
    }

## Library

    #include "sg2gr.h"          /* see the header for the full contract */
    sg2gr_overrides_parse(json, len, &ov, &err);
    sg2gr_convert(csv, len, ov, &out, &out_len, &stats, &err);
    sg2gr_free(out);

## WebAssembly

    make wasm                   # -> build/sg2gr.wasm (needs clang with wasm32 + wasm-ld)
    node wasm/cli.mjs build/sg2gr.wasm export.csv overrides.json > goodreads.csv

`wasm/sg2gr.mjs` is the browser/node wrapper: `const {convert} = await loadSg2gr(bytes)`.
The module has no imports; `wasm/shim.c` provides malloc and the mem*/strlen routines.

## Tests

    make test                   # unit tests under ASan + UBSan
