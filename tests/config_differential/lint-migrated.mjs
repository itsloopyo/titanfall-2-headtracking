// Runs core's canonical config lint over CameraUnlock.ini, the committed file, and over every
// distinct file the differential test migrated (the folder it names as the argument).
//
// A migrated file holds a value on each row where the player's value is not what Defaults.ini
// gives, which the lint reports because a committed file holds `default` there: that rule is
// for the committed file, so it is the one problem a migrated file may have.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintCanonicalConfig } from "../../cameraunlock-core/scripts/check-canonical-config.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const migratedDir = process.argv[2];
if (!migratedDir) throw new Error("usage: node lint-migrated.mjs <folder of migrated files>");

const options = { dialect: "native", perGame: [] };
const VALUED = /per_game (does not list it|lists none of them) for this repo/;
const failures = [];

for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(repo, "CameraUnlock.ini")), options)) {
  failures.push(`CameraUnlock.ini: ${problem}`);
}

const files = fs.readdirSync(migratedDir).filter((f) => f.endsWith(".ini"));
if (files.length === 0) throw new Error(`${migratedDir} holds no migrated files`);
for (const file of files) {
  for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(migratedDir, file)), options)) {
    if (!VALUED.test(problem)) failures.push(`${file}: ${problem}`);
  }
}

if (failures.length > 0) {
  for (const f of failures) console.log(`FAIL ${f}`);
  process.exit(1);
}
console.log(`canonical config lint: the committed file and ${files.length} migrated files pass`);
