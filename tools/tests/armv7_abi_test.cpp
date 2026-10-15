// Runs on Android/ARM32; the assembly fixtures intentionally use Darwin's ABI.
#include "hle/abi.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

extern "C" int64_t invoke_darwin_mix(void*);
extern "C" int64_t darwin_mix(int, double, int, int, int64_t, int);
extern "C" void darwin_clobber_r9();
extern "C" int check_r9(void(*)());

static int64_t native_mix(int a, double b, int c, int d, int64_t e, int f) {
    assert(a == 1 && b == 2.5 && c == 3 && d == 4 && f == 5);
    return e + a + int(b * 10) + c + d + f;
}
static int hits = 0;
static void bump() { ++hits; }

int main() {
    static_assert(sizeof(void*) == 4);
    constexpr int64_t value = 0x1122334455667788LL;
    assert(invoke_darwin_mix(abi::to_game(native_mix)) == value + 38);
    assert(abi::native_callback(darwin_mix)(1, 2.5, 3, 4, value, 5) == value + 38);
    assert(check_r9(abi::native_callback(darwin_clobber_r9)) == 1);
    abi::call_game(darwin_clobber_r9);
    abi::call_game(bump);
    abi::call_game(bump);
    assert(hits == 2);
    puts("ARMv7 ABI tests passed: arguments, split 64-bit stack values, stack alignment, r9.");
}
