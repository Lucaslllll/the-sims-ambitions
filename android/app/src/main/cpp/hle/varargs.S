// Darwin varargs: arm64 stack slots; armv7 contiguous register/stack words.
    .text

#if defined(__arm__)
    .syntax unified
    .thumb
#endif

.macro APPLE_VARIADIC name, impl, fixed
    .globl \name
    .type \name, %function
    .p2align 2
#if defined(__arm__)
    .thumb_func
#endif
\name:
#if defined(__aarch64__)
    mov x\fixed, sp
    b \impl
#else
    push {r0-r3}             // contiguous with the incoming stack arguments
    push {r4,lr}
    mov r4,sp
    bic r12,r4,#7
    mov sp,r12
    add r\fixed,r4,#(8 + \fixed * 4)
    bl \impl
    mov sp,r4
    pop {r4,lr}
    add sp,sp,#16
    bx lr
#endif
    .size \name, .-\name
.endm

APPLE_VARIADIC hle_printf, hle_printf_impl, 1
APPLE_VARIADIC hle_snprintf, hle_snprintf_impl, 3
APPLE_VARIADIC hle_sprintf, hle_sprintf_impl, 2
APPLE_VARIADIC hle_fprintf, hle_fprintf_impl, 2
APPLE_VARIADIC hle_sscanf, hle_sscanf_impl, 2
APPLE_VARIADIC hle_open, hle_open_impl, 2
APPLE_VARIADIC hle_NSLog, hle_NSLog_impl, 1
APPLE_VARIADIC hle_CFStringCreateWithFormat, hle_CFStringCreateWithFormat_impl, 3
APPLE_VARIADIC hle_CFStringAppendFormat, hle_CFStringAppendFormat_impl, 3
APPLE_VARIADIC hle_NSString_stringWithFormat, hle_NSString_stringWithFormat_impl, 3
APPLE_VARIADIC hle_NSString_initWithFormat, hle_NSString_initWithFormat_impl, 3
APPLE_VARIADIC hle_NSMutableString_appendFormat, hle_NSMutableString_appendFormat_impl, 3
APPLE_VARIADIC hle_NSDictionary_dictionaryWithObjectsAndKeys, hle_NSDictionary_dictionaryWithObjectsAndKeys_impl, 3
APPLE_VARIADIC hle_NSArray_arrayWithObjects, hle_NSArray_arrayWithObjects_impl, 3
    .section .note.GNU-stack,"",%progbits
