#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

# RUN: llvm-mc -triple=armv7-unknown-linux-gnueabi -filetype=obj %s -o %t.o
# RUN: ld.lld -Ttext=0x401000 %t.o -o %t
# RUN: %root/bin/revng quick analyze --analyses=parse-binary,detect-abi %t > %t.model
# RUN: %root/bin/revng quick artifact emit-cfg --analyses=parse-binary,detect-abi --yaml %t -- --debug-names > %t.cfg
# RUN: python3 %S/check-tail-calls.py %t.model %t.cfg
# RUN: %root/bin/revng quick artifact isolate --analyses=parse-binary,detect-abi --tar %t -- --debug-names | %root/bin/revng merge llvm | %root/bin/revng opt -S | FileCheck %s
# RUN: %root/bin/revng quick artifact render-svg-cfg --analyses=parse-binary,detect-abi --tar %t > %t.svg.tar

# CHECK-LABEL: define void @local_direct()
# CHECK: call void @local_callee()
# CHECK-NEXT: ret void
# CHECK-LABEL: define void @local_conditional()
# CHECK: call void @local_callee()
# CHECK-NEXT: ret void
# CHECK-LABEL: define void @local_bad_stack()
# CHECK-NOT: call void @local_callee()
# CHECK-LABEL: define void @local_indirect()
# CHECK: call void @function_dispatcher()
# CHECK-NEXT: ret void
# CHECK-LABEL: define void @local_ordinary()
# CHECK: call void @local_callee()
# CHECK-NOT: call void @local_callee()
# CHECK: ret void
# CHECK-LABEL: define void @local_unmarked()
# CHECK-NOT: call void @local_
# CHECK-LABEL: define void @local_recursive()
# CHECK: call void @local_recursive()
# CHECK-NEXT: ret void
# CHECK-LABEL: define void @local_noreturn_tail()
# CHECK: call void @local_noreturn_callee()
# CHECK-NEXT: unreachable
# CHECK-LABEL: define void @local_unknown_stack()
# CHECK-NOT: call void @local_callee()
# CHECK-LABEL: define void @local_restored_stack()
# CHECK: call void @local_callee()
# CHECK-NEXT: ret void

# CHECK-LABEL: define void @local_falls_into_next()
# CHECK-NOT: call void @local_next_function()
# CHECK: call void @local_callee()
# CHECK-NOT: call void @local_next_function()
# CHECK-LABEL: define void @local_next_function()

.syntax unified
.arm
.text
.globl _start, callee, direct, conditional, bad_stack, indirect, ordinary, unmarked, recursive
.type _start, %function
_start:
  bx lr
.size _start, .-_start
.org 0x20
.type callee, %function
callee:
  push {lr}
  mov lr, #0
  add r0, r0, #1
  pop {pc}
.size callee, .-callee
.org 0x40
.type direct, %function
direct:
  b callee
.size direct, .-direct
.org 0x60
.type conditional, %function
conditional:
  cmp r0, #0
  bne callee
  bx lr
.size conditional, .-conditional
.org 0x80
.type bad_stack, %function
bad_stack:
  sub sp, sp, #4
  b callee
.size bad_stack, .-bad_stack
.org 0xa0
.type indirect, %function
indirect:
  bx r0
.size indirect, .-indirect
.org 0xc0
.type ordinary, %function
ordinary:
  push {lr}
  bl callee
  add r0, r0, #1
  pop {pc}
.size ordinary, .-ordinary
.org 0xe0
.type unmarked, %function
unmarked:
  b local_label
  udf #0
local_label:
  mov r0, #0
  bx lr
.size unmarked, .-unmarked
.org 0x100
.type recursive, %function
recursive:
  cmp r0, #0
  bne recursive
  bx lr
.size recursive, .-recursive

.org 0x120
.globl noreturn_callee, noreturn_tail, unknown_stack, restored_stack
.type noreturn_callee, %function
noreturn_callee:
  nop
noreturn_loop:
  b noreturn_loop
.size noreturn_callee, .-noreturn_callee
.org 0x140
.type noreturn_tail, %function
noreturn_tail:
  b noreturn_callee
.size noreturn_tail, .-noreturn_tail
.org 0x160
.type unknown_stack, %function
unknown_stack:
  mov sp, r0
  b callee
.size unknown_stack, .-unknown_stack
.org 0x180
.type restored_stack, %function
restored_stack:
  sub sp, sp, #8
  add sp, sp, #8
  b callee
.size restored_stack, .-restored_stack

.org 0x1a0
.globl falls_into_next, next_function
.type falls_into_next, %function
falls_into_next:
  cmp r0, #0
  bne callee
.size falls_into_next, .-falls_into_next
.type next_function, %function
next_function:
  bx lr
.size next_function, .-next_function
