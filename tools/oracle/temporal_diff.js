/**
 * Compare this library's RFC 9557 reader against V8's Temporal.
 *
 * design.md section 12 names test262 and Temporal as the oracle for this
 * grammar. Temporal's string format *is* RFC 9557 - the `[America/New_York]`
 * suffix was standardised for Temporal - and V8's implementation is the one
 * test262 exercises, so asking V8 is the same authority without a corpus to
 * fetch. It needs `node --harmony-temporal`; Temporal is not on by default.
 *
 * `Temporal.ZonedDateTime.from(s, {offset: "reject", disambiguation:
 * "reject"})` is the closest thing to this library's strict defaults:
 * GCHRON_ZONECONFLICT_REJECT refuses an offset that contradicts the zone, and
 * a gap or an overlap that the offset does not settle is refused rather than
 * guessed.
 *
 * **The oracle is incomplete, and where it is, that is recorded rather than
 * worked around.** V8's Temporal in Node 22 rejects three things RFC 9557
 * allows, so every string in those classes is counted and reported instead of
 * being compared. A newer V8 that accepts them should make those counts fall
 * to zero, which is the point of counting rather than filtering.
 *
 * Run by `make check-oracle-temporal`; not part of the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */

import { spawnSync } from "node:child_process";
import process from "node:process";

if (typeof Temporal === "undefined") {
  console.error(
    "temporal_diff: this node has no Temporal. Run it as\n" +
    "  node --harmony-temporal tools/oracle/temporal_diff.js --driver <path>\n" +
    "The oracle is the authority here; without it the check is absent rather\n" +
    "than weaker, and saying so beats a green run.");
  process.exit(1);
}

let driver = null;
for (let i = 2; i < process.argv.length; ++i) {
  if (process.argv[i] === "--driver" && i + 1 < process.argv.length) {
    driver = process.argv[++i];
  }
}
if (driver === null) {
  console.error("usage: temporal_diff.js --driver <path to gchron_iso>");
  process.exit(2);
}

/*
 * The classes where V8 is known to be the one that is wrong, each with the
 * rule it breaks. A string that falls in one of these is counted, not
 * compared.
 */
const ORACLE_GAPS = [
  {
    name: "critical flag",
    rule: "RFC 9557 §4.1 allows `!` on any annotation; V8 rejects the string",
    matches: (s) => s.includes("[!"),
  },
  {
    name: "unknown annotation",
    rule: "RFC 9557 §4.1 says ignore an unrecognised annotation that is not " +
          "critical; V8 rejects the string",
    matches: (s) => /\[[a-z0-9-]+=/.test(s) && !/\[u-ca=/.test(s),
  },
  {
    name: "u-ca=julian",
    rule: "not a registered BCP 47 calendar, so V8 refuses it; this library " +
          "implements the Julian calendar and names it that way",
    matches: (s) => s.includes("u-ca=julian"),
  },
];

/*
 * Where Temporal is looser than the RFC it profiles. Not a V8 defect and not
 * this library's decision either - a rule Temporal adds on top.
 */
const TEMPORAL_EXTRA = [
  {
    name: "zone identifier case",
    rule: "Temporal matches IANA identifiers case-insensitively, so it " +
          "reads `[europe/paris]`. RFC 9557 §3.1: \"Keys are lowercase " +
          "only. Values are case-sensitive unless otherwise specified\" - " +
          "and the name is a value, so this library resolves it exactly. " +
          "`whenever` refuses it too",
    matches: (s) => {
      const m = s.match(/\[([A-Za-z_]+\/[A-Za-z_]+)\]/);
      return m !== null && m[1] !== canonical(m[1]);
    },
  },
];

/*
 * Differences that are this library's own decision rather than V8's gap.
 */
const DESIGNED = [
  {
    name: "no zone annotation",
    rule: "Temporal.ZonedDateTime.from requires one; this library reads the " +
          "string as RFC 3339 and gives it a fixed-offset zone",
    matches: (s) => !/\[[^=\]]+\]/.test(s) || /^\S+\[[+-]/.test(s),
  },
];

const ZONES = ["Europe/Paris", "America/New_York", "Asia/Kolkata",
  "Australia/Lord_Howe", "Pacific/Chatham", "Etc/GMT", "Etc/GMT+5"];

function canonical(name) {
  // Only used to spot a deliberately case-mangled name in the corpus.
  return ZONES.find((z) => z.toLowerCase() === name.toLowerCase()) || name;
}

/** Every string the differential puts through both sides. */
function corpus() {
  const out = [];
  const civils = [
    "2026-09-20T15:30:45", "2026-01-15T00:00:00", "2026-06-30T23:59:59",
    "2026-03-08T02:30:00", "2026-11-01T01:30:00", "2026-03-29T02:30:00",
    "2026-10-25T02:30:00", "2026-04-05T02:30:00", "1970-01-01T00:00:00",
    "2000-02-29T12:00:00", "2038-01-19T03:14:07", "1900-01-01T12:00:00",
  ];
  const fractions = ["", ".5", ".123", ".123456", ".123456789"];
  const offsets = ["Z", "+00:00", "+02:00", "-05:00", "+05:30", "+10:30",
    "-00:00", "+13:45"];

  for (const civil of civils) {
    for (const fraction of fractions) {
      for (const offset of offsets) {
        const stem = civil + fraction + offset;
        out.push(stem);
        for (const zone of ZONES) {
          out.push(`${stem}[${zone}]`);
        }
        out.push(`${stem}[Europe/Paris][u-ca=iso8601]`);
        out.push(`${stem}[Europe/Paris][u-ca=gregory]`);
        out.push(`${stem}[Europe/Paris][u-ca=julian]`);
        out.push(`${stem}[Europe/Paris][x-vendor=1]`);
        out.push(`${stem}[!Europe/Paris]`);
        out.push(`${stem}[europe/paris]`);
        out.push(`${stem}[+02:00]`);
      }
    }
  }

  // Shapes that should be refused, and a few that should not.
  for (const bad of [
    "2026-09-20T15:30:45", "2026-09-20", "15:30:45Z", "2026-09-20T15:30Z",
    "2026-13-01T00:00:00Z", "2026-02-30T00:00:00Z", "2026-09-20T24:00:00Z",
    "2026-09-20T15:30:60Z", "2026-09-20T15:30:45+24:00",
    "2026-09-20 15:30:45Z", "20260920T153045Z", "2026-09-20T15:30:45z",
    "2026-09-20t15:30:45Z", "+002026-09-20T15:30:45Z", "",
    "2026-09-20T15:30:45Z[]", "2026-09-20T15:30:45Z[Not/AZone]",
    "2026-09-20T15:30:45Z[Europe/Paris", "2026-09-20T15:30:45Z]",
    "2026-09-20T15:30:45Z[Europe/Paris]trailing",
    "2026-09-20T15:30:45.Z", "2026-09-20T15:30:45.1234567890Z",
  ]) {
    out.push(bad);
  }
  return out;
}

/** What Temporal makes of one string: an epoch-nanosecond string, or null. */
function ask_temporal(text) {
  try {
    const zdt = Temporal.ZonedDateTime.from(text,
        { offset: "reject", disambiguation: "reject" });
    return zdt.epochNanoseconds.toString();
  }
  catch {
    return null;
  }
}

const strings = corpus();
const input = strings.map((s) => Buffer.from(s, "utf8").toString("hex"))
    .join("\n") + "\n";
const run = spawnSync(driver, [], { input, maxBuffer: 1 << 28 });
if (run.status !== 0) {
  console.error(`temporal_diff: the driver exited ${run.status}`);
  if (run.stderr) process.stderr.write(run.stderr);
  process.exit(1);
}
const verdicts = run.stdout.toString().split("\n");

let compared = 0;
let agreed = 0;
const disagreements = [];
const skipped = new Map();

for (let i = 0; i < strings.length; ++i) {
  const text = strings[i];
  const line = verdicts[i] ?? "";

  const gap = ORACLE_GAPS.find((g) => g.matches(text))
      ?? DESIGNED.find((d) => d.matches(text))
      ?? TEMPORAL_EXTRA.find((t) => t.matches(text));
  if (gap !== undefined) {
    skipped.set(gap.name, (skipped.get(gap.name) ?? 0) + 1);
    continue;
  }

  const theirs = ask_temporal(text);
  const ours = line.startsWith("ok ") ? line.split(" ") : null;
  compared += 1;

  if (theirs === null && ours === null) {
    agreed += 1;
    continue;
  }
  if (theirs === null || ours === null) {
    disagreements.push(`${JSON.stringify(text)}: temporal ` +
        `${theirs === null ? "refused" : "read " + theirs}, chron ` +
        `${ours === null ? "refused (" + line + ")" : "read it"}`);
    continue;
  }
  const nanos = BigInt(ours[1]) * 1000000000n + BigInt(ours[2]);
  if (nanos.toString() !== theirs) {
    disagreements.push(`${JSON.stringify(text)}: temporal ${theirs}, ` +
        `chron ${nanos}`);
    continue;
  }
  agreed += 1;
}

function report(heading, entries) {
  console.log(heading);
  for (const entry of entries) {
    const n = skipped.get(entry.name) ?? 0;
    console.log(`  ${String(n).padStart(6)}  ${entry.name}`);
    console.log(`          ${entry.rule}`);
  }
}
report("The oracle is wrong here, and a newer V8 should drop these to zero:",
    ORACLE_GAPS);
report("This library's own decision:", DESIGNED);
report("Temporal is looser than RFC 9557 here:", TEMPORAL_EXTRA);
console.log(`temporal: ${compared} strings compared, ${agreed} agreed, ` +
    `${disagreements.length} differed`);
for (const line of disagreements.slice(0, 40)) {
  console.log(`  DIFFER ${line}`);
}
if (disagreements.length > 40) {
  console.log(`  ... and ${disagreements.length - 40} more`);
}
process.exit(disagreements.length === 0 ? 0 : 2);
