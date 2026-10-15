// Common handler for unimplemented imported functions.
// Entered from a per-symbol stub with x17 = symbol name. Preserves argument
// registers around the report call, then returns 0 to the game.
#if defined(__arm__)
    .syntax unified
    .arm
#endif
    .text
    .globl ts3a_unimplemented_stub
    .type ts3a_unimplemented_stub, %function
ts3a_unimplemented_stub:
#if defined(__aarch64__)
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x0, x17
    bl ts3a_unimplemented_report
    movi d0, #0
    movi d1, #0
    mov x1, #0
    ldp x29, x30, [sp], #16
    ret
#else
    // The generated stub enters in ARM state with r12 = symbol name.
    push {r4, lr}
    mov r4, sp
    bic sp, sp, #7
    mov r0, r12
    bl ts3a_unimplemented_report
    mov sp, r4
    mov r0, #0
    mov r1, #0
    pop {r4, pc}
#endif
    .size ts3a_unimplemented_stub, .-ts3a_unimplemented_stub
