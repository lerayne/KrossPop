# CrossPoint Reader — Durable Context

Keep this file focused on repo-specific gotchas that are worth reusing in future sessions.

## Simulator

- Simulator patches belong in the adjacent `crosspoint-simulator` repo.
- The valid local simulator env in this repo is `simulator`, and `pio run -e simulator` currently builds cleanly.
- The simulator `PNGdec` stub in `crosspoint-simulator/src/PNGdec.h` needs to mirror the real API shape used by app code, including `hasAlpha()` and `getTransparentColor()`, even though decode still fails intentionally.
- Known simulator limits:
  - No image rendering: `platformio.ini` ignores `hal`, `PNGdec`, and `JPEGDEC`, so image decoders are intentionally absent.
  - JPEGDEC stub always fails; `JPEGDEC fallback: open failed (err=-1)` is expected in simulator.
  - `esp_deep_sleep_start()` is a no-op in simulator.
  - `HalStorage` uses POSIX file access under `./fs_` and allows multiple readers, unlike real hardware.

## Real Hardware / Storage

- SdFat on hardware allows only one open reader per file path at a time. If a fallback needs to reopen the same file, close the first handle before reopening.

## Rendering / Reader Pipeline

- `lib/Epub/Epub/Page.cpp`: images must render only in `GfxRenderer::BW`; grayscale passes are text anti-aliasing passes only.
- Kindle EPUBs may contain paired high-res and old-Kindle fallback images. `ChapterHtmlSlimParser` should skip `<img>` nodes with `data-AmznRemoved-M8` to avoid duplicate stacked images.
- After image/layout pipeline changes that affect cached EPUB output, clear the affected `.crosspoint/epub_<hash>/` cache if behavior looks stale.

## E-Ink Refresh Modes (X3, `Uc8253X3Driver`)

Hard-won from a long session of log-instrumented hardware testing; re-read this before touching any `HalDisplay::RefreshMode`/`displayGrayscaleBase` call.

- `HALF_REFRESH` and `FULL_REFRESH` both drive *every* pixel to target — neither is a partial/differential update. Only `FAST_REFRESH` is genuinely differential (compares against `DTM1`, redrives only changed pixels).
- `FULL_REFRESH` is unconditional (`display()`'s `doFullSync` check includes `!fastMode && !halfMode`, so Full always wins) and resets `DTM1` to a white baseline before transitioning, using the strongest waveform bank (`_cfg.full`).
- `HALF_REFRESH` by itself uses a lighter bank (`_cfg.half`, "drive every pixel to target ignoring DTM1", no white-baseline reset) — but escalates to Full's behavior if `_forceFullSyncNext`/`!_redRamSynced`/pending initial syncs are set. `HalDisplay::displayBuffer()`/`displayGrayscaleBase()` call `einkDisplay.requestResync(1)` to force this escalation — that's the anti-ghosting mechanism, and it always upgrades to Full-strength, never to genuine Half.
- `displayGrayscaleBase()`'s cheap "happy path" (`cleanBaseNeeded == false`) always uses the `preBwMid` bank (strong-for-changed/gentle-for-unchanged vs `DTM1`) regardless of the `fallback` mode argument — the mode only matters on the expensive `cleanBaseNeeded == true` path.
- `grayscaleRevert()` fires automatically inside `display()`/`displayGrayscaleBase()` whenever `_inGrayscaleMode` is still set (i.e. the previous draw ended in a grayscale nudge) — it's a real extra refresh using the same `_cfg.half` bank, which is why grayscale→grayscale transitions cost one more refresh than B/W→grayscale.
- Multi-stage visible flashes (blank → inverted-looking echo → final image) during Half/Full refreshes are inherent panel waveform physics, not a bug — don't try to "fix" the flash itself.
- `waitBusy()` (`EpdBus.cpp`) polls a real busy GPIO pin, not a fixed timer — refresh-duration log measurements reflect genuine hardware time.
- Known unsolved tradeoff: relying on any previously-established "clean" baseline (our own blank flash, or the driver's own `grayscaleRevert()`) for a cheap follow-up `FAST_REFRESH`/happy-path draw still leaves faint residual ghosting on "boring" (always-white) pixels after many repeated fast switches, because those pixels only ever get `preBwMid`'s gentle reinforcement, never a strong redrive. `src/activities/util/BmpViewerActivity.cpp`'s `bmpViewerFastRedraw` setting exists to let users trade this off explicitly; forcing `HALF_REFRESH`/`FULL_REFRESH` every time is the only fully ghost-free option.

## Settings UI

- Registering a setting in `SettingsList.h`'s `getSettingsList()` (`SettingInfo::Toggle`/`Enum`) is necessary but **not sufficient** for it to appear on-device. Each settings screen (e.g. Display) has its own separate curation function (e.g. `buildGroupedDisplaySettingsList()`) that hand-picks which registered settings to actually show, via `addDisplaySetting(StrId::...)`-style calls. Forgetting this step means the setting silently doesn't appear anywhere — no error, no warning.

## Misc Repo Gotchas

- POSIX TZ signs are inverted from ISO 8601 in `TimeStore::applyTimezone()`: `"UTC-1"` means UTC+1.
- `LyraTheme::drawHeader()` does not call `BaseTheme::drawHeader()`, so header changes in the base theme must be duplicated in Lyra if needed.
- The `freeink-sdk` git submodule (and its own nested `lucide` icons submodule) may not be checked out on a fresh clone — `git submodule status` shows a leading `-` when uninitialized. Run `git submodule update --init --recursive`; without it, `EInkDisplay`/driver symbols show as "no member" in the IDE.
- IDE/clangd diagnostics can report stale/phantom errors (wrong line, nonsense identifiers) right after an edit that shifts line numbers, especially in large files. Verify by reading the actual file content before trusting a reported error — don't assume a real regression from diagnostics alone.
