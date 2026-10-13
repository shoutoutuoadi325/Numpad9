# Offline dictionary

`data/lexicon.tsv` contains 183,235 reading/output pairs: 121,736 imported
Chinese readings, original common phrases and approximately 61,000 English
words. A reading is not a unique Chinese word: polyphonic words can have more
than one reading. The runtime removes duplicate output text from each candidate
list. Dictionary files are bundled with the app; users do not need a network
connection or a language-model service.

Chinese data comes from [CC-CEDICT, published by MDBG](https://www.mdbg.net/chinese/dictionary?page=cc-cedict),
release `2026-10-12T08:13:53Z`, retrieved October 13, 2026. CC-CEDICT is licensed
under [CC BY-SA 4.0](https://cc-cedict.org/editor/editor.php?handler=Download).
English data comes from [SCOWL](https://wordlist.aspell.net/), pinned release
`2020.12.07`, whose full permissive copyright/source notices are retained in
`data/licenses/SCOWL-COPYRIGHT.txt`. Source snapshots and SHA-256 values are in
`data/SOURCES.json`. The app installer must include `data/NOTICE.txt` and both
files in `data/licenses/`. The application code and dictionary are independently
licensed; keep the dictionary available as readable, modifiable TSV.

The CC-CEDICT transformation selects simplified outputs, removes definitions
and traditional outputs, drops Pinyin tone numbers, lowercases pronunciations,
removes separators, converts `u:`/`ü` to `v`, excludes unsupported mixed numeric
or Latin pronunciation notations, and deduplicates spelling/output pairs.
SCOWL selects the English/American word categories in size bands up to 50,
keeps only lowercase ASCII words and deduplicates them. Original preference
overrides and phrases are in `data/common.tsv`. These changes are explicitly
described in `data/NOTICE.txt`; Chinese adaptations and original additions are
released under CC BY-SA 4.0.

Rebuild the exact packaged dictionary using Python 3.9 or later:

```sh
python3 tools/import_dictionary.py
```

The importer verifies the retained source archives' checksums and performs no
network requests. Its default output is `data/lexicon.tsv`; `--output` selects
another path. Regeneration sorts by normalized spelling and output text, uses
LF newlines and contains no wall-clock timestamp, making its bytes deterministic.
Upstream download URLs are recorded for attribution and future maintainers;
ordinary builds do not download dictionaries. Source updates require reviewing
the license, recording new checksums/dates and regenerating the TSV.

Each TSV record has three columns:

```text
nihao<TAB>你好<TAB>29000
hello<TAB>hello<TAB>29570
```

The integer is a heuristic preference score, not a measured corpus frequency.
Exact digit encodings precede prefix completions; higher rank then spelling and
output text break ties. The source has no Chinese frequency corpus. Original
common phrases receive preference overrides; uncommon entries receive a small
score based on output length. This gives useful deterministic common candidates
without storing users' typing history. It does not offer learned personal ranks,
cloud prediction or arbitrary whole-sentence language-model completion. Users
can compose sentences by confirming successive words or phrases.

The physical keypad is read by position: `7 8 9` means phone `1 2 3`, the middle
row remains `4 5 6`, and `1 2 3` means phone `7 8 9`. Pinyin letters use the phone
ABC/DEF layout, with `v` for `ü`. Only logical digits `2` through `9` extend an
engine composition. Punctuation and committing the selected phrase belong to
the native Windows input integration. Dedicated navigation keys are excluded
by marking extended scan codes with `0x100`; NumLock-off keypad navigation scan
codes still map by keypad position.

The C++17 engine accepts at most 64 digits, returns at most 100 candidates,
loads at most 500,000 records and rejects files larger than 64 MiB. It validates
UTF-8, Unicode scalar values, TSV shape, rank overflow and record lengths.
Failed loading preserves the previous dictionary and composition. Candidate
lookup binary-searches the digit prefix range and retains only the requested
best candidates. The runtime never writes a dictionary, composition or history
to disk and performs no network access.

Run the portable tests directly or through the project's CMake test target:

```sh
clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc \
  src/core/engine.cpp tests/core_tests.cpp -o /tmp/numpad9-core-tests
/tmp/numpad9-core-tests data/lexicon.tsv
```

Tests cover phone orientation, NumLock-off versus extended navigation keys,
Pinyin and English ranking, homophone collisions, prefix completion, duplicate
readings, mode changes, input limits, corrupt/oversized dictionaries, atomic
replacement, supplementary-plane Unicode and actual packaged common phrases.
They also print dictionary loading time and broad-prefix query latency; timing
is observational, so slow CI hosts do not produce flaky test failures.
