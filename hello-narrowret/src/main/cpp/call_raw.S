/*
 * Copyright (C) 2026 utzcoz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// uint64_t CallRaw(void* fn, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);
//
// Calls fn(a0, a1, a2, a3) and returns the callee's x0 exactly as the register
// holds it. C and C++ callers of a bool/char/short-returning function narrow the
// result themselves (clang tests `tst w0, #0xff`), so they cannot observe what a
// callee leaves in the rest of the register. Code from other compilers does:
// rustc tests a `jboolean` with `cbz w0`. This shim lets the sample inspect w0
// the same way.
    .text
    .p2align 2
    .globl CallRaw
    .type CallRaw, %function
CallRaw:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    mov     x16, x0
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    mov     x3, x4
    blr     x16
    ldp     x29, x30, [sp], #16
    ret
    .size CallRaw, . - CallRaw

    .section .note.GNU-stack, "", %progbits
