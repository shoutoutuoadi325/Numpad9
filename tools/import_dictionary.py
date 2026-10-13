#!/usr/bin/env python3
"""Deterministically derive the packaged offline lexicon; Python standard library only.

No downloading is performed. Source snapshots and every checksum are tracked in
data/SOURCES.json. See docs/dictionary.md for the licensed source locations.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parents[1]
CEDICT_ENTRY = re.compile(r"^\S+ (\S+) \[([^\]]+)\] /")


def import_cedict(path: Path) -> dict[tuple[str, str], int]:
    entries: dict[tuple[str, str], int] = {}
    with gzip.open(path, "rt", encoding="utf-8") as source:
        for line in source:
            if line.startswith("#"):
                continue
            match = CEDICT_ENTRY.match(line)
            if not match:
                continue
            text, tone_spelling = match.groups()
            # Ignore dictionary conventions such as Latin abbreviations,
            # numerical names and unspecified pronunciations. Do not invent a
            # pronunciation for an entry whose spelling is not plain Pinyin.
            spelling = tone_spelling.lower().replace("u:", "v").replace("ü", "v")
            spelling = re.sub(r"[1-5\s'·-]", "", spelling)
            if not re.fullmatch(r"[a-z]{1,64}", spelling) or len(text) > 64:
                continue
            if not text or all(ord(c) < 128 for c in text):
                continue
            if any(ord(c) < 32 for c in text):
                continue
            # The source has no frequency data. These are heuristic scores,
            # never represented as corpus frequencies. Common overrides below
            # outrank this conservative default.
            entries[(spelling, text)] = max(10, 500 - 20 * len(text))
    return entries


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "data/lexicon.tsv")
    args = parser.parse_args()
    provenance = json.loads((ROOT / "data/SOURCES.json").read_text(encoding="utf-8"))
    for source in provenance["snapshots"]:
        path = ROOT / source["path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != source["sha256"]:
            raise SystemExit(f"Checksum mismatch: {path}")
    entries = import_cedict(ROOT / "data/sources/cedict.txt.gz")
    chinese_count = len(entries)
    # Select English/American word categories <= 50. Do not extract archive
    # paths onto disk; process only matching regular-file members in memory.
    with tarfile.open(ROOT / "data/sources/scowl-2020.12.07.tar.gz", "r:gz") as archive:
        for member in archive.getmembers():
            match = re.search(r"/final/(?:english|american)-words\.(\d+)$", member.name)
            if not member.isfile() or not match or int(match[1]) > 50:
                continue
            source = archive.extractfile(member)
            assert source is not None
            # SCOWL v1 uses Latin-1, not UTF-8. Non-ASCII entries are omitted.
            for word in source.read().decode("latin-1").splitlines():
                if re.fullmatch(r"[a-z]{1,64}", word):
                    entries[(word, word)] = max(entries.get((word, word), 0), 1000 - int(match[1]) * 10)
    for line in (ROOT / "data/common.tsv").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        spelling, text, rank = line.split("\t")
        assert re.fullmatch(r"[a-z]{1,64}", spelling), spelling
        assert 0 < len(text) <= 64 and "\t" not in text
        entries[(spelling, text)] = int(rank)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        output.write("# Numpad9 offline lexicon. Attribution and licenses: data/NOTICE.txt\n")
        output.write("# Chinese adaptations: CC BY-SA 4.0. English source: SCOWL permissive notices.\n")
        for (spelling, text), rank in sorted(entries.items()):
            output.write(f"{spelling}\t{text}\t{rank}\n")
    print(f"Wrote {len(entries):,} entries ({chinese_count:,} imported Chinese readings).")


if __name__ == "__main__":
    main()
