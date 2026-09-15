#!/usr/bin/env python3
"""Decode vectors: WHATWG/browser semantics (encoding.mjs is ground truth)."""
import struct

cases = []
def add(name, gbk, blob, expect):
    cases.append((name, gbk, blob, expect))

def t(name, gbk, text):
    add(name, gbk, text.encode('gbk' if gbk else 'cp932'), [ord(c) for c in text])

t('ascii932', False, 'Hello TOUHOU 10: ZUN!')
t('ascii936', True, 'Hello TOUHOU 10: ZUN!')
t('hira', False, 'あいうえお少女祈祷中')
t('kata', False, 'アイリス東方風神録')
t('kanji', False, '博麗霊夢霧雨魔理沙上海アリス幻樂団')
t('fullwidth', False, 'ＭＳ ゴシック１０ｔｈ')
t('halfwidth', False, 'ｱｲﾘｽ123')
# WHATWG shift_jis lore: 0x5C -> U+00A5, 0x7E -> U+203E (browser, not CPython).
add('yen932', False, b'A\x5cB\x7eC', [0x41, 0xA5, 0x42, 0x203E, 0x43])
t('zh_title', True, '东方风神录少女祈祷中')
t('zh_names', True, '博丽灵梦雾雨魔理沙上海爱丽丝幻乐团')
t('zh_mixed', True, '第1面：妖怪之山的山麓 100%★☆')
t('symbols', False, '★☆♪～…「」、。『』（）！！？？')
t('symbols936', True, '★☆～…「」、。『』（）！！？？')

# Malformed cases, hand-verified against encoding.mjs fallback code.
add('bad932a', False, b'\x82', [0x30FB])
add('bad932b', False, b'\x82 A', [0x30FB, 0x41])
add('bad932c', False, b'\x82\x82\xa0', [0xFF42, 0xF8F0])
add('bad932d', False, b'\x82\x00A', [0x30FB, 0x0000, 0x41])
add('bad932e', False, b'Z\xfd\xfe\xff', [0x5A, 0xF8F1, 0xF8F2, 0xF8F3])
add('bad932f', False, b'\x7f', [0x7F])
add('bad936a', True, b'\xd6', [0x3F])
add('bad936b', True, b'\xd6 A', [0x3F, 0x41])
add('bad936c', True, b'A\x80\xff', [0x41, 0x20AC, 0x3F])

# Exhaustive valid double-byte pairs vs CPython (identical mappings).
for codec, gbk, name in (('cp932', False, 'all932'), ('gbk', True, 'all936')):
    blob = bytearray()
    expect = []
    for hi in range(256):
        for lo in range(256):
            b = bytes([hi, lo])
            try:
                s = b.decode(codec)
            except Exception:
                continue
            if len(s) == 1 and ord(s) < 0x10000:
                if gbk and not (0x81 <= hi <= 0xFE):
                    continue
                if not gbk and not (0x81 <= hi <= 0x9F or 0xE0 <= hi <= 0xFC):
                    continue
                blob += b
                expect.append(ord(s))
    cases.append((name, gbk, bytes(blob), expect))

# Per-byte singles, each alone (two-pass fallback is buffer-global).
for b in range(256):
    if 0x81 <= b <= 0x9F or 0xE0 <= b <= 0xFC:
        continue  # leads covered by dangling test below
    if b < 0x80:
        e = 0xA5 if b == 0x5C else 0x203E if b == 0x7E else b
    elif 0xA1 <= b <= 0xDF:
        e = 0xFF61 + b - 0xA1
    elif b == 0x80:
        e = 0x0080  # fast FFFD -> fallback charCode
    elif b == 0xA0:
        e = 0xF8F0
    else:
        e = 0xF8F1 + b - 0xFD
    add(f's932_{b:02X}', False, bytes([b]), [e])
for b in range(0x81, 0xFD):
    pass
for b in list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD)):
    add(f's932dang_{b:02X}', False, bytes([b]), [0x30FB])
for b in range(256):
    if 0x81 <= b <= 0xFE:
        add(f's936dang_{b:02X}', True, bytes([b]), [0x3F])
    elif b < 0x80:
        add(f's936_{b:02X}', True, bytes([b]), [b])
    elif b == 0x80:
        add('s936_80', True, bytes([b]), [0x20AC])
    else:
        add('s936_FF', True, bytes([b]), [0x3F])

with open('tests/vectors.bin', 'wb') as f:
    f.write(struct.pack('<Ixxxx', len(cases)))
    for name, gbk, blob, expect in cases:
        nb = name.encode()
        f.write(bytes([1 if gbk else 0, len(nb)]) + struct.pack('<H', len(blob)) + struct.pack('<I', len(expect)))
        f.write(nb)
        f.write(blob)
        for u in expect:
            f.write(struct.pack('<H', u))
print(f"wrote {len(cases)} cases")
