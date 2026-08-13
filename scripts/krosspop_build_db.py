#!/usr/bin/env python3
"""
Compile the KrossPop card database from the LibreOffice workbook.

    krosspop/krosspop.fods + krosspop/schema/tables.json
        -> krosspop/build/<table>.data.bin   (copy to SD card)
        -> src/krosspop/KrosspopDb.generated.h

Reads the workbook, never writes to it. Stdlib only. See krosspop/README.md
for the workbook layout this expects.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCHEMA_PATH = os.path.join(PROJECT_DIR, 'krosspop', 'schema', 'tables.json')
WORKBOOK_PATH = os.path.join(PROJECT_DIR, 'krosspop', 'krosspop.fods')
BUILD_DIR = os.path.join(PROJECT_DIR, 'krosspop', 'build')
HEADER_PATH = os.path.join(PROJECT_DIR, 'src', 'krosspop', 'KrosspopDb.generated.h')

OFFICE = '{urn:oasis:names:tc:opendocument:xmlns:office:1.0}'
TABLE = '{urn:oasis:names:tc:opendocument:xmlns:table:1.0}'
TEXT = '{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'

# LibreOffice pads sheets out to the full grid using repeat counts in the
# millions; expanding those literally would exhaust memory.
MAX_REPEAT = 4096

INT_FMT = {'u8': 'B', 'u16': 'H', 'u32': 'I', 'i8': 'b', 'i16': 'h', 'i32': 'i'}
CPP_TYPE = {'u8': 'uint8_t', 'u16': 'uint16_t', 'u32': 'uint32_t',
            'i8': 'int8_t', 'i16': 'int16_t', 'i32': 'int32_t'}
CHAR_RE = re.compile(r'^char\[(\d+)\]$')
LEADING_INT_RE = re.compile(r'^\s*(\d+)')


class BuildError(Exception):
    pass


def field_size(ftype):
    if ftype in INT_FMT:
        return struct.calcsize('<' + INT_FMT[ftype])
    m = CHAR_RE.match(ftype)
    if m:
        return int(m.group(1))
    raise BuildError(f'unknown field type: {ftype}')


def record_size(fields):
    return sum(field_size(f['type']) for f in fields)


# --- FODS reading ---------------------------------------------------------

def cell_value(cell):
    """Text of one cell. Formula cells expose their last cached result, which
    is what the Label columns need."""
    if cell.get(OFFICE + 'value-type') in ('float', 'percentage', 'currency'):
        raw = cell.get(OFFICE + 'value')
        if raw is not None:
            number = float(raw)
            return str(int(number)) if number.is_integer() else str(number)
    return '\n'.join(''.join(p.itertext()) for p in cell.findall(TEXT + 'p')).strip()


def sheet_rows(table_el):
    rows = []
    for row_el in table_el.findall(TABLE + 'table-row'):
        cells = []
        for cell in row_el.findall(TABLE + 'table-cell'):
            repeat = int(cell.get(TABLE + 'number-columns-repeated', 1))
            value = cell_value(cell)
            if not value and repeat > MAX_REPEAT:
                repeat = 1
            cells.extend([value] * min(repeat, MAX_REPEAT))
        while cells and not cells[-1]:
            cells.pop()

        repeat = int(row_el.get(TABLE + 'number-rows-repeated', 1))
        if not cells and repeat > MAX_REPEAT:
            repeat = 1
        for _ in range(min(repeat, MAX_REPEAT)):
            rows.append(list(cells))

    while rows and not any(rows[-1]):
        rows.pop()
    return rows


def read_workbook(path):
    """{sheet name: list of {column header: value}}"""
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise BuildError(
            f'{os.path.relpath(path, PROJECT_DIR)} is not valid XML ({exc}).\n'
            f'Make sure it was saved as "ODF Spreadsheet (Flat XML)" (.fods), '
            f'not .ods or .xlsx.') from exc
    sheets = {}
    for table_el in root.iter(TABLE + 'table'):
        rows = sheet_rows(table_el)
        if not rows:
            continue
        headers = [h.strip() for h in rows[0]]
        records = []
        for line_no, row in enumerate(rows[1:], start=2):
            if not any(row):
                continue
            entry = {h: (row[i].strip() if i < len(row) else '')
                     for i, h in enumerate(headers) if h}
            entry['__row__'] = line_no
            records.append(entry)
        sheets[table_el.get(TABLE + 'name')] = records
    return sheets


# --- validation -----------------------------------------------------------

def parse_leading_int(value):
    m = LEADING_INT_RE.match(value or '')
    return int(m.group(1)) if m else None


def check_columns(spec, rows, errors):
    """Every column the schema reads must exist, or names silently come out
    blank and references silently come out zero."""
    if not rows:
        return
    present = set(rows[0]) - {'__row__'}
    wanted = {spec['idColumn']} | {f['source'] for f in spec['fields'] if f.get('source')}
    for column in sorted(wanted - present):
        errors.append(f'sheet {spec["sheet"]!r}: no column named {column!r} '
                      f'(found: {", ".join(sorted(present))})')


def collect_ids(spec, rows, errors):
    """Map row -> ID, reporting duplicates and malformed values."""
    sheet = spec['sheet']
    id_column = spec['idColumn']
    ids = {}
    for row in rows:
        raw = row.get(id_column, '')
        parsed = parse_leading_int(raw)
        if parsed is None:
            errors.append(f'{sheet} row {row["__row__"]}: {id_column} is not a number ({raw!r})')
            continue
        if parsed in ids:
            errors.append(f'{sheet} row {row["__row__"]}: duplicate {id_column} {parsed} '
                          f'(already used on row {ids[parsed]})')
            continue
        ids[parsed] = row['__row__']
        row['__id__'] = parsed
    return ids


def encode_field(field, row, spec, known_ids, errors, warnings):
    sheet = spec['sheet']
    where = f'{sheet} row {row["__row__"]}'
    ftype = field['type']
    source = field.get('source')

    if source is None:
        return b'\x01' if field['name'] == 'flags' else bytes(field_size(ftype))

    raw = row.get(source, '')

    char_match = CHAR_RE.match(ftype)
    if char_match:
        width = int(char_match.group(1))
        suffix = field.get('suffix', '')
        if suffix and raw and not raw.lower().endswith(suffix.lower()):
            raw += suffix
        encoded = raw.encode('utf-8')
        if len(encoded) > width - 1:
            # Cut on a character boundary: slicing raw bytes can split a
            # multi-byte sequence (any Korean name does this) and hand the
            # firmware's text renderer invalid UTF-8.
            encoded = encoded[:width - 1].decode('utf-8', 'ignore').encode('utf-8')
            warnings.append(f'{where}: {source} truncated to fit {width - 1} bytes ({raw!r})')
        return encoded + bytes(width - len(encoded))

    if 'values' in field:
        if raw not in field['values']:
            allowed = ', '.join(sorted(field['values']))
            errors.append(f'{where}: {source} is {raw!r}, expected one of {allowed}')
            return bytes(field_size(ftype))
        value = field['values'][raw]
    elif 'ref' in field:
        value = parse_leading_int(raw)
        if value is None:
            errors.append(f'{where}: {source} does not start with an ID ({raw!r})')
            return bytes(field_size(ftype))
        if value not in known_ids.get(field['ref'], {}):
            errors.append(f'{where}: {source} points at {field["ref"]} ID {value}, '
                          f'which does not exist')
            return bytes(field_size(ftype))
    else:
        value = parse_leading_int(raw)
        if value is None:
            if field.get('required', True):
                errors.append(f'{where}: {source} is not a number ({raw!r})')
                return bytes(field_size(ftype))
            value = field.get('default', 0)

    low, high = field.get('min'), field.get('max')
    if low is not None and value < low or high is not None and value > high:
        errors.append(f'{where}: {source} is {value}, expected {low}..{high}')
        return bytes(field_size(ftype))

    return struct.pack('<' + INT_FMT[ftype], value)


# --- outputs --------------------------------------------------------------

def build_table(spec, rows, known_ids, errors, warnings):
    size = record_size(spec['fields'])
    ids = {row['__id__']: row for row in rows if '__id__' in row}
    count = max(ids) + 1 if ids else 0
    if count > 0xFFFF:
        raise BuildError(f'{spec["table"]}: highest ID {max(ids)} exceeds the 16-bit record count')

    blob = bytearray(struct.pack('<BBH', spec['version'], size, count))
    for index in range(count):
        row = ids.get(index)
        if row is None:
            blob += bytes(size)  # unused slot, flags bit0 = 0
            continue
        for field in spec['fields']:
            blob += encode_field(field, row, spec, known_ids, errors, warnings)
    return bytes(blob), count



def cpp_struct_name(table):
    return 'Krosspop' + ''.join(part.capitalize() for part in table.split('_')) + 'Record'


def write_header(schema):
    os.makedirs(os.path.dirname(HEADER_PATH), exist_ok=True)
    lines = [
        '#pragma once',
        '',
        '// GENERATED by scripts/krosspop_build_db.py from',
        '// krosspop/schema/tables.json — do not edit.',
        '',
        '#include <cstdint>',
        '',
        '#pragma pack(push, 1)',
        '',
        '// Prefix of every .data.bin. Record N follows at',
        '// sizeof(KrosspopDbHeader) + N * recordSize.',
        'struct KrosspopDbHeader {',
    ]
    for field in schema['header']['fields']:
        lines.append(f'  {CPP_TYPE[field["type"]]} {field["name"]};')
    lines += ['};', '']

    for spec in schema['tables']:
        name = cpp_struct_name(spec['table'])
        lines.append(f'struct {name} {{')
        for field in spec['fields']:
            comment = f'  // {field["comment"]}' if 'comment' in field else ''
            char_match = CHAR_RE.match(field['type'])
            if char_match:
                lines.append(f'  char {field["name"]}[{char_match.group(1)}];{comment}')
            else:
                lines.append(f'  {CPP_TYPE[field["type"]]} {field["name"]};{comment}')
        lines += ['};', '']

    lines += ['#pragma pack(pop)', '']
    for field_list, name in [(schema['header']['fields'], 'KrosspopDbHeader')]:
        lines.append(f'static_assert(sizeof({name}) == {record_size(field_list)},'
                     f' "{name} must stay packed");')
    for spec in schema['tables']:
        name = cpp_struct_name(spec['table'])
        lines.append(f'static_assert(sizeof({name}) == {record_size(spec["fields"])},'
                     f' "{name} must stay packed");')
    lines.append('')

    for spec in schema['tables']:
        const = 'KROSSPOP_' + spec['table'].upper() + '_VERSION'
        lines.append(f'constexpr uint8_t {const} = {spec["version"]};')
    lines.append('')

    with open(HEADER_PATH, 'w', encoding='utf-8') as handle:
        handle.write('\n'.join(lines))
    return HEADER_PATH


# --- entry point ----------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--workbook', default=WORKBOOK_PATH)
    parser.add_argument('--header-only', action='store_true',
                        help='regenerate the C++ header only; no workbook needed')
    args = parser.parse_args()

    with open(SCHEMA_PATH, encoding='utf-8') as handle:
        schema = json.load(handle)

    print(f'header  -> {os.path.relpath(write_header(schema), PROJECT_DIR)}')
    if args.header_only:
        return 0

    if not os.path.isfile(args.workbook):
        print(f'\nNo workbook at {os.path.relpath(args.workbook, PROJECT_DIR)}.\n'
              f'See krosspop/README.md for the layout to create, or pass '
              f'--header-only to skip the data build.', file=sys.stderr)
        return 1

    sheets = read_workbook(args.workbook)
    errors, warnings = [], []

    for spec in schema['tables']:
        if spec['sheet'] not in sheets:
            errors.append(f'workbook has no sheet named {spec["sheet"]!r}')
    if errors:
        for message in errors:
            print(f'ERROR: {message}', file=sys.stderr)
        return 1

    for spec in schema['tables']:
        check_columns(spec, sheets[spec['sheet']], errors)
    if errors:
        for message in errors:
            print(f'ERROR: {message}', file=sys.stderr)
        return 1

    known_ids = {}
    for spec in schema['tables']:
        known_ids[spec['table']] = collect_ids(spec, sheets[spec['sheet']], errors)

    os.makedirs(BUILD_DIR, exist_ok=True)

    outputs = []
    for spec in schema['tables']:
        rows = sheets[spec['sheet']]
        blob, count = build_table(spec, rows, known_ids, errors, warnings)
        outputs.append((spec, blob, count, rows))

    for message in warnings:
        print(f'warning: {message}', file=sys.stderr)
    if errors:
        for message in errors:
            print(f'ERROR: {message}', file=sys.stderr)
        print(f'\n{len(errors)} error(s); nothing written.', file=sys.stderr)
        return 1

    for spec, blob, count, rows in outputs:
        bin_path = os.path.join(BUILD_DIR, f'{spec["table"]}.data.bin')
        with open(bin_path, 'wb') as handle:
            handle.write(blob)
        filled = sum(1 for row in rows if '__id__' in row)
        print(f'{spec["table"]:<14} {filled:>4} rows, {count:>4} slots, '
              f'{len(blob):>6} bytes -> {os.path.relpath(bin_path, PROJECT_DIR)}')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except BuildError as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        sys.exit(1)
