#!/bin/bash

# Resolve the interpreter: bare `python` on PATH is often PlatformIO's bundled
# penv, which lacks freetype-py/fonttools. Prefer the project venv, and let
# PYTHON=... override.
if [ -z "${PYTHON:-}" ]; then
  if [ -x "$(dirname "$0")/../../../.venv/bin/python3" ]; then
    PYTHON="$(cd "$(dirname "$0")/../../.." && pwd)/.venv/bin/python3"
  else
    PYTHON=python3
  fi
fi
if ! "$PYTHON" -c "import freetype" 2>/dev/null; then
  echo "ERROR: $PYTHON cannot import freetype." >&2
  echo "  Install deps:  .venv/bin/pip install -r scripts/requirements.txt" >&2
  echo "  Or override :  PYTHON=/path/to/python $0" >&2
  exit 1
fi
echo "Using interpreter: $PYTHON"

# Which fonts to (re)generate. Reading fonts rasterise slightly differently
# across FreeType versions, so regenerating them on a machine other than the
# one that produced the committed headers churns ~200 files for no intended
# change. Default to UI only; pass "all" or "reading" when you mean it.
TARGET="${1:-ui}"
case "$TARGET" in
  ui|reading|all) ;;
  *) echo "usage: $0 [ui|reading|all]   (default: ui)" >&2; exit 2 ;;
esac


set -e

cd "$(dirname "$0")"

EMOJI_FONT="../builtinFonts/source/NotoEmoji/NotoEmoji-Regular.ttf"
SYMBOLS_FONT="../builtinFonts/source/NotoSymbols/NotoSansSymbols-Regular.ttf"
PHM_FONT="../builtinFonts/source/NotoSansCJKsc/NotoSansCJKsc-Regular.otf"

# Additional Unicode intervals to include beyond the default Latin/Cyrillic/math set.
# 0x2669-0x266F: Music notes and accidentals (♩♪♫♬♭♮♯)
# Emoticons subset, excluding lower-value faces to keep firmware size down.
# 0x1F44B-0x1F44F: Hand gesture emojis (👋👌👍👎👏)
# 0x2764: Heart symbol (❤️)
# 0x03BB: Greek lambda (λ)
# 0x0410-0x0414, 0x0418, 0x041B, 0x041D-0x0423, 0x0425, 0x0427,
# 0x042B-0x042C, 0x042E-0x0432, 0x0434-0x0435, 0x0437, 0x043A,
# 0x043D-0x043E, 0x0440, 0x0442, 0x0446, 0x044C, 0x044E: Cyrillic subset
# 0x2113: Script small l (ℓ)
COMMON_FALLBACK_INTERVALS=(
  --additional-intervals 0x03BB,0x03BB
  --additional-intervals 0x0410,0x0414
  --additional-intervals 0x0418,0x0418
  --additional-intervals 0x041B,0x041B
  --additional-intervals 0x041D,0x0423
  --additional-intervals 0x0425,0x0425
  --additional-intervals 0x0427,0x0427
  --additional-intervals 0x042B,0x042C
  --additional-intervals 0x042E,0x0432
  --additional-intervals 0x0434,0x0435
  --additional-intervals 0x0437,0x0437
  --additional-intervals 0x043A,0x043A
  --additional-intervals 0x043D,0x043E
  --additional-intervals 0x0440,0x0440
  --additional-intervals 0x0442,0x0442
  --additional-intervals 0x0446,0x0446
  --additional-intervals 0x044C,0x044C
  --additional-intervals 0x044E,0x044E
  --additional-intervals 0x2113,0x2113
)

EMOJI_ONLY_INTERVALS=(
  --additional-intervals 0x2669,0x266F
  --additional-intervals 0x1F600,0x1F607
  --additional-intervals 0x1F609,0x1F614
  --additional-intervals 0x1F618,0x1F618
  --additional-intervals 0x1F61A,0x1F61A
  --additional-intervals 0x1F61C,0x1F61D
  --additional-intervals 0x1F620,0x1F622
  --additional-intervals 0x1F624,0x1F625
  --additional-intervals 0x1F629,0x1F629
  --additional-intervals 0x1F62C,0x1F62E
  --additional-intervals 0x1F631,0x1F635
  --additional-intervals 0x1F641,0x1F642
  --additional-intervals 0x1F644,0x1F644
  --additional-intervals 0x1F44B,0x1F44F
  --additional-intervals 0x2764,0x2764
)

BASE_FALLBACK_INTERVALS=(
  "${COMMON_FALLBACK_INTERVALS[@]}"
  "${EMOJI_ONLY_INTERVALS[@]}"
)

PHM_INTERVALS=(
  --additional-intervals 0x4F1A,0x4F1A
  --additional-intervals 0x53BB,0x53BB
  --additional-intervals 0x5458,0x5458
  --additional-intervals 0x59DA,0x59DA
  --additional-intervals 0x5B98,0x5B98
  --additional-intervals 0x5BA4,0x5BA4
  --additional-intervals 0x5E26,0x5E26
  --additional-intervals 0x6211,0x6211
  --additional-intervals 0x62C9,0x62C9
  --additional-intervals 0x653E,0x653E
  --additional-intervals 0x6746,0x677F
  --additional-intervals 0x7532,0x7532
  --additional-intervals 0x7684,0x7684
  --additional-intervals 0x8BAE,0x8BAE
  --additional-intervals 0x8BF7,0x8BF7
  --additional-intervals 0x91CA,0x91CA
)

CHAREINK_FALLBACK_RANGES=(
  0x03BB,0x03BB
  0x0410,0x0414
  0x0418,0x0418
  0x041B,0x041B
  0x041D,0x0423
  0x0425,0x0425
  0x0427,0x0427
  0x042B,0x042C
  0x042E,0x0432
  0x0434,0x0435
  0x0437,0x0437
  0x043A,0x043A
  0x043D,0x043E
  0x0440,0x0440
  0x0442,0x0442
  0x0446,0x0446
  0x044C,0x044C
  0x044E,0x044E
  0x2113,0x2113
)

EMOJI_FALLBACK_RANGES=(
  0x1F600,0x1F607
  0x1F609,0x1F614
  0x1F618,0x1F618
  0x1F61A,0x1F61A
  0x1F61C,0x1F61D
  0x1F620,0x1F622
  0x1F624,0x1F625
  0x1F629,0x1F629
  0x1F62C,0x1F62E
  0x1F631,0x1F635
  0x1F641,0x1F642
  0x1F644,0x1F644
  0x1F44B,0x1F44F
  0x2764,0x2764
)

SYMBOL_FALLBACK_RANGES=(
  0x2669,0x266F
)

PHM_FALLBACK_RANGES=(
  0x4F1A,0x4F1A
  0x53BB,0x53BB
  0x5458,0x5458
  0x59DA,0x59DA
  0x5B98,0x5B98
  0x5BA4,0x5BA4
  0x5E26,0x5E26
  0x6211,0x6211
  0x62C9,0x62C9
  0x653E,0x653E
  0x6746,0x677F
  0x7532,0x7532
  0x7684,0x7684
  0x8BAE,0x8BAE
  0x8BF7,0x8BF7
  0x91CA,0x91CA
)

READING_FONT_SIZES=(8 9 10 12 14 16 18 20)
READING_FONT_STYLES=("Regular" "Bold" "Italic" "BoldItalic")
READING_FONT_RENDER_ARGS=(--2bit --compress --pnum --darken-aa)

font_include_args() {
  local face_index="$1"
  shift
  for range in "$@"; do
    printf '%s\n' --font-include-intervals "${face_index}:${range}"
  done
}

generate_family() {
  local family_name="$1"
  local source_dir="$2"
  local source_prefix="$3"
  local output_dir="$4"
  local include_fallbacks="$5"
  local use_chareink_common_fallback="$6"

  for size in ${READING_FONT_SIZES[@]}; do
    for style in ${READING_FONT_STYLES[@]}; do
      local style_lower
      style_lower="$(echo $style | tr '[:upper:]' '[:lower:]')"
      local font_name="${family_name}_${size}_${style_lower}"
      local font_path="../builtinFonts/source/${source_dir}/${source_prefix}-${style}.ttf"
      local output_path="${output_dir}/${font_name}.h"
      local font_stack=("$font_path")
      local interval_args=()
      local include_args=()

      if [[ "$include_fallbacks" == "yes" ]]; then
        interval_args+=("${BASE_FALLBACK_INTERVALS[@]}")
        if [[ "$use_chareink_common_fallback" == "yes" ]]; then
          font_stack+=("../builtinFonts/source/ChareInk7/ChareInk7-${style}.ttf")
          include_args+=($(font_include_args $(( ${#font_stack[@]} - 1 )) "${CHAREINK_FALLBACK_RANGES[@]}"))
        fi
        font_stack+=("$EMOJI_FONT")
        include_args+=($(font_include_args $(( ${#font_stack[@]} - 1 )) "${EMOJI_FALLBACK_RANGES[@]}"))
        font_stack+=("$SYMBOLS_FONT")
        include_args+=($(font_include_args $(( ${#font_stack[@]} - 1 )) "${SYMBOL_FALLBACK_RANGES[@]}"))

        if [[ "$style" == "Regular" ]]; then
          interval_args+=("${PHM_INTERVALS[@]}")
          font_stack+=("$PHM_FONT")
          include_args+=($(font_include_args $(( ${#font_stack[@]} - 1 )) "${PHM_FALLBACK_RANGES[@]}"))
        fi
      fi

      "$PYTHON" fontconvert.py $font_name $size "${font_stack[@]}" "${interval_args[@]}" "${include_args[@]}" "${READING_FONT_RENDER_ARGS[@]}" > $output_path
      echo "Generated $output_path"
    done
  done
}

generate_reading_variant() {
  local output_dir="$1"
  local include_fallbacks="$2"
  local label="$3"

  mkdir -p "$output_dir"
  echo "Generating ${label} font variants..."
  generate_family lexenddeca LexendDeca LexendDeca "$output_dir" "$include_fallbacks" yes
  generate_family bitter Bitter Bitter "$output_dir" "$include_fallbacks" yes
  generate_family charein ChareInk7 ChareInk7 "$output_dir" "$include_fallbacks" no
  echo ""
  echo "${label} variants complete."
  echo ""
}

# Reading font variants:
#   builtinFonts/             default: emoji/symbol fallback + PHM CJK fallback
#   builtinFonts/noemoji/     OMIT_EMOJI_FONTS: primary fonts only, no emoji and no PHM CJK
if [[ "$TARGET" == "all" || "$TARGET" == "reading" ]]; then
  generate_reading_variant ../builtinFonts yes "default"
  generate_reading_variant ../builtinFonts/noemoji no "no-emoji"
else
  echo "Skipping reading fonts (TARGET=$TARGET)."
  echo ""
fi

if [[ "$TARGET" == "reading" ]]; then
  echo "Skipping UI fonts (TARGET=reading)."
  exit 0
fi

# UI Font - Terminus (bitmap) with Inter as fallback
#
# Terminus is a strike font: it renders as pure 1-bit black/white, so UI text
# has no antialiasing blur on the e-ink panel. fontconvert.py picks the strike
# nearest to size_pt * 150/72, so 8/10/12 pt land on the 16/20/24 px strikes.
#
# Terminus only covers Latin/Greek/Cyrillic, so Inter and IBM Plex Sans Hebrew
# stay in the stack behind it for everything else (Hebrew, emoji, Vietnamese).
# Those fall back to antialiased outlines, which looks slightly different from
# the crisp primary — only visible to users of those scripts.
#
# NOTE: Terminus ships only two weights (n/b) and we use bold as the *regular*
# UI weight, because its 2px stems hold up on e-ink where the 1px regular
# stems look washed out. That leaves nothing heavier for the Bold variant, so
# both currently map to the same strike. Revisit if UI bold needs to be
# visually distinct.
TERMINUS_VERSION=4.49.1
TERMINUS_DIR=downloaded_fonts/terminus-font-${TERMINUS_VERSION}

# Terminus strike (bold) per UI point size. Chosen by eye on the device
# rather than derived: Terminus's nominal size is the full cell, and its
# strikes are unevenly spaced (…20, 22, 24, 28, 32), so no single formula
# gives all three. Add an entry here if a new UI size appears.
terminus_strike_for() {
  local size="$1" style="${2:-Regular}"
  case "$size" in
     # Always bold at the small strikes: Terminus regular is 1px-stemmed
     # there and washes out on e-ink. Both UI weights map to the same file.
     8) echo "22b" ;;
    10) echo "28b" ;;
     # 32 is the only strike where Terminus regular is already 2px and bold
     # is a genuine 3px, so this size carries a real weight distinction —
     # which the UI uses (~87 regular vs ~50 bold call sites).
    12) if [ "$style" = "Bold" ]; then echo "32b"; else echo "32n"; fi ;;
     *) echo "ERROR: no Terminus strike mapped for ${size}pt (see terminus_strike_for)" >&2; exit 1 ;;
  esac
}


# downloaded_fonts/ is gitignored, so fetch on first run like build-sd-fonts.py does.
if [ ! -d "$TERMINUS_DIR" ]; then
  echo "Downloading Terminus ${TERMINUS_VERSION}..."
  mkdir -p downloaded_fonts
  curl -fsSL -o downloaded_fonts/terminus.tar.gz \
    "https://downloads.sourceforge.net/project/terminus-font/terminus-font-4.49/terminus-font-${TERMINUS_VERSION}.tar.gz" \
    || { echo "ERROR: could not download Terminus" >&2; exit 1; }
  tar xzf downloaded_fonts/terminus.tar.gz -C downloaded_fonts/
  # This lives under lib/EpdFont/, so PlatformIO's library dependency finder
  # compiles any C/C++ it finds here — and the tarball ships win32/*.c, which
  # fails on <windows.h>. Only the .bdf strikes are needed.
  find "$TERMINUS_DIR" -type f ! -name "*.bdf" -delete
  find "$TERMINUS_DIR" -type d -empty -delete
  rm -f downloaded_fonts/terminus.tar.gz
fi

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="inter_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    terminus_px="$(terminus_strike_for "$size" "$style")"
    terminus_path="${TERMINUS_DIR}/ter-u${terminus_px}.bdf"
    [ -f "$terminus_path" ] || { echo "ERROR: missing $terminus_path" >&2; exit 1; }
    font_path="../builtinFonts/source/Inter/Inter-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    "$PYTHON" fontconvert.py $font_name $size $terminus_path $font_path > $output_path
    echo "Generated $output_path (Terminus ${terminus_px}px + Inter fallback)"
  done
done

# Small UI Font - Terminus 16px + Inter fallback

"$PYTHON" fontconvert.py inter_8_regular 8 \
  "${TERMINUS_DIR}/ter-u$(terminus_strike_for 8).bdf" \
  ../builtinFonts/source/Inter/Inter-Regular.ttf > ../builtinFonts/inter_8_regular.h

echo ""
echo "Running compression verification..."
"$PYTHON" verify_compression.py ../builtinFonts/
