#!/usr/bin/env python3
"""Dump Objective-C 2 metadata (classes, methods, ivars, protocols, selrefs,
classrefs) from a thin armv7/arm64 Mach-O. LIEF cannot parse ObjC for this binary,
so this walks the raw structures directly.

usage: macho_objc.py <thin-macho> [--json out.json]
"""
import json
import struct
import sys

import lief


class Image:
    def __init__(self, path):
        self.raw = open(path, 'rb').read()
        self.b = lief.MachO.parse(path).at(0)
        self.ps = 8 if struct.unpack_from('<I', self.raw)[0] == 0xfeedfacf else 4
        self.segs = [(s.virtual_address, s.virtual_size, s.file_offset, s.file_size)
                     for s in self.b.segments]
        self.binds = {}
        for bi in self.b.bindings:
            if bi.has_symbol:
                self.binds[bi.address] = bi.symbol.name

    def off(self, va):
        for v, vs, fo, fs in self.segs:
            if v <= va < v + vs:
                if va - v >= fs:
                    return None
                return fo + (va - v)
        return None

    def u64(self, va):
        o = self.off(va)
        return struct.unpack_from('<Q' if self.ps == 8 else '<I', self.raw, o)[0] if o is not None else 0

    def u32(self, va):
        o = self.off(va)
        return struct.unpack_from('<I', self.raw, o)[0] if o is not None else 0

    def cstr(self, va):
        o = self.off(va)
        if o is None:
            return None
        e = self.raw.index(b'\0', o)
        return self.raw[o:e].decode('utf-8', 'replace')

    def section(self, name):
        for s in self.b.sections:
            if s.name == name:
                return s
        return None

    def ptrs(self, name):
        s = self.section(name)
        if not s:
            return []
        return [(s.virtual_address + i, self.u64(s.virtual_address + i))
                for i in range(0, s.size, self.ps)]

    def ptr_or_bind(self, va):
        if va in self.binds:
            return self.binds[va]
        p = self.u64(va)
        return p


def method_list(img, va):
    if not va:
        return []
    entsize_flags, count = struct.unpack_from('<II', img.raw, img.off(va))
    entsize = entsize_flags & ~3
    out = []
    for i in range(count):
        e = va + 8 + i * entsize
        name = img.cstr(img.u64(e))
        types = img.cstr(img.u64(e + img.ps))
        imp = img.u64(e + 2 * img.ps)
        out.append({'name': name, 'types': types, 'imp': hex(imp)})
    return out


def ivar_list(img, va):
    if not va:
        return []
    entsize, count = struct.unpack_from('<II', img.raw, img.off(va))
    out = []
    for i in range(count):
        e = va + 8 + i * entsize
        offp = img.u64(e)
        out.append({'name': img.cstr(img.u64(e + img.ps)), 'type': img.cstr(img.u64(e + 2 * img.ps)),
                    'offset': img.u32(offp) if offp else None,
                    'size': img.u32(e + 3 * img.ps + 4)})
    return out


def prop_list(img, va):
    if not va:
        return []
    entsize, count = struct.unpack_from('<II', img.raw, img.off(va))
    return [{'name': img.cstr(img.u64(va + 8 + i * entsize)),
             'attr': img.cstr(img.u64(va + 8 + i * entsize + img.ps))} for i in range(count)]


def proto_list(img, va):
    if not va:
        return []
    count = img.u64(va)
    out = []
    for i in range(count):
        p = img.u64(va + img.ps + i * img.ps)
        out.append(img.cstr(img.u64(p + img.ps)))
    return out


def class_ro(img, va):
    flags, istart, isize = struct.unpack_from('<III', img.raw, img.off(va))
    base = 16 if img.ps == 8 else 12
    return {
        'flags': flags, 'instanceStart': istart, 'instanceSize': isize,
        'name': img.cstr(img.u64(va + base + img.ps)),
        'methods': method_list(img, img.u64(va + base + 2 * img.ps)),
        'protocols': proto_list(img, img.u64(va + base + 3 * img.ps)),
        'ivars': ivar_list(img, img.u64(va + base + 4 * img.ps)),
        'properties': prop_list(img, img.u64(va + base + 6 * img.ps)),
    }


def parse_class(img, va):
    isa = img.ptr_or_bind(va)
    sup = img.ptr_or_bind(va + img.ps)
    mask = ~(img.ps - 1)
    data = img.u64(va + 4 * img.ps) & mask
    ro = class_ro(img, data)
    meta_va = isa if isinstance(isa, int) else None
    meta = class_ro(img, img.u64(meta_va + 4 * img.ps) & mask) if meta_va else None
    if isinstance(sup, int):
        sup = class_ro(img, img.u64(sup + 4 * img.ps) & mask)['name'] if sup else None
    return {'addr': hex(va), 'name': ro['name'], 'super': sup, 'ro': ro,
            'class_methods': meta['methods'] if meta else []}


def main():
    img = Image(sys.argv[1])
    out = {'classes': [], 'categories': [], 'selrefs': [], 'classrefs': [], 'superrefs': []}
    for _, c in img.ptrs('__objc_classlist'):
        out['classes'].append(parse_class(img, c))
    for _, c in img.ptrs('__objc_catlist'):
        cls = img.ptr_or_bind(c + img.ps)
        if isinstance(cls, int):
            cls = parse_class(img, cls)['name']
        out['categories'].append({
            'name': img.cstr(img.u64(c)), 'class': cls,
            'instance_methods': method_list(img, img.u64(c + 2 * img.ps)),
            'class_methods': method_list(img, img.u64(c + 3 * img.ps)),
        })
    for va, p in img.ptrs('__objc_selrefs'):
        out['selrefs'].append(img.cstr(p))
    s = img.section('__objc_classrefs')
    for i in range(0, s.size, img.ps):
        r = img.ptr_or_bind(s.virtual_address + i)
        if isinstance(r, int):
            r = parse_class(img, r)['name']
        out['classrefs'].append(r)

    for c in out['classes']:
        print(f"@interface {c['name']} : {c['super']}  // size={c['ro']['instanceSize']} protos={c['ro']['protocols']}")
        for iv in c['ro']['ivars']:
            print(f"    ivar {iv['type']:30} {iv['name']} @{iv['offset']}")
        for p in c['ro']['properties']:
            print(f"    @property {p['name']} [{p['attr']}]")
        for m in c['class_methods']:
            print(f"    + {m['name']:50} {m['types']}  {m['imp']}")
        for m in c['ro']['methods']:
            print(f"    - {m['name']:50} {m['types']}  {m['imp']}")
    for c in out['categories']:
        print(f"@category {c['class']} ({c['name']})")
        for m in c['class_methods']:
            print(f"    + {m['name']:50} {m['types']}  {m['imp']}")
        for m in c['instance_methods']:
            print(f"    - {m['name']:50} {m['types']}  {m['imp']}")
    print('\n// classrefs:', sorted(set(map(str, out['classrefs']))))
    print('\n// selrefs (%d):' % len(out['selrefs']))
    print(' '.join(sorted(out['selrefs'])))
    if '--json' in sys.argv:
        json.dump(out, open(sys.argv[sys.argv.index('--json') + 1], 'w'), indent=1)


if __name__ == '__main__':
    main()
