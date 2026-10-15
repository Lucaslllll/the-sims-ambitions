// Minimal dyld: maps a native armv7/arm64 Mach-O MH_EXECUTE and applies classic
// LC_DYLD_INFO rebase/bind opcodes.
#include "loader/macho.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "hle/common.h"
#include "loader/symbols.h"
#include "objc/runtime.h"

MachOImage* g_game_image = nullptr;

namespace {

// --- Mach-O structures (subset of <mach-o/loader.h>) ------------------------
constexpr uint32_t FAT_MAGIC = 0xcafebabe;
constexpr uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr int32_t CPU_TYPE_ARM64 = 0x0100000c;
#if defined(__arm__)
constexpr uint32_t NATIVE_MAGIC = 0xfeedface;
constexpr int32_t NATIVE_CPU = 12;
constexpr uint32_t NATIVE_SEGMENT = 1;
using MachOWord = uint32_t;
#else
constexpr uint32_t NATIVE_MAGIC = MH_MAGIC_64;
constexpr int32_t NATIVE_CPU = CPU_TYPE_ARM64;
constexpr uint32_t NATIVE_SEGMENT = 0x19;
using MachOWord = uint64_t;
#endif
constexpr size_t kPointerSize = sizeof(uintptr_t);

constexpr uint32_t LC_SEGMENT_64 = 0x19;
constexpr uint32_t LC_DYLD_INFO = 0x22;
constexpr uint32_t LC_DYLD_INFO_ONLY = 0x80000022;
constexpr uint32_t LC_MAIN = 0x80000028;
constexpr uint32_t LC_LOAD_DYLIB = 0xc;
constexpr uint32_t LC_LOAD_WEAK_DYLIB = 0x80000018;

struct mach_header_64 {
    uint32_t magic;
    int32_t cputype;
    int32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
#if !defined(__arm__)
    uint32_t reserved;
#endif
};
struct load_command {
    uint32_t cmd, cmdsize;
};
struct segment_command_64 {
    uint32_t cmd, cmdsize;
    char segname[16];
    MachOWord vmaddr, vmsize, fileoff, filesize;
    int32_t maxprot, initprot;
    uint32_t nsects, flags;
};
struct section_64 {
    char sectname[16];
    char segname[16];
    MachOWord addr, size;
    uint32_t offset, align, reloff, nreloc, flags, reserved1, reserved2;
#if !defined(__arm__)
    uint32_t reserved3;
#endif
};
struct dyld_info_command {
    uint32_t cmd, cmdsize;
    uint32_t rebase_off, rebase_size;
    uint32_t bind_off, bind_size;
    uint32_t weak_bind_off, weak_bind_size;
    uint32_t lazy_bind_off, lazy_bind_size;
    uint32_t export_off, export_size;
};
struct entry_point_command {
    uint32_t cmd, cmdsize;
    uint64_t entryoff, stacksize;
};
struct dylib_command {
    uint32_t cmd, cmdsize;
    uint32_t name_offset;
    uint32_t timestamp, current_version, compat_version;
};

uint64_t read_uleb(const uint8_t*& p, const uint8_t* end) {
    uint64_t result = 0;
    int bit = 0;
    while (p < end) {
        uint8_t b = *p++;
        result |= (uint64_t)(b & 0x7f) << bit;
        bit += 7;
        if (!(b & 0x80)) break;
    }
    return result;
}

int64_t read_sleb(const uint8_t*& p, const uint8_t* end) {
    int64_t result = 0;
    int bit = 0;
    uint8_t b = 0;
    while (p < end) {
        b = *p++;
        result |= (int64_t)(b & 0x7f) << bit;
        bit += 7;
        if (!(b & 0x80)) break;
    }
    if (bit < 64 && (b & 0x40)) result |= -((int64_t)1 << bit);
    return result;
}

std::string short_dylib(const std::string& path) {
    auto s = path.rfind('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

struct Loader {
    MachOImage& img;
    const uint8_t* file;  // start of the native thin image
    size_t file_size;
    const dyld_info_command* dyld_info = nullptr;
    std::vector<std::string> dylibs;
    int unresolved = 0;

    uint64_t seg_addr(int idx) const { return img.segments.at(idx).vmaddr; }

    bool in_section(uint64_t addr, const char* sect) const {
        for (auto& s : img.sections)
            if (s.sectname == sect && addr >= s.addr && addr < s.addr + s.size) return true;
        return false;
    }

    void* resolve(const char* name, uint64_t where, int ordinal, bool weak_import) {
        if (void* p = hle::find_symbol(name)) return p;
        if (void* p = objc::resolve_class_symbol(name)) return p;
        bool is_function = in_section(where, "__la_symbol_ptr") || in_section(where, "__got");
        const char* lib = (ordinal > 0 && ordinal <= (int)dylibs.size())
                              ? dylibs[ordinal - 1].c_str() : "?";
        unresolved++;
        if (is_function && !in_section(where, "__got")) {
            return make_unimplemented_stub(name);
        }
        if (in_section(where, "__got")) {
            // Could be a function address or a data symbol; a stub is the
            // safer choice for calls, but log loudly.
            LOGW("unresolved GOT import %s (%s) -> stub", name, lib);
            return make_unimplemented_stub(name);
        }
        LOGW("unresolved DATA import %s (%s) at %#lx -> zero block", name, lib, (unsigned long)where);
        return calloc(1, 512);
    }

    void do_bind(uint64_t addr, const char* name, int64_t addend, int ordinal, uint8_t flags) {
        void* target = resolve(name, addr, ordinal, flags & 1 /*WEAK_IMPORT*/);
        *(uintptr_t*)addr = (uintptr_t)target + addend;
    }

    bool parse_commands() {
        auto* mh = (const mach_header_64*)file;
        if (file_size < sizeof(*mh) || mh->magic != NATIVE_MAGIC || mh->cputype != NATIVE_CPU) {
            LOGE("not a native Mach-O (magic %#x cpu %#x)", mh->magic, mh->cputype);
            return false;
        }
        if (NATIVE_CPU == 12 && (mh->cpusubtype & 0xffffff) != 9) return false;
        if (mh->sizeofcmds > file_size - sizeof(*mh)) return false;
        const uint8_t* p = file + sizeof(mach_header_64);
        const uint8_t* commands_end = p + mh->sizeofcmds;
        uint64_t entryoff = 0;
        for (uint32_t i = 0; i < mh->ncmds; i++) {
            auto* lc = (const load_command*)p;
            if (p + sizeof(*lc) > commands_end || lc->cmdsize < sizeof(*lc) ||
                lc->cmdsize > (size_t)(commands_end - p)) return false;
            switch (lc->cmd) {
                case NATIVE_SEGMENT: {
                    auto* seg = (const segment_command_64*)p;
                    if (lc->cmdsize < sizeof(*seg) || seg->nsects >
                        (lc->cmdsize - sizeof(*seg)) / sizeof(section_64)) return false;
                    if (seg->fileoff > file_size || seg->filesize > file_size - seg->fileoff ||
                        seg->filesize > seg->vmsize) return false;
                    MachOSegment s;
                    s.name.assign(seg->segname, strnlen(seg->segname, 16));
                    s.vmaddr = seg->vmaddr;
                    s.vmsize = seg->vmsize;
                    s.fileoff = seg->fileoff;
                    s.filesize = seg->filesize;
                    s.prot = seg->initprot;
                    img.segments.push_back(s);
                    auto* sect = (const section_64*)(seg + 1);
                    for (uint32_t k = 0; k < seg->nsects; k++) {
                        MachOSection ms;
                        ms.segname.assign(sect[k].segname, strnlen(sect[k].segname, 16));
                        ms.sectname.assign(sect[k].sectname, strnlen(sect[k].sectname, 16));
                        ms.addr = sect[k].addr;
                        ms.size = sect[k].size;
                        ms.flags = sect[k].flags;
                        img.sections.push_back(ms);
                    }
                    break;
                }
                case LC_DYLD_INFO:
                case LC_DYLD_INFO_ONLY:
                    dyld_info = (const dyld_info_command*)p;
                    break;
                case LC_MAIN:
                    entryoff = ((const entry_point_command*)p)->entryoff;
                    break;
                case LC_LOAD_DYLIB:
                case LC_LOAD_WEAK_DYLIB: {
                    auto* d = (const dylib_command*)p;
                    dylibs.push_back(short_dylib((const char*)p + d->name_offset));
                    break;
                }
                default:
                    break;
            }
            p += lc->cmdsize;
        }
        if (!dyld_info) {
            LOGE("no LC_DYLD_INFO (chained fixups not supported)");
            return false;
        }
        // __TEXT's file offset is 0, so entryoff is relative to __TEXT vmaddr.
        for (auto& s : img.segments)
            if (s.name == "__TEXT") img.entry = s.vmaddr + entryoff;
        return true;
    }

    bool map() {
        uint64_t lo = UINT64_MAX, hi = 0;
        for (auto& s : img.segments) {
            if (s.name == "__PAGEZERO" || s.vmsize == 0) continue;
            lo = std::min(lo, s.vmaddr);
            hi = std::max(hi, s.vmaddr + s.vmsize);
        }
        size_t size = hi - lo;
        void* base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED) {
            LOGE("mmap(%zu) failed", size);
            return false;
        }
        img.map_base = (uint8_t*)base;
        img.map_size = size;
        img.slide = (int64_t)(uint64_t)base - (int64_t)lo;
        for (auto& s : img.segments) {
            if (s.name == "__PAGEZERO") continue;
            uint8_t* dst = (uint8_t*)(s.vmaddr + img.slide);
            if (s.filesize) memcpy(dst, file + s.fileoff, s.filesize);
            s.vmaddr += img.slide;
        }
        for (auto& s : img.sections) s.addr += img.slide;
        img.entry += img.slide;
        LOGI("mapped image at %p (size %#zx, slide %#llx)", base, size, (long long)img.slide);
        return true;
    }

    void rebase() {
        const uint8_t* p = file + dyld_info->rebase_off;
        const uint8_t* end = p + dyld_info->rebase_size;
        uint64_t addr = 0;
        int count = 0;
        auto apply = [&]() {
            *(uintptr_t*)addr += img.slide;
            count++;
        };
        while (p < end) {
            uint8_t b = *p++;
            uint8_t op = b & 0xf0, imm = b & 0x0f;
            switch (op) {
                case 0x00: p = end; break;  // DONE
                case 0x10: break;           // SET_TYPE_IMM (always pointer)
                case 0x20:                  // SET_SEGMENT_AND_OFFSET_ULEB
                    addr = seg_addr(imm) + read_uleb(p, end);
                    break;
                case 0x30: addr += read_uleb(p, end); break;  // ADD_ADDR_ULEB
                case 0x40: addr += imm * kPointerSize; break;  // ADD_ADDR_IMM_SCALED
                case 0x50:                                    // DO_REBASE_IMM_TIMES
                    for (int i = 0; i < imm; i++) { apply(); addr += kPointerSize; }
                    break;
                case 0x60: {  // DO_REBASE_ULEB_TIMES
                    uint64_t n = read_uleb(p, end);
                    for (uint64_t i = 0; i < n; i++) { apply(); addr += kPointerSize; }
                    break;
                }
                case 0x70:  // DO_REBASE_ADD_ADDR_ULEB
                    apply();
                    addr += kPointerSize + read_uleb(p, end);
                    break;
                case 0x80: {  // DO_REBASE_ULEB_TIMES_SKIPPING_ULEB
                    uint64_t n = read_uleb(p, end), skip = read_uleb(p, end);
                    for (uint64_t i = 0; i < n; i++) { apply(); addr += kPointerSize + skip; }
                    break;
                }
                default:
                    LOGE("bad rebase opcode %#x", b);
                    p = end;
            }
        }
        LOGI("rebased %d pointers", count);
    }

    void bind(uint32_t off, uint32_t size, bool lazy) {
        const uint8_t* p = file + off;
        const uint8_t* end = p + size;
        uint64_t addr = 0;
        const char* sym = nullptr;
        int64_t addend = 0;
        int ordinal = 0;
        uint8_t flags = 0;
        int count = 0;
        while (p < end) {
            uint8_t b = *p++;
            uint8_t op = b & 0xf0, imm = b & 0x0f;
            switch (op) {
                case 0x00:  // DONE
                    if (!lazy) p = end;
                    break;
                case 0x10: ordinal = imm; break;
                case 0x20: ordinal = (int)read_uleb(p, end); break;
                case 0x30: ordinal = imm ? (int8_t)(0xf0 | imm) : 0; break;
                case 0x40:
                    flags = imm;
                    sym = (const char*)p;
                    p += strlen(sym) + 1;
                    break;
                case 0x50: break;  // SET_TYPE_IMM
                case 0x60: addend = read_sleb(p, end); break;
                case 0x70: addr = seg_addr(imm) + read_uleb(p, end); break;
                case 0x80: addr += read_uleb(p, end); break;
                case 0x90:
                    do_bind(addr, sym, addend, ordinal, flags);
                    addr += kPointerSize;
                    count++;
                    break;
                case 0xA0:
                    do_bind(addr, sym, addend, ordinal, flags);
                    addr += kPointerSize + read_uleb(p, end);
                    count++;
                    break;
                case 0xB0:
                    do_bind(addr, sym, addend, ordinal, flags);
                    addr += kPointerSize + imm * kPointerSize;
                    count++;
                    break;
                case 0xC0: {
                    uint64_t n = read_uleb(p, end), skip = read_uleb(p, end);
                    for (uint64_t i = 0; i < n; i++) {
                        do_bind(addr, sym, addend, ordinal, flags);
                        addr += kPointerSize + skip;
                        count++;
                    }
                    break;
                }
                default:
                    LOGE("bad bind opcode %#x", b);
                    p = end;
            }
        }
        LOGI("bound %d %s pointers", count, lazy ? "lazy" : "regular");
    }

    void weak_bind() {
        // Weak binding only coalesces C++ operator new/delete here; resolve the
        // symbols to our implementations when we provide them.
        const uint8_t* p = file + dyld_info->weak_bind_off;
        const uint8_t* end = p + dyld_info->weak_bind_size;
        uint64_t addr = 0;
        const char* sym = nullptr;
        int64_t addend = 0;
        while (p < end) {
            uint8_t b = *p++;
            uint8_t op = b & 0xf0, imm = b & 0x0f;
            switch (op) {
                case 0x00: p = end; break;
                case 0x40:
                    sym = (const char*)p;
                    p += strlen(sym) + 1;
                    break;
                case 0x50: break;
                case 0x60: addend = read_sleb(p, end); break;
                case 0x70: addr = seg_addr(imm) + read_uleb(p, end); break;
                case 0x80: addr += read_uleb(p, end); break;
                case 0x90:
                case 0xA0:
                case 0xB0: {
                    if (void* t = hle::find_symbol(sym)) *(uintptr_t*)addr = (uintptr_t)t + addend;
                    if (op == 0x90) addr += kPointerSize;
                    else if (op == 0xA0) addr += kPointerSize + read_uleb(p, end);
                    else addr += kPointerSize + imm * kPointerSize;
                    break;
                }
                case 0xC0: {
                    uint64_t n = read_uleb(p, end), skip = read_uleb(p, end);
                    for (uint64_t i = 0; i < n; i++) {
                        if (void* t = hle::find_symbol(sym)) *(uintptr_t*)addr = (uintptr_t)t + addend;
                        addr += kPointerSize + skip;
                    }
                    break;
                }
                default:
                    p = end;
            }
        }
    }

    void protect() {
        long page = sysconf(_SC_PAGESIZE);
        for (auto& s : img.segments) {
            if (s.name == "__PAGEZERO" || s.vmsize == 0) continue;
            int prot = 0;
            if (s.prot & 1) prot |= PROT_READ;
            if (s.prot & 2) prot |= PROT_WRITE;
            if (s.prot & 4) prot |= PROT_EXEC;
            if (s.name == "__LINKEDIT") prot = PROT_READ;
            uint64_t a = s.vmaddr & ~(uint64_t)(page - 1);
            uint64_t e = (s.vmaddr + s.vmsize + page - 1) & ~(uint64_t)(page - 1);
            if (mprotect((void*)a, e - a, prot) != 0) LOGE("mprotect %s failed", s.name.c_str());
            if (prot & PROT_EXEC) __builtin___clear_cache((char*)s.vmaddr, (char*)(s.vmaddr + s.vmsize));
        }
    }
};

}  // namespace

const MachOSection* MachOImage::section(const char* seg, const char* sect) const {
    for (auto& s : sections)
        if (s.segname == seg && s.sectname == sect) return &s;
    return nullptr;
}

bool macho_load(const std::string& path, MachOImage& img) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        LOGE("cannot open %s", path.c_str());
        return false;
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::unique_ptr<uint8_t[]> buf(new uint8_t[size]);
    if (fread(buf.get(), 1, size, f) != size) {
        fclose(f);
        return false;
    }
    fclose(f);

    const uint8_t* thin = buf.get();
    size_t thin_size = size;
    if (size < sizeof(mach_header_64)) return false;
    if (__builtin_bswap32(*(uint32_t*)thin) == FAT_MAGIC) {
        uint32_t n = __builtin_bswap32(((uint32_t*)thin)[1]);
        if (n > (size - 8) / 20) return false;
        const uint32_t* arch = (const uint32_t*)(thin + 8);
        bool found = false;
        for (uint32_t i = 0; i < n; i++, arch += 5) {
            if ((int32_t)__builtin_bswap32(arch[0]) == NATIVE_CPU &&
                (NATIVE_CPU != 12 || (__builtin_bswap32(arch[1]) & 0xffffff) == 9)) {
                uint32_t off = __builtin_bswap32(arch[2]);
                thin_size = __builtin_bswap32(arch[3]);
                if (off > size || thin_size > size - off || thin_size < sizeof(mach_header_64)) return false;
                thin = buf.get() + off;
                found = true;
                break;
            }
        }
        if (!found) {
            LOGE("fat binary has no native CPU %x slice", NATIVE_CPU);
            return false;
        }
    }

    Loader ld{img, thin, thin_size};
    if (!ld.parse_commands() || !ld.map()) return false;
    ld.rebase();
    // HLE framework classes already exist at this point, so _OBJC_CLASS_$_X
    // imports resolve to them; the image's own classes are realized later by
    // objc::load_image() once all superclass pointers are bound.
    ld.bind(ld.dyld_info->bind_off, ld.dyld_info->bind_size, false);
    ld.bind(ld.dyld_info->lazy_bind_off, ld.dyld_info->lazy_bind_size, true);
    ld.weak_bind();
    LOGI("%d imports unresolved (stubbed)", ld.unresolved);
    ld.protect();
    g_game_image = &img;
    return true;
}
