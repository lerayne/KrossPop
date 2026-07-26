# KrossPop Roadmap

Living plan for turning this fork into KrossPop, a K-pop fan companion app built on
the CrossInk/CrossPoint e-reader firmware. This is a planning document, not a
contract — update it as ideas change. See [`SCOPE.md`](SCOPE.md) for the
in-scope/out-of-scope rules this roadmap must stay inside (or explicitly propose
changing).

## Vision

KrossPop repurposes the sleep screen into a passive "card pull" moment: every
time the device wakes, the user sees a card from a curated, hand-tuned image
pool instead of (or in addition to) a static sleep image. The pull is
quasi-random, weighted by rarity and by time/calendar context, and the device
quietly tracks how long each card is kept on screen as an implicit signal of
which idols/cards the user favors — without any explicit rating UI.

## Feature Ideas

### Card Draw on Wake (core loop)

- **Status:** idea
- **User value:** turns every wake into a small, low-effort collectible moment
  rather than a static sleep image.
- **Mechanic:**
  - Every wake forces a redraw — the previously selected card is deselected
    regardless of how the device woke (timeout, button, etc.), so there is no
    "keep the same card" path other than re-drawing the same one by chance.
  - Draw is quasi-random, weighted by rarity tier (common/rare/ultra-rare).
  - Anti-repeat logic: a card that's just been shown enters a cooldown window
    and can't be redrawn until it expires.
- **Hardware/technical notes:**
  - Likely lands in/near [`SleepActivity`](src/activities/boot_sleep/SleepActivity.h) —
    it already owns per-mode sleep screen rendering
    (`renderCustomSleepScreen`, `renderBitmapSleepScreen`, etc.) and already
    allocates full-panel grayscale plane buffers once and reuses them
    (`grayscaleLsbBuffer`/`grayscaleMsbBuffer`), which is the pattern any new
    card-rendering path should follow rather than allocating per-draw.
  - Anti-repeat/cooldown state needs to persist across sleep/wake cycles, so it
    can't just live in `SleepActivity` member state (the activity is
    heap-allocated and destroyed on exit per [`AGENTS.md`](AGENTS.md)'s
    Activity Lifecycle rules) — needs a small persisted store (SD-backed or a
    settings-adjacent store), sized deliberately given the ~380 KB RAM budget.
  - "Weighted quasi-random with anti-repeat and cooldown" needs a defined data
    shape (card ID → last-shown timestamp, cooldown-until timestamp) before
    implementation — see Open Questions.
- **Open questions:**
  - Draw-eligibility state's file format/location is now drafted — see
    [Persistence Architecture](#persistence-architecture).
  - What's the actual rarity-tier weighting formula, and does it get
    parameterized or hardcoded?

### Card Asset Pipeline

- **Status:** idea
- **User value:** ensures every card looks good on the 2-bit e-ink panel
  instead of relying on generic dithering.
- **Mechanic:**
  - All source images are manually converted, with contrast/brightness tuned
    per-image for the display — explicitly no automated/scraped feeds.
  - Cards are assigned a rarity tier (common/rare/ultra-rare) at creation time.
- **Hardware/technical notes:**
  - This fork already has a BMP pipeline used for sleep images/file viewer
    (see the recent `c4ade826` "quicker BMP rendering" work and
    [`Bitmap.h`](lib/GfxRenderer/Bitmap.h)) — card assets should reuse this
    pipeline rather than a new one.
  - Rarity tier and any metadata (tags for time/calendar gating) need a
    manifest format decided before card assets are authored at scale, so
    tagging isn't redone by hand later.
- **Open questions:**
  - Where do card assets live on SD — same convention as existing sleep
    images, or a new dedicated folder? (Manifest/authoring format itself is
    now drafted — see [Persistence Architecture](#persistence-architecture).)
  - Tag-based gating (time/calendar tags per card) still needs its field
    shape decided within the authoring workbook.

### Calendar-Aware Draw Weighting

- **Status:** idea
- **User value:** makes pulls feel alive and occasion-aware (member days,
  comeback days, anniversaries) without requiring connectivity.
- **Mechanic:**
  - Enabled by the persistent RTC — no autonomous/scheduled wake needed, this
    only evaluates at the moment the device wakes anyway.
  - Time-of-day and day-of-week can reskin or narrow the active card pool
    (e.g. member-day, comeback-day pools).
  - Delay-based rarity boost: the longer the gap since the last wake, the
    better the odds of a rare pull.
  - Anniversary/date-gated ultra-rare cards tied to real calendar dates.
- **Hardware/technical notes:**
  - Time source: [`HalClock`](lib/hal/HalClock.h) — X3 has a dedicated RTC
    that holds accurate wall-clock time across sleep per `SCOPE.md`'s Clock
    Display table; X4 relies on the ESP32-C3's internal RTC, which drifts
    during deep sleep without NTP. This layer's date/day-of-week logic is only
    as reliable as that underlying clock — X4 behavior needs to be explicitly
    scoped (accept drift, or require occasional sync) rather than assumed
    accurate.
  - "Gap since last wake" needs the previous wake timestamp persisted
    somewhere durable across sleep (same persistence question as the cooldown
    state above — likely the same store).
- **Open questions:**
  - Does this feature get gated to X3-only (reliable RTC) or supported on both
    with X4 caveats surfaced to the user?
  - Are member-day/comeback-day dates hardcoded per card in the manifest, or
    is there a separate calendar/event table?

### Retention Tracking & Stat Card

- **Status:** idea
- **User value:** a passive, no-UI way of surfacing which cards/idols the user
  actually favors, without asking them to rate anything.
- **Mechanic:**
  - Fully passive: logs (card ID, draw timestamp, next-draw timestamp) every
    time a card is shown — no explicit UI.
  - Retention duration is the reluctance/preference signal: keeping a card up
    costs the user the device's other (awake) functionality, so longer
    retention implies stronger preference.
  - Scoring is computed at *read* time via a diminishing curve, not at write
    time — raw duration data is logged unweighted so the curve can be
    redesigned/retested later without losing history.
  - Curve shape (hours vs. days scale) is intentionally left open, to be tuned
    empirically against real usage data rather than decided upfront.
  - Output is surfaced periodically as a generated stat card (e.g. monthly
    "longest-held" card), rendered through the same BMP pipeline as regular
    cards — not a hidden log or dashboard.
- **Hardware/technical notes:**
  - "Log every draw, unweighted, keep history" implies an append-only or
    rolling log on SD, sized and rotated deliberately — this is exactly the
    kind of persistent-write path `AGENTS.md`'s Resource Rules flag ("debounce
    persistent writes... do not write on every page turn"); a write per wake
    is much rarer than per-page-turn, but the log growth over months of daily
    use still needs a retention/rotation policy.
  - Deriving the stat card requires reading back that log — decide whether
    that happens on-device (parse log, compute curve, render stat card) or
    whether it's designed for computation elsewhere (e.g. via the existing
    web-transfer/file-transfer surface) given ESP32-C3 CPU/RAM constraints.
- **Open questions:**
  - Log format and monthly rotation are now drafted — see
    [Persistence Architecture](#persistence-architecture).
  - Is the diminishing curve computed entirely on-device, or exported for
    external tuning during development?
  - Cadence for the generated stat card (monthly, on-demand, both)?

## Persistence Architecture

Draft covering card asset identity, the authoring workflow, and the on-device
data format. This is a deliberately minimal hand-rolled database — fixed-size
heap files with direct offset addressing — not a general SQL engine; see
"Explicitly rejected" below for why.

### Card asset naming

- Card BMPs live as a flat folder of files (no subfolder hierarchy needed for
  identity).
- Filename = a zero-padded numeric card ID, optionally followed by a
  human-readable suffix for the author's own convenience (e.g.
  `000042_iu_checkmate.bmp`). The ID is always the leading token, delimited so
  it's trivially strippable by both the build script and firmware (parse
  leading digits up to the first non-digit character).
- An ID is assigned once per card and never reused, even after a card is
  retired from the pool.

### Authoring source of truth

- A single local `.xlsx` workbook — can live on the SD card itself — with one
  sheet per entity: Cards, Idols, Groups, Subsidiaries, Agencies (Releases/Eras
  can be added later as one more sheet without touching existing ones).
- Every entity sheet has three columns: `ID` (a plain integer, hand-assigned
  once when the row is created, frozen forever after — never derived from
  name, never reused), `DisplayName` (freely editable at any time), and a
  `Label` formula column that concatenates `ID — DisplayName (disambiguating
  context)` (e.g. `45 — Yuri (SNSD)` vs. `46 — Yuri (Girl's Day)`).
- Any sheet referencing another entity (e.g. Cards → Idols) uses a dropdown
  validation column sourced from that entity's `Label` column. References are
  always picked, never typed — this eliminates typos and same-name collisions
  entirely, since the visible label is disambiguated by its leading ID.
- Renaming an entity later only edits its own row's `DisplayName`; already-
  filled reference cells keep whatever label text was baked in at pick-time
  (cosmetically stale, harmless) since the build script only ever parses the
  leading `ID —` prefix and ignores the rest of the string.
- The build script (Python, alongside the existing `scripts/*.py` generators
  such as `scripts/gen_i18n.py`) opens the workbook with `openpyxl` and parses
  every sheet directly — no manual "export to CSV" step. It validates
  referential integrity (every FK resolves to a real ID, no malformed Label
  parses) before compiling.
- The script is deliberately **read-only** against the workbook — it never
  auto-fills blank IDs or writes back into the file — because `openpyxl`'s
  support for writing data-validation dropdowns back to a file is inconsistent
  across versions and risks silently corrupting the very dropdowns the
  workflow depends on.

### On-device compiled format

- For each entity, the script emits a small fixed-size-record binary file,
  indexed by that entity's own dense ID: record `N` lives at
  `headerSize + N * recordSize`, so looking up a row is a direct `seek()` —
  no on-device lookup table needed.
- Reference/lookup tables (`idols.data.bin`, `groups.data.bin`,
  `subsidiaries.data.bin`, `agencies.data.bin`) are expected to be small
  (dozens to low hundreds of rows) and can be loaded fully into RAM and kept
  resident, since they're orders of magnitude smaller than the card table.
- `cards.data.bin` is the large table (hundreds to thousands of rows) and is
  never held resident — always accessed via seek or streamed sequentially.
- Forward-chain resolution (`card → idol → group → subsidiary → agency`) is a
  handful of O(1) seeks; with the small reference tables cached, it's free
  after their first load.

### Filtered/attribute queries (e.g. "random card from group X")

- Not natively O(1), since `cards.data.bin` is only indexed by card ID, not by
  idol or group.
- Default approach: a linear scan of `cards.data.bin`, checking each record's
  `idolId` against the (RAM-cached) idol→group mapping, using **reservoir
  sampling** to pick a uniformly random match in a single streaming pass with
  O(1) memory — no need to materialize a list of matches. Expected to be
  cheap in practice given expected card counts (sequential SD reads, tiny
  fixed record size).
- Escape hatch, only if real-hardware profiling shows this insufficient: the
  build script already resolves every card's full chain to validate FKs, so
  it can additionally emit a precomputed inverted index (e.g.
  `cards.index.group.bin`: per-group contiguous run of matching card IDs, with
  a tiny per-group offset header) — this shifts the cost to build time, free
  on-device. Do not build this preemptively; add it only once a specific
  operation is shown to need it.

### Retention log & no-repeat/cooldown

- Append-only, fixed-size records (`cardId`, `drawnAt`, `nextDrawnAt`),
  rotated monthly (`retention.<YYYYMM>.data.bin`) — bounds file growth and
  lines up with the planned monthly stat-card cadence.
- No separate no-repeat/cooldown buffer file: cooldown status is derived by
  tail-reading the last N records directly (`seek(fileSize - N*recordSize)`,
  read forward), with a bounded fallback to also tail-read the previous
  month's file if the current month doesn't yet have N entries (i.e. shortly
  after a rotation).
- Retention scoring (the diminishing curve from Retention Tracking & Stat
  Card) is applied at *read* time against the raw, unweighted log so the
  curve can be redesigned later without losing history.

### File naming & schema convention

Per table, three kinds of file, sharing a `<table>.` prefix so a directory
listing groups by table first:

- `<table>.schema.json` — one per table. Field list (name + type; offsets
  computed from declaration order, not hand-specified) plus a version number.
  This is a **build-time-only artifact** — the firmware never parses JSON at
  runtime. It's the single source of truth that generates two things: the
  Python build script's packing logic, and a generated C++ struct header for
  firmware (mirroring how `scripts/gen_i18n.py` generates C++ from YAML
  today), so the two sides can never silently drift apart. Optionally copied
  onto the SD card alongside the `.bin` files purely as human-readable
  documentation — never functionally required there.
- `<table>.data.bin` — the fixed-size-record heap file itself. Carries only a
  leading `u8 version` byte plus records — no self-description, matching the
  existing `book.bin`/`reader_settings.bin` convention in
  [`docs/file-formats.md`](docs/file-formats.md).
- `<table>.index.<keyField>.bin` — one per secondary index (e.g.
  `cards.index.group.bin`), only built if/when the linear-scan default proves
  insufficient. Index layout (offset table + flat ID list) is described in the
  same `<table>.schema.json` under an `"indexes"` section rather than a
  separate schema file per index.
- Monthly-rotated tables (the retention log) keep one shared
  `retention.schema.json` with per-month data files:
  `retention.202603.data.bin`, `retention.202604.data.bin`, etc.

### Explicitly rejected: SQLite on-device

Considered and set aside for the firmware runtime:

- RAM budget (~380 KB, no PSRAM) doesn't comfortably fit SQLite's page
  cache/VDBE overhead alongside rendering buffers, the EPUB parser, and
  everything else already competing for that budget.
- Real hardware storage goes through `HalFile`/SdFat
  ([`HalStorage.h`](lib/hal/HalStorage.h)), not POSIX — only the simulator HAL
  is POSIX-backed per `.claude/CONTEXT.md` — so it would need a custom VFS
  port, a real integration cost, not a drop-in.
- The append-only retention log fights a transactional B-tree write model
  (journal/WAL overhead per write) versus one plain sequential append; SD
  write amplification/wear matters for something written on every wake, every
  day, for the device's lifetime.
- The actual query set here (forward-chain resolve, group-filtered random
  draw, monthly aggregation) is small and fully known in advance — exactly
  the situation where a hand-rolled fixed-record format outperforms a general
  query engine, since every access pattern can be made O(1) or a cheap bounded
  scan by construction.

SQLite (and Airtable/Notion) were also considered and set aside for the
authoring side, in favor of a local spreadsheet — see Authoring source of
truth above. The deciding constraints were: no external services, no internet
dependency, and a real search/pick UX for references without hand-building a
UI.

## Rust Integration

KrossPop-specific on-device app logic (the card-DB engine: record
parsing/packing, weighted rarity draw, reservoir sampling, retention-curve
scoring) is planned to be written in **Rust**, not C++, and linked into the
existing C++/PlatformIO/Arduino firmware — a deliberate choice, not a
technical necessity: see Open Questions/rationale below.

- **Naming convention:** Rust crates live under the top-level `rust/` folder
  (e.g. `rust/krosspop_core`), not under `lib/` — PlatformIO's Library
  Dependency Finder scans `lib/` expecting real C/C++ libraries, and a Cargo
  crate's `target/` directory (which balloons as a crate grows) has no
  business being walked by it. This mirrors `web/`, which is already a
  foreign-toolchain source tree feeding the firmware through its own build
  step (`scripts/build_web.py`), not PlatformIO's normal C/C++ scanning. On
  the C++ side, every FFI declaration header for a Rust crate lives under the
  root-level `src/rust_bridge/<CrateName>Ffi.h` (e.g.
  `src/rust_bridge/KrosspopCoreFfi.h`) — a dedicated, clearly-named folder so
  Rust-boundary scaffolding is visually distinct from ordinary KrossPop C++
  app code (activities, rendering, etc.) elsewhere under `src/`.
- **Split:** C++ keeps everything platform-coupled (`HalFile`/`HalStorage` I/O,
  rendering, activity lifecycle). Rust owns pure computation only — no file
  I/O, no display calls, no ESP-IDF API surface — operating on byte buffers
  C++ already read, returning primitives or fixed `#[repr(C)]` structs.
- **Why this split de-risks the integration:** because the Rust code never
  touches ESP-IDF APIs, it doesn't need `esp-idf-sys`/`esp-idf-hal` at all — it
  can be plain `#![no_std]` Rust targeting the RISC-V ISA directly (ESP32-C3
  is RISC-V, upstream-rustc-supported, unlike the older Xtensa ESP32 chips
  which need a custom LLVM fork), compiled to a static library and linked into
  the existing PlatformIO/Arduino build via `extra_scripts` in
  `platformio.ini`. This avoids the biggest risk of a fuller Rust
  integration — Arduino-ESP32's pinned ESP-IDF version/sdkconfig potentially
  conflicting with a Rust toolchain that wants to build/own its own ESP-IDF —
  because there's no second ESP-IDF instance to reconcile.
- **Simulator support, with no per-call-site conditional compilation:** because
  the crate is platform-agnostic by design, `scripts/build_rust.py` compiles
  it for the ESP32-C3's RISC-V target in real firmware envs (`[base]`) and for
  the host's native target in `[env:simulator]` — same source, two targets,
  selected by which PlatformIO env is building. This means C++ call sites
  never need `#ifndef SIMULATOR` guarding; there's always a working
  implementation linked in. That stops working the moment a Rust function
  needs something ESP-IDF-only (see the wrapper-function entry below) — such
  a function would need its own simulator-side stand-in, on a case-by-case
  basis, not a blanket guard.
- **No `alloc` initially, and this is confirmed additive, not a lock-in:**
  functions borrow caller-owned `&[u8]` in, return fixed-size data out — no
  `Vec`/`String`/`Box`. This avoids needing a `#[global_allocator]` wired to
  the same heap C++ already uses. Enabling `alloc` later only requires
  registering a global allocator once (ESP32 prior art exists: the
  `esp-alloc` crate wraps ESP-IDF's heap as a Rust allocator, though whether
  it fits this Arduino-framework setup specifically needs its own small check
  when the time comes) — existing borrow-only functions don't need to change.
  The one place new complexity would appear is a future function that hands
  an *owned* allocation across the FFI boundary itself (a "who calls free,
  with which allocator" question) — that cost lands only on such a function
  when/if it's written, not retroactively.
- **If Rust ever needs something ESP-IDF provides (time, GPIO, etc.):** don't
  reach for `esp-idf-sys`/`bindgen` against ESP-IDF's real API — that would
  mean two independently-built copies of ESP-IDF needing to agree byte-for-
  byte (Arduino-ESP32/PlatformIO's existing one, plus whatever `esp-idf-sys`
  builds for itself), which is a real drift/ABI-mismatch risk. Instead, write
  a small C++ wrapper function (compiled as ordinary part of this project's
  existing build, so it's automatically the same ESP-IDF/sdkconfig as
  everything else) that does the real ESP-IDF call and exposes a tiny,
  hand-designed `extern "C"` signature with only primitive types — the same
  "C++ owns the platform, Rust only sees a narrow plain interface" shape as
  the rest of this design, just one layer further down. This is the standard
  pattern for embedding a new language into an existing large C/C++ codebase.
  Each new ESP-IDF capability Rust needs gets its own small wrapper — no
  blanket access, which is a deliberate/auditable boundary, not a limitation.
- **Panic policy:** Rust panics can't unwind across an FFI boundary and must
  not violate `AGENTS.md`'s "no exceptions, no `abort()`" rule. Resolution:
  treat a Rust panic as the equivalent of this codebase's `assert(false)` — a
  signal of a truly-impossible state, never used for routine errors. Routine
  failures are modeled as an `extern "C"` function returning an error code,
  matching the existing `LOG_ERR` + `return false` C++ convention.
- **Status:** POC validated on real hardware (X3). `rust/krosspop_core` (a
  `no_std`, `alloc`-free static lib) is linked into `[base]` firmware builds
  via `scripts/build_rust.py`, called from `main.cpp` through a plain
  `extern "C"` boundary (`src/rust_bridge/KrosspopCoreFfi.h`), and confirmed
  at boot over serial: `krosspop_poc_add(2, 3) = 5`. Firmware size impact was negligible
  (5,493,904 / 6,553,600 bytes, ~83.8% of the `app0` partition — comparable to
  pre-Rust size). `scripts/build_rust.py` now also builds the same crate for
  the host target and is wired into `[env:simulator]` — confirmed correctly
  detecting the env and linking the host-built lib (`KrossPop Rust core
  linked (host): ...`), but a full `pio run -e simulator` couldn't be
  verified end-to-end: it currently fails on an unrelated pre-existing bug
  (see `.claude/CONTEXT.md`'s Simulator section — a `WifiSelectionActivity.cpp`
  vs. simulator `WiFi.h` stub mismatch), unrelated to this work. Next step:
  design the real DB-engine FFI functions (record
  parsing, weighted draw, reservoir sampling) and remove the trivial POC call
  once real functionality replaces it.
- **`AGENTS.md` follow-up, not yet done:** once the POC and the policy
  decisions above are proven out in practice, add a Rust-specific section (or
  sibling file, referenced from `AGENTS.md` the way `CLAUDE.md` already just
  points elsewhere) rather than rewriting the existing C++-oriented rules,
  since the bulk of the firmware stays C++.
- **Note on `AGENTS.md` generally:** it was written for CrossInk/CrossPoint, an
  entirely-C++ publicly-distributed reader firmware. KrossPop is a divergent
  successor, not a strict continuation — its code-culture rules are not fixed
  for this project and are expected to keep evolving as KrossPop's feature set
  and toolchain diverge further, not preserved for their own sake.

## Deferred / Secondary Ideas

Ideas that are intentionally not part of the initial build — noted so they
aren't lost, not because they're scheduled.

- **IMU shake-to-reveal:** bolts onto the reveal moment later; not a primary
  trigger for the initial core loop.
- **NFC:** unexplored — hardware presence on this device still needs to be
  confirmed before this goes anywhere. There's prior X3 NFC investigation in
  `research/nfc-findings.md` (paused, no conclusion drawn) — related hardware
  research but not the same goal as this feature.
- **Retention-weighted rarity feedback loop:** using past retention data to
  bias future draw weighting toward (or away from) favored idols — a possible
  future layer on top of Retention Tracking, not committed.

## Technical Constraints Log

Cross-cutting technical findings that affect multiple features (e.g. RAM budget
tradeoffs, storage/cache format decisions, new HAL surfaces needed). Keep this in
sync with `.claude/CONTEXT.md` if a finding is durable and reusable beyond this
roadmap.

- Persisted state across sleep/wake cycles (cooldown/no-repeat, retention log)
  is now drafted in [Persistence Architecture](#persistence-architecture).
  Still open: where the "gap since last wake" timestamp for Calendar-Aware
  Draw Weighting lives — likely its own tiny single-record file, rewritten in
  place each wake, separate from the per-card `cards.data.bin` slots.

## Open Questions

- Overall: is the card-draw screen a replacement for existing sleep screen
  modes, a new sleep screen mode alongside the existing ones (Custom, Cover,
  Reading Stats, Minimal, Dashboard — see
  [`SleepActivity.h`](src/activities/boot_sleep/SleepActivity.h)), or does it
  eventually take over the "Custom"/"Favorite" sleep image slot entirely?
- What's the minimum viable card pool size/rarity distribution for the first
  build?
