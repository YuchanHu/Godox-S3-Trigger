"""Build QiuYe 16px descriptors and native Spleen data glyphs, without scaling BDF pixels."""
from pathlib import Path
import argparse
import hashlib
import math
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def load_bdf(path, selected):
    lines = path.read_text(encoding='utf-8').splitlines()
    glyphs = {}
    for index, line in enumerate(lines):
        if not line.startswith('STARTCHAR '):
            continue
        props = {}
        j = index + 1
        while lines[j] != 'BITMAP':
            key, _, value = lines[j].partition(' ')
            props[key] = value
            j += 1
        cp = int(props['ENCODING'])
        if cp < 0 or chr(cp) not in selected:
            continue
        width, height, xoff, yoff = map(int, props['BBX'].split())
        assert xoff == 0
        advance = int(props['DWIDTH'].split()[0])
        image = Image.new('1', (width, height))
        bits = ((width + 7) // 8) * 8
        for y in range(height):
            row = int(lines[j + 1 + y], 16)
            for x in range(width):
                image.putpixel((x, y), bool(row & (1 << (bits - 1 - x))))
        glyphs[chr(cp)] = (image, advance)
    assert set(selected) == set(glyphs), (path, set(selected) - set(glyphs))
    return glyphs


def generate(font_path):
    source = (ROOT/'components/stage_ui/stage_view.c').read_text(encoding='utf-8')
    ascii_chars = {chr(i) for i in range(32, 127)}
    chars = sorted(ascii_chars | {c for c in source if ord(c) > 127})
    font = ImageFont.truetype(str(font_path), 16)
    descriptors = {}
    for char in chars:
        width = max(1, math.ceil(font.getlength(char)))
        gray = Image.new('L', (width, 20))
        ImageDraw.Draw(gray).text((0, 16), char, font=font, fill=255, anchor='ls')
        descriptors[char] = (gray.point(lambda v: 255 if v >= 128 else 0, mode='1'), width)
    tables = [(0, 16, descriptors)]
    for size, width in [(16, 8), (24, 12), (32, 16)]:
        path = ROOT/f'assets/fonts/spleen/spleen-{width}x{size}.bdf'
        tables.append((1, size, load_bdf(path, ascii_chars)))
    out = ('/* Generated: QiuYeYuanTi-16.ttf (OFL-1.1), Spleen (BSD-2-Clause).\n'
           ' * Licenses: assets/QiuYeYuanTi-16.LICENSE.txt, assets/fonts/spleen/LICENSE. */\n'
           '#pragma once\n#include <stdint.h>\n'
           'enum { UI_FONT_QIU, UI_FONT_SPLEEN };\n'
           'typedef struct {uint32_t cp,offset;uint8_t w,h,advance,ix,iy,iw,ih;} ui_glyph_t;\n'
           'typedef struct {int family,size;unsigned count;const ui_glyph_t *glyphs;const uint8_t *bits;} ui_font_t;\n')
    fonts = []
    for family, size, glyphs in tables:
        data = bytearray()
        records = []
        for char, (image, advance) in sorted(glyphs.items()):
            ink = image.getbbox()
            ix, iy, iw, ih = (ink[0], ink[1], ink[2]-ink[0], ink[3]-ink[1]) if ink else (0, 0, 0, 0)
            records.append(f'{{{ord(char)},{len(data)},{image.width},{image.height},{advance},{ix},{iy},{iw},{ih}}}')
            pixels = list(image.getdata())
            packed = bytearray((len(pixels) + 7) // 8)
            for index, bit in enumerate(pixels):
                if bit:
                    packed[index // 8] |= 1 << (7 - index % 8)
            data.extend(packed)
        name = f'{family}_{size}'
        out += f'static const ui_glyph_t ui_glyphs_{name}[]={{' + ','.join(records) + '};\n'
        out += f'static const uint8_t ui_bits_{name}[]={{' + ','.join(map(str, data)) + '};\n'
        fonts.append(f'{{{family},{size},{len(records)},ui_glyphs_{name},ui_bits_{name}}}')
    return out + 'static const ui_font_t ui_fonts[]={' + ','.join(fonts) + '};\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--font', type=Path, default=ROOT/'assets/QiuYeYuanTi-16.ttf')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    result = generate(args.font)
    target = ROOT/'components/stage_ui/ui_font.h'
    if args.check:
        assert target.read_text(encoding='utf-8') == result, 'Font atlas needs regeneration'
    else:
        target.write_text(result, encoding='utf-8')
    print('Font atlas', 'verified' if args.check else 'generated', len(result), 'bytes of C text;',
          'QiuYe source SHA256', hashlib.sha256(args.font.read_bytes()).hexdigest())
