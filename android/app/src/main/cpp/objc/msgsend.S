// objc_msgSend / objc_msgSendSuper2 for the Apple arm64 ABI.
// Both save every argument register (x0-x8, q0-q7), resolve the IMP in C,
// restore and tail-jump, so any method signature (incl. stack varargs and x8
// indirect results) passes through untouched.

#if defined(__aarch64__)
    .text

.macro SAVE_ARGS
    stp x29, x30, [sp, #-224]!
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    str x8, [sp, #80]
    stp q0, q1, [sp, #96]
    stp q2, q3, [sp, #128]
    stp q4, q5, [sp, #160]
    stp q6, q7, [sp, #192]
.endm

.macro RESTORE_ARGS
    ldp q6, q7, [sp, #192]
    ldp q4, q5, [sp, #160]
    ldp q2, q3, [sp, #128]
    ldp q0, q1, [sp, #96]
    ldr x8, [sp, #80]
    ldp x6, x7, [sp, #64]
    ldp x4, x5, [sp, #48]
    ldp x2, x3, [sp, #32]
    ldp x0, x1, [sp, #16]
    ldp x29, x30, [sp], #224
.endm

    .globl objc_msgSend
    .type objc_msgSend, %function
    .p2align 4
objc_msgSend:
    cbz x0, 1f
    SAVE_ARGS
    bl objc_lookup_for_object
    mov x16, x0
    RESTORE_ARGS
    br x16
1:  // nil receiver: return zeros in every return register
    mov x1, #0
    movi d0, #0
    movi d1, #0
    movi d2, #0
    movi d3, #0
    ret
    .size objc_msgSend, .-objc_msgSend

    .globl objc_msgSendSuper2
    .type objc_msgSendSuper2, %function
    .p2align 4
objc_msgSendSuper2:
    SAVE_ARGS
    bl objc_lookup_super2
    mov x16, x0
    RESTORE_ARGS
    ldr x0, [x0]        // receiver
    br x16
    .size objc_msgSendSuper2, .-objc_msgSendSuper2

    .globl objc_nil_method
    .type objc_nil_method, %function
    .p2align 4
objc_nil_method:
    mov x0, #0
    mov x1, #0
    movi d0, #0
    movi d1, #0
    movi d2, #0
    movi d3, #0
    ret
    .size objc_nil_method, .-objc_nil_method
#elif defined(__arm__)
    .syntax unified
    .thumb
    .text

.macro LOOKUP_SEND name, lookup, super=0, stret=0
    .globl \name
    .type \name, %function
    .p2align 2
    .thumb_func
\name:
.if \stret
    cbz r1, 1f
.else
    cbz r0, 1f
.endif
    push {r0-r3, r4, lr}
    mov r4, sp
    bic r12, r4, #7
    mov sp, r12
.if \stret
    mov r0, r1
    mov r1, r2
.endif
    bl \lookup
    mov r12, r0
    mov sp, r4
    pop {r0-r3, r4, lr}
.if \super
.if \stret
    ldr r1, [r1]
.else
    ldr r0, [r0]
.endif
.endif
    bx r12
1:
.if !\stret
    movs r0, #0
    movs r1, #0
.endif
    bx lr
    .size \name, .-\name
.endm

LOOKUP_SEND objc_msgSend, objc_lookup_for_object
LOOKUP_SEND objc_msgSendSuper2, objc_lookup_super2, 1
LOOKUP_SEND objc_msgSend_stret, objc_lookup_for_object, 0, 1
LOOKUP_SEND objc_msgSendSuper2_stret, objc_lookup_super2, 1, 1

    .globl objc_nil_method
    .type objc_nil_method, %function
    .thumb_func
objc_nil_method:
    movs r0, #0
    movs r1, #0
    bx lr
    .size objc_nil_method, .-objc_nil_method
#endif
    .section .note.GNU-stack,"",%progbits
