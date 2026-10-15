// Darwin varargs: 8-byte slots on arm64, 4-byte words on armv7 (64-bit values
// consume two words). The assembly entry points flatten the incoming arguments.
#pragma once

#include <cstdint>
#include <string>

#include "hle/common.h"

namespace hle {

struct AppleArgs {
    const uintptr_t* slots;
};

// `allow_objects` enables %@ (NSString/CFString formats).
std::string apple_format(const char* fmt, AppleArgs args, bool allow_objects);

}  // namespace hle
