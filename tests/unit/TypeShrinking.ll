;
; This file is distributed under the MIT License. See LICENSE.md for details.
;

; CHECK checks arithmetic narrowing with InstCombine in the pipeline.
; CMP checks comparison narrowing with type-shrinking alone.

define i64 @sum32(i64 %0, i64 %1) {
  ; CHECK-LABEL: @sum32(
  %3 = add i64 %1, %0
  ; CHECK: add i32
  ; CHECK-NOT: and
  %4 = and i64 %3, 4294967295
  ret i64 %4
}

define i64 @shl32(i64 %0) {
  ; CHECK-LABEL: @shl32(
  %shl = shl i64 %0, 7
  ; CHECK: shl i32
  ; CHECK-NOT: and
  %masked = and i64 %shl, 4294967295
  ret i64 %masked
}

define i64 @ashr32(i64 %0) {
  ; CHECK-LABEL: @ashr32(
  %shl = shl i64 %0, 32
  ; CHECK: ashr i32 %{{.*}}, 1
  %ashr = ashr i64 %shl, 33
  %masked = and i64 %ashr, 4294967295
  ret i64 %masked
}

define i64 @lshr32(i64 %0) {
  ; CHECK-LABEL: @lshr32(
  %shl = shl i64 %0, 32
  ; CHECK: lshr i32 %{{.*}}, 1
  %ashr = lshr i64 %shl, 33
  %masked = and i64 %ashr, 4294967295
  ret i64 %masked
}

; A phi copies one of its incoming values bit for bit, so only the bits alive
; in the result are alive in each of them.
define i64 @phi32(i64 %0, i64 %1, i1 %c) {
  ; CHECK-LABEL: @phi32(
entry:
  br i1 %c, label %then, label %else

then:
  %a = add i64 %0, %1
  br label %join

else:
  %b = sub i64 %0, %1
  br label %join

join:
  ; CHECK: phi i32
  %p = phi i64 [ %a, %then ], [ %b, %else ]
  ; CHECK-NOT: and
  %masked = and i64 %p, 4294967295
  ret i64 %masked
}

; A loop counter feeds nothing but the phi it comes back to, so before this
; the whole chain stayed 64 bits wide.
define i64 @loop_counter32(i64 %n) {
  ; CHECK-LABEL: @loop_counter32(
entry:
  br label %loop

loop:
  ; CHECK: phi i32
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
  ; CHECK: add i32
  %next = add i64 %i, 1
  %masked_next = and i64 %next, 4294967295
  %masked_n = and i64 %n, 4294967295
  ; CHECK: icmp u{{lt|gt}} i32
  %cond = icmp ult i64 %masked_next, %masked_n
  br i1 %cond, label %loop, label %exit

exit:
  %r = and i64 %i, 4294967295
  ret i64 %r
}

; A select copies one of its two values bit for bit, the same as a phi. The
; condition picks between them rather than being one of them, so it stays as
; it is.
define i64 @select32(i64 %0, i64 %1, i1 %c) {
  ; CHECK-LABEL: @select32(
  %a = add i64 %0, %1
  %b = sub i64 %0, %1
  ; CHECK: select i1 %c, i32
  %s = select i1 %c, i64 %a, i64 %b
  ; CHECK-NOT: and
  %masked = and i64 %s, 4294967295
  ; CHECK: ret i64
  ret i64 %masked
}

; A signed comparison of zero extensions must become unsigned.
define i1 @zext_slt(i32 %a, i32 %b) {
  ; CMP-LABEL: @zext_slt(
  %ae = zext i32 %a to i64
  %be = zext i32 %b to i64
  ; CMP: %[[C:[^ ]+]] = icmp ult i32 %a, %b
  ; CMP-NEXT: ret i1 %[[C]]
  %c = icmp slt i64 %ae, %be
  ret i1 %c
}

; Use the larger narrow width and preserve unsigned order of sign extensions.
define i1 @different_sext_widths(i32 %a, i8 %b) {
  ; CMP-LABEL: @different_sext_widths(
  %ae = sext i32 %a to i64
  %be = sext i8 %b to i64
  ; CMP: %[[B:[^ ]+]] = sext i8 %b to i32
  ; CMP-NEXT: %[[C:[^ ]+]] = icmp ult i32 %a, %[[B]]
  ; CMP-NEXT: ret i1 %[[C]]
  %c = icmp ult i64 %ae, %be
  ret i1 %c
}

; A constant with the narrow sign bit set is still positive at wide width.
define i1 @zext_const_left(i32 %a) {
  ; CMP-LABEL: @zext_const_left(
  %ae = zext i32 %a to i64
  ; CMP: %[[C:[^ ]+]] = icmp uge i32 {{.*}}, %a
  ; CMP-NEXT: ret i1 %[[C]]
  %c = icmp sge i64 2147483648, %ae
  ret i1 %c
}

; A representable negative constant retains the signed predicate.
define i1 @sext_negative(i32 %a) {
  ; CMP-LABEL: @sext_negative(
  %ae = sext i32 %a to i64
  ; CMP: %[[C:[^ ]+]] = icmp slt i32 %a, {{.*}}
  ; CMP-NEXT: ret i1 %[[C]]
  %c = icmp slt i64 %ae, -42
  ret i1 %c
}

; Do not truncate a constant that loses bits.
define i1 @zext_large_unchanged(i32 %a) {
  ; CMP-LABEL: @zext_large_unchanged(
  %ae = zext i32 %a to i64
  ; CMP: %[[C:[^ ]+]] = icmp eq i64 {{.*}}, %ae
  ; CMP-NEXT: ret i1 %[[C]]
  %c = icmp eq i64 4294967296, %ae
  ret i1 %c
}

; A constant that fits unsigned may still fail the sign-extension round trip.
define i1 @sext_positive_unchanged(i32 %a) {
  ; CMP-LABEL: @sext_positive_unchanged(
  %ae = sext i32 %a to i64
  ; CMP: %[[C:[^ ]+]] = icmp ult i64 %ae, {{.*}}
  ; CMP-NEXT: ret i1 %[[C]]
  %c = icmp ult i64 %ae, 2147483648
  ret i1 %c
}

; Removing different extension kinds would change equality for high-bit values.
define i1 @mixed_extensions(i32 %a, i32 %b) {
  ; CMP-LABEL: @mixed_extensions(
  %ae = zext i32 %a to i64
  %be = sext i32 %b to i64
  ; CMP: %c = icmp eq i64 %ae, %be
  ; CMP-NEXT: ret i1 %c
  %c = icmp eq i64 %ae, %be
  ret i1 %c
}

; Do not truncate an operand whose high bits are unconstrained.
define i1 @unextended_operand(i32 %a, i64 %b) {
  ; CMP-LABEL: @unextended_operand(
  %ae = zext i32 %a to i64
  ; CMP: %c = icmp ult i64 %ae, %b
  ; CMP-NEXT: ret i1 %c
  %c = icmp ult i64 %ae, %b
  ret i1 %c
}
