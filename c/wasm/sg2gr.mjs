// JS wrapper around build/sg2gr.wasm.
//   const { convert } = await loadSg2gr(bytesOrResponseBuffer);
//   const r = convert(csvText, overridesJsonText);   // { text, stats: {…, blankTitles: string[]} } | { error, status }
const enc = new TextEncoder(), dec = new TextDecoder();
const ERRMSG_MAX = 128;

export async function loadSg2gr(wasmBytes) {
  const { instance } = await WebAssembly.instantiate(wasmBytes, {});
  const x = instance.exports;

  // Linear memory can move when it grows, so take a fresh view after every call.
  const bytes = () => new Uint8Array(x.memory.buffer);
  const words = () => new Uint32Array(x.memory.buffer);
  const put = (data) => {
    const p = x.sg2gr_alloc(data.length);
    bytes().set(data, p);
    return p;
  };

  function convert(csvText, overridesText = "") {
    const csv = enc.encode(csvText), ov = enc.encode(overridesText);
    const csvPtr = put(csv), ovPtr = ov.length ? put(ov) : 0;
    const slots = x.sg2gr_alloc(4 * 7); // out ptr, out len, 4 stats, blank-titles ptr
    const err = x.sg2gr_alloc(ERRMSG_MAX);
    const rc = x.sg2gr_wasm_convert(csvPtr, csv.length, ovPtr, ov.length,
                                    slots, slots + 4, slots + 8, slots + 24, err);
    let result;
    if (rc === 0) {
      const [outPtr, outLen, rowsIn, rowsOut, excluded, blankStatus, blankPtr] =
        words().subarray(slots / 4, slots / 4 + 7);
      let blankTitles = [];
      if (blankPtr) {
        const b = bytes();
        blankTitles = dec.decode(b.subarray(blankPtr, b.indexOf(0, blankPtr))).split("\n");
        x.sg2gr_free(blankPtr);
      }
      result = {
        text: dec.decode(bytes().subarray(outPtr, outPtr + outLen)),
        stats: { rowsIn, rowsOut, excluded, blankStatus, blankTitles },
      };
      x.sg2gr_free(outPtr);
    } else {
      const m = bytes().subarray(err, err + ERRMSG_MAX);
      result = { error: dec.decode(m.subarray(0, m.indexOf(0))), status: rc };
    }
    for (const p of [csvPtr, ovPtr, slots, err]) if (p) x.sg2gr_free(p);
    return result;
  }

  // Parse-only check of an overrides document: null if valid, else the error message.
  function validateOverrides(text) {
    const json = enc.encode(text);
    if (!json.length) return null;
    const ptr = put(json);
    const out = x.sg2gr_alloc(4);
    const err = x.sg2gr_alloc(4 + ERRMSG_MAX); // sg2gr_error: int status, char message[]
    const rc = x.sg2gr_overrides_parse(ptr, json.length, out, err);
    let message = null;
    if (rc === 0) {
      x.sg2gr_overrides_free(words()[out / 4]);
    } else {
      const m = bytes().subarray(err + 4, err + 4 + ERRMSG_MAX);
      message = dec.decode(m.subarray(0, m.indexOf(0)));
    }
    for (const p of [ptr, out, err]) x.sg2gr_free(p);
    return message;
  }

  return { convert, validateOverrides, memoryBytes: () => x.memory.buffer.byteLength };
}
