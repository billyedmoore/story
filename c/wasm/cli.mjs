// Usage: node wasm/cli.mjs build/sg2gr.wasm INPUT.csv [OVERRIDES.json] > out.csv
import { readFileSync } from "node:fs";
import { loadSg2gr } from "./sg2gr.mjs";

const [wasmPath, csvPath, ovPath] = process.argv.slice(2);
const { convert } = await loadSg2gr(readFileSync(wasmPath));
const r = convert(readFileSync(csvPath, "utf8"), ovPath ? readFileSync(ovPath, "utf8") : "");
if (r.error) {
  console.error(`sg2gr: ${r.error}`);
  process.exit(1);
}
process.stdout.write(r.text);
console.error(`Converted ${r.stats.rowsOut} books (${r.stats.excluded} excluded, ${r.stats.blankStatus} blank status)`);
