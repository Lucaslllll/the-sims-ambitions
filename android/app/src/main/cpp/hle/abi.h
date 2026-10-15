// ARMv7 Darwin packs 64-bit arguments on 4-byte boundaries; Android uses 8.
#pragma once

#include <type_traits>
#include <vector>
#include <cstddef>

namespace abi {

struct Argument { unsigned words, alignment; };
struct Signature {
    std::vector<Argument> arguments;
    bool variadic = false;
};

void* bridge(void* target, const Signature& signature, bool to_darwin);
void register_method(void* target, const Signature& signature);
void* method_to_game(void* target);

template <typename R, typename... A>
Signature signature_of(R(*)(A...)) {
    Signature result;
    if constexpr (std::is_class_v<R>) {
        if constexpr (sizeof(R) > 4) result.arguments.push_back({1, 1});
    }
    (result.arguments.push_back({unsigned((sizeof(A) + 3) / 4), unsigned(alignof(A) > 4 ? 2 : 1)}), ...);
    return result;
}

template <typename R, typename... A>
Signature signature_of(R(*)(A..., ...)) { return {{}, true}; }

template <typename T>
void* to_game(T target) {
#if defined(__arm__)
    if constexpr (std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>)
        return bridge((void*)target, signature_of(target), false);
#endif
    return (void*)target;
}

template <typename R, typename... A>
R call_game(R(*target)(A...), A... args) {
#if defined(__arm__)
    static thread_local void* last_target = nullptr;
    static thread_local void* adapted = nullptr;
    if (last_target != (void*)target) {
        adapted = bridge((void*)target, signature_of(target), true);
        last_target = (void*)target;
    }
    return ((R(*)(A...))adapted)(args...);
#else
    return target(args...);
#endif
}

template <typename T>
T native_callback(T target) {
#if defined(__arm__)
    return (T)bridge((void*)target, signature_of(target), true);
#else
    return target;
#endif
}

}  // namespace abi
