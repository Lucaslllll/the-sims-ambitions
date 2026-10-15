    .syntax unified
    .thumb
    .text
    .globl invoke_darwin_mix
    .type invoke_darwin_mix,%function
    .thumb_func
invoke_darwin_mix:
    push {r4,lr}
    mov r4,r0
    sub sp,sp,#20  // deliberately only 4-byte aligned at the Darwin boundary
    movs r0,#4
    str r0,[sp,#0]
    movw r0,#0x7788
    movt r0,#0x5566
    str r0,[sp,#4]
    movw r0,#0x3344
    movt r0,#0x1122
    str r0,[sp,#8]
    movs r0,#5
    str r0,[sp,#12]
    movs r0,#1
    movs r1,#0
    movw r2,#0
    movt r2,#0x4004  // double 2.5 in r1/r2 (Android expects r2/r3)
    movs r3,#3
    blx r4
    add sp,sp,#20
    pop {r4,pc}

    .globl darwin_mix
    .type darwin_mix,%function
    .thumb_func
darwin_mix:
    cmp r0,#1
    bne 1f
    cmp r1,#0
    bne 1f
    movw r12,#0
    movt r12,#0x4004
    cmp r2,r12
    bne 1f
    cmp r3,#3
    bne 1f
    ldr r0,[sp,#0]
    cmp r0,#4
    bne 1f
    ldr r0,[sp,#12]
    cmp r0,#5
    bne 1f
    ldr r0,[sp,#4]
    ldr r1,[sp,#8]
    adds r0,r0,#38
    movs r2,#0
    adcs r1,r1,r2
    bx lr
1:
    movs r0,#0
    movs r1,#0
    bx lr

    .globl darwin_clobber_r9
    .type darwin_clobber_r9,%function
    .thumb_func
darwin_clobber_r9:
    movw r9,#0xbeef
    movt r9,#0xdead
    bx lr

    .globl check_r9
    .type check_r9,%function
    .thumb_func
check_r9:
    push {r4,r9,lr}
    sub sp,sp,#4
    mov r4,r0
    movw r9,#0x5678
    movt r9,#0x1234
    blx r4
    movw r1,#0x5678
    movt r1,#0x1234
    cmp r9,r1
    ite eq
    moveq r0,#1
    movne r0,#0
    add sp,sp,#4
    pop {r4,r9,pc}
    .section .note.GNU-stack,"",%progbits
