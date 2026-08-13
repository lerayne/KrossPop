# KrossPop Card Database

Authoring source for the on-device card database. You edit one LibreOffice
workbook; a script compiles it into fixed-record binaries for the SD card.

```
krosspop/krosspop.fods       <- you edit this (LibreOffice, free)
krosspop/schema/tables.json  <- field layout, single source of truth
        |
        |  .venv/bin/python3 scripts/krosspop_build_db.py
        v
krosspop/build/*.data.bin    <- copy to SD card (gitignored, regenerable)
src/krosspop/*.generated.h   <- C++ structs matching the binaries (gitignored)
```

The script only ever **reads** the workbook, so your formatting, formulas and
dropdowns are never at risk.

## Format

Save as **ODF Spreadsheet (Flat XML)** — `.fods`, not `.ods`. It is a single
plain-XML file, so git can diff and delta-compress it. In LibreOffice:
*File → Save As → ODF Spreadsheet (Flat XML) (.fods)*.

## Sheets

Six sheets. Column *order* doesn't matter (the script matches on the header
text in row 1), but names must match `schema/tables.json`.

| Sheet | Columns |
| --- | --- |
| `agencies` | `agency_id`, `label`, `short_name`, `long_name` |
| `subsidiaries` | `subsidiary_id`, `label`, `short_name`, `long_name`, `agency_id`, `agency` |
| `groups` | `group_id`, `label`, `short_name`, `long_name`, `subsidiary_id`, `subsidiary` |
| `idols` | `idol_id`, `label`, `stage_name`, `real_name`, `group_id`, `group` |
| `releases` | `release_id`, `label`, `group_id`, `name`, `year`, `month`, `tracks_number` |
| `photocards` | `card_id`, `filename`, `idol_id`, `idol`, `rarity_tier`, `release_id`, `release` |

### The three column roles

- **`*_id` (own row)** — a plain integer you assign once when creating the
  row, then never change and never reuse, even after retiring the row. It is
  not the row position: sorting a sheet must not change anything. It is also
  the record's slot in the binary, so keep IDs reasonably dense (using ID 5000
  for your tenth idol makes a 5001-slot file).
- **Picker columns** (`idol`, `group`, `release`, …) — dropdowns, set up via
  *Data → Validity → Cell range* pointing at the target sheet's `label`
  column. These exist so you never type an ID by hand. They are **not**
  compiled into the binaries.
- **`*_id` (reference)** — a formula resolving the picker back to a number:

  ```
  =INDEX(<sheet>.$A$2:$A$999;MATCH(<picker cell>;<sheet>.$B$2:$B$999;0))
  ```

  This is the column the script actually reads. A picker value with no match
  yields `#N/A`, which the build rejects rather than writing bad data.

`label` is also a formula, e.g.
`=C2 & " (" & INDEX(groups.$C$2:$C$999;MATCH(E2;groups.$A$2:$A$999;0)) & ")"`.
Since `MATCH` takes the *first* hit, labels need to stay unique.

## What reaches the device

Only fields listed in `schema/tables.json`. `label`, `real_name`, `long_name`,
`tracks_number` and the picker columns stay in the workbook — the small
reference tables are meant to be RAM-resident on a ~380 KB device, so every
byte counts. Adding one later is a schema edit plus a rebuild; there is no
migration, since the binaries are regenerated wholesale.

`rarity_tier` is `1` = most common, higher = rarer (schema allows 1–9).

## Card images

`filename` holds the stem; the script appends `.bmp`. Card 42's file is
`000042_whatever.bmp` in the photocards folder on the SD card. Images are
never committed to git — they're large and copyrighted.

## Building

```bash
.venv/bin/python3 scripts/krosspop_build_db.py
```

Validation runs before anything is written: missing columns, non-numeric or
duplicate IDs, references that don't resolve, and out-of-range numbers are all
reported together, and nothing is written if any error is found. Name
truncation is a warning, not an error.

Pass `--header-only` to regenerate just the C++ header without a workbook.
