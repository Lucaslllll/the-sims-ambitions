#include "hle/abi.h"

#include <sys/mman.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include "hle/common.h"

namespace abi {
namespace {
struct State {
    std::recursive_mutex lock;
    std::map<void*, Signature> methods;
    std::map<std::vector<uintptr_t>, void*> thunks;
    uint32_t* next = nullptr;
    uint32_t* end = nullptr;
};
State& state() { static State value; return value; }

std::vector<unsigned> layout(const Signature& signature, bool darwin) {
    std::vector<unsigned> slots;
    unsigned slot = 0;
    for (auto argument : signature.arguments) {
        unsigned alignment = darwin ? 1 : argument.alignment;
        slot = (slot + alignment - 1) & ~(alignment - 1);
        for (unsigned i = 0; i < argument.words; ++i) slots.push_back(slot++);
    }
    return slots;
}
}  // namespace

void* bridge(void* target, const Signature& signature, bool to_darwin) {
#if defined(__arm__)
    if (!target || signature.variadic) return target;
    auto& s = state();
    std::lock_guard guard(s.lock);
    std::vector<uintptr_t> key{(uintptr_t)target, to_darwin};
    for (auto a : signature.arguments) { key.push_back(a.words); key.push_back(a.alignment); }
    if (auto found = s.thunks.find(key); found != s.thunks.end()) return found->second;
    auto source = layout(signature, !to_darwin), dest = layout(signature, to_darwin);
    unsigned stack_words = dest.empty() || dest.back() < 4 ? 0 : dest.back() - 3;
    unsigned frame = (stack_words * 4 + 7) & ~7u;
    if (frame > 248) { LOGE("ARM ABI bridge frame too large"); abort(); }
    std::vector<uint32_t> code{
        0xe92d421f,  // push {r0-r3,r4,r9,lr}; Darwin may clobber Android's r9
        0xe1a0400d,  // mov r4,sp (incoming save area)
        0xe3cdd007,  // bic sp,sp,#7
        0xe24dd000 | frame,
    };
    auto source_offset = [&](unsigned slot) { return slot < 4 ? slot * 4 : 28 + (slot - 4) * 4; };
    for (size_t i = 0; i < dest.size(); ++i) {
        if (dest[i] < 4) continue;
        code.push_back(0xe594c000 | source_offset(source[i]));  // ldr r12,[r4,#offset]
        code.push_back(0xe58dc000 | ((dest[i] - 4) * 4));       // str r12,[sp,#offset]
    }
    for (size_t i = 0; i < dest.size(); ++i)
        if (dest[i] < 4) code.push_back(0xe5940000 | (dest[i] << 12) | source_offset(source[i]));
    size_t literal_load = code.size();
    code.push_back(0);
    code.push_back(0xe12fff3c);  // blx r12
    code.push_back(0xe1a0d004);  // mov sp,r4
    code.push_back(0xe28dd010);  // add sp,sp,#16 (keep return r0/r1)
    code.push_back(0xe8bd8210);  // pop {r4,r9,pc}
    code[literal_load] = 0xe59fc000 | ((code.size() - literal_load - 2) * 4);
    code.push_back((uintptr_t)target);
    if (!s.next || size_t(s.end - s.next) < code.size()) {
        constexpr size_t arena_size = 64 * 1024;
        void* arena = mmap(nullptr, arena_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (arena == MAP_FAILED) { LOGE("ARM ABI arena allocation failed"); abort(); }
        s.next = (uint32_t*)arena;
        s.end = s.next + arena_size / 4;
    }
    void* result = s.next;
    memcpy(s.next, code.data(), code.size() * 4);
    s.next += code.size();
    __builtin___clear_cache((char*)result, (char*)s.next);
    s.thunks.emplace(std::move(key), result);
    return result;
#else
    return target;
#endif
}

void register_method(void* target, const Signature& signature) {
#if defined(__arm__)
    auto& s = state();
    std::lock_guard guard(s.lock);
    s.methods[target] = signature;
#endif
}

void* method_to_game(void* target) {
#if defined(__arm__)
    auto& s = state();
    std::lock_guard guard(s.lock);
    auto found = s.methods.find(target);
    if (found == s.methods.end()) { LOGE("ARM method lacks ABI signature: %p", target); abort(); }
    return bridge(target, found->second, false);
#else
    return target;
#endif
}
}  // namespace abi
