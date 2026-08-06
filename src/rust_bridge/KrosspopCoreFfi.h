#pragma once

// FFI declarations for rust/krosspop_core. See ROADMAP.md's "Rust
// Integration" section for the split/build details.
//
// krosspop_poc_add is the link-verification POC; remove once real
// DB-engine FFI functions replace it.
extern "C" int32_t krosspop_poc_add(int32_t a, int32_t b);

// Card Pull sleep screen MVP (see SleepActivity::renderCardPullSleepScreen).
// Given a random value and how many cards exist, returns which index to
// show — must return a value in [0, count). No rarity/cooldown weighting
// yet; that layers on top once the real card-DB engine exists.
extern "C" uint32_t krosspop_pick_card_index(uint32_t random_value, uint32_t count);
