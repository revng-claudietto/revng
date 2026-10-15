;
; This file is distributed under the MIT License. See LICENSE.md for details.
;

; RUN: %root/bin/revng opt -S -type-shrinking -early-cse -dce -verify %s | FileCheck %s
; RUN: %root/bin/revng opt -S -type-shrinking -instcombine -type-shrinking -early-cse -dce -verify %s | FileCheck --check-prefix=PIPE %s

; Values narrowed to a window of their bits that does not start at bit zero,
; such as a field packed in a register, and comparisons of the top bits of
; their operands.
;
; CHECK exercises type-shrinking alone. PIPE checks that the windows are
; recovered after InstCombine widens them back, which depends on the legal
; integer widths of the target.

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; A 16-bit field at offset 16 multiplied in place, next to a field at offset 0:
; `(uint16_t) (b * 5) << 16 | (uint16_t) a`.
define i64 @field_in_place(i64 %a, i64 %b) {
  ; CHECK-LABEL: @field_in_place(
  ; PIPE-LABEL: @field_in_place(
  %product = mul i64 %b, 327680
  %field = and i64 %product, 4294901760
  %low = and i64 %a, 65535
  %r = or i64 %low, %field
  ; CHECK: %[[M:[^ ]+]] = mul i16 %{{[^ ]+}}, 5
  ; CHECK: %[[E:[^ ]+]] = zext i16 %[[M]] to i32
  ; CHECK-NEXT: %[[S:[^ ]+]] = shl i32 %[[E]], 16
  ; CHECK-NEXT: or i32 %{{[^ ]+}}, %[[S]]
  ; PIPE: mul i16
  ; PIPE: shl i32
  ret i64 %r
}

; The same field at the top of the register, where no mask is left.
define i64 @top_field(i64 %a, i64 %b) {
  ; CHECK-LABEL: @top_field(
  ; PIPE-LABEL: @top_field(
  %field = mul i64 %b, 1407374883553280 ; 5 << 48
  %low = and i64 %a, 65535
  %r = or i64 %low, %field
  ; CHECK: %[[M:[^ ]+]] = mul i16 %{{[^ ]+}}, 5
  ; CHECK: %[[E:[^ ]+]] = zext i16 %[[M]] to i64
  ; CHECK-NEXT: %[[S:[^ ]+]] = shl i64 %[[E]], 48
  ; CHECK-NEXT: or i64 %{{[^ ]+}}, %[[S]]
  ; PIPE: mul i16
  ; PIPE: shl i64
  ret i64 %r
}

; A field bounded by a truncation instead of a mask.
define i32 @truncated_field(i64 %a, i64 %b) {
  ; CHECK-LABEL: @truncated_field(
  ; PIPE-LABEL: @truncated_field(
  %field = mul i64 %b, 327680
  %low = and i64 %a, 65535
  %o = or i64 %low, %field
  %r = trunc i64 %o to i32
  ; CHECK: mul i16
  ; CHECK: shl i32
  ; PIPE: mul i16
  ; PIPE: shl i32
  ret i32 %r
}

; A field moved back down by a shift reads the window as it is.
define i64 @field_extracted(i64 %x) {
  ; CHECK-LABEL: @field_extracted(
  ; PIPE-LABEL: @field_extracted(
  %product = mul i64 %x, 327680
  %shifted = lshr exact i64 %product, 16
  %r = and i64 %shifted, 65535
  ; CHECK: %[[T:[^ ]+]] = trunc i64 %x to i16
  ; CHECK-NEXT: %[[M:[^ ]+]] = mul i16 %[[T]], 5
  ; CHECK-NEXT: %[[E:[^ ]+]] = zext i16 %[[M]] to i64
  ; CHECK-NEXT: ret i64 %[[E]]
  ; PIPE: mul i16
  ; PIPE-NOT: lshr
  ; PIPE: ret i64
  ret i64 %r
}

define i64 @shifted_product(i64 %x) {
  ; CHECK-LABEL: @shifted_product(
  %product = mul i64 %x, 1407374883553280 ; 5 << 48
  %r = lshr i64 %product, 48
  ; CHECK: %[[T:[^ ]+]] = trunc i64 %x to i16
  ; CHECK-NEXT: %[[M:[^ ]+]] = mul i16 %[[T]], 5
  ; CHECK-NEXT: %[[E:[^ ]+]] = zext i16 %[[M]] to i64
  ; CHECK-NEXT: ret i64 %[[E]]
  ret i64 %r
}

; A right shift reading past the top of its operand gets zeros there.
define i8 @top_bit(i64 %x) {
  ; CHECK-LABEL: @top_bit(
  %bit = lshr i64 %x, 63
  %r = trunc i64 %bit to i8
  ; CHECK: %[[H:[^ ]+]] = lshr i64 %x, 63
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i64 %[[H]] to i8
  ; CHECK-NEXT: ret i8 %[[T]]
  ret i8 %r
}

; An arithmetic one gets copies of the sign.
define i8 @top_nibble(i32 %x) {
  ; CHECK-LABEL: @top_nibble(
  %nibble = ashr i32 %x, 28
  %r = trunc i32 %nibble to i8
  ; CHECK: %[[H:[^ ]+]] = ashr i32 %x, 28
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i32 %[[H]] to i8
  ; CHECK-NEXT: ret i8 %[[T]]
  ret i8 %r
}

; An 8-bit field at offset 8 incremented in place.
define i64 @field_add(i64 %x) {
  ; CHECK-LABEL: @field_add(
  %shifted = shl i64 %x, 8
  %sum = add i64 %shifted, 1280
  %field = and i64 %sum, 65280
  ; CHECK: %[[T:[^ ]+]] = trunc i64 %x to i8
  ; CHECK-NEXT: %[[A:[^ ]+]] = add i8 %[[T]], 5
  ; CHECK-NEXT: %[[E:[^ ]+]] = zext i8 %[[A]] to i64
  ; CHECK-NEXT: %[[S:[^ ]+]] = shl i64 %[[E]], 8
  ; CHECK-NEXT: ret i64 %[[S]]
  ret i64 %field
}

; The product is only a multiple of 2^15, so reading bits 16 to 31 of it would
; take a right shift, and rebuilding the field a left one: it stays wide.
define i64 @field_insufficient_factor(i64 %x) {
  ; CHECK-LABEL: @field_insufficient_factor(
  %product = mul i64 %x, 98304
  %field = and i64 %product, 4294901760
  ; CHECK: %[[M:[^ ]+]] = mul i32 %{{[^ ]+}}, u0x18000
  ; CHECK-NEXT: %[[A:[^ ]+]] = and i32 %[[M]], u0xffff0000
  ; CHECK-NEXT: %[[E:[^ ]+]] = zext i32 %[[A]] to i64
  ; CHECK-NEXT: ret i64 %[[E]]
  ret i64 %field
}

; Adding a multiple of 2^16 cannot carry into bits 16 and up from below, so
; the field is added at its own width.
define i64 @add_into_field(i64 %x, i64 %y) {
  ; CHECK-LABEL: @add_into_field(
  ; PIPE-LABEL: @add_into_field(
  %high = shl i64 %y, 16
  %sum = add i64 %x, %high
  %shifted = lshr i64 %sum, 16
  %r = and i64 %shifted, 65535
  ; CHECK: add i16
  ; PIPE: add i16
  ret i64 %r
}

; Neither can subtracting one borrow.
define i64 @sub_from_field(i64 %x, i64 %y) {
  ; CHECK-LABEL: @sub_from_field(
  ; PIPE-LABEL: @sub_from_field(
  %high = shl i64 %y, 16
  %difference = sub i64 %x, %high
  %shifted = lshr i64 %difference, 16
  %r = and i64 %shifted, 65535
  ; CHECK: sub i16
  ; PIPE: sub i16
  ret i64 %r
}

; Subtracting a value with nonzero low bits can borrow from the field.
define i64 @borrow_into_field(i64 %x, i64 %y) {
  ; CHECK-LABEL: @borrow_into_field(
  ; CHECK-NOT: sub i16
  ; CHECK: sub i32
  %high = shl i64 %y, 16
  %difference = sub i64 %high, %x
  %shifted = lshr i64 %difference, 16
  %r = and i64 %shifted, 65535
  ret i64 %r
}

; Adding two values with nonzero low bits can carry into the field.
define i64 @carry_into_field(i64 %x, i64 %y) {
  ; CHECK-LABEL: @carry_into_field(
  ; CHECK-NOT: add i16
  ; CHECK: add i32
  %sum = add i64 %x, %y
  %shifted = lshr i64 %sum, 16
  %r = and i64 %shifted, 65535
  ret i64 %r
}

; The top 16 bits compared with -9. The mask only clears ignored bits.
define i1 @masked_compare(i64 %x) {
  ; CHECK-LABEL: @masked_compare(
  ; PIPE-LABEL: @masked_compare(
  %masked = and i64 %x, -4294967296
  %r = icmp slt i64 %masked, -2533274790395904
  ; CHECK: %[[H:[^ ]+]] = lshr i64 %x, 48
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i64 %[[H]] to i16
  ; CHECK-NEXT: icmp slt i16 %[[T]], u0xfff7
  ; PIPE: %[[H:[^ ]+]] = lshr i64 %x, 48
  ; PIPE-NEXT: %[[T:[^ ]+]] = trunc i64 %[[H]] to i16
  ; PIPE-NEXT: icmp slt i16 %[[T]], u0xfff7
  ret i1 %r
}

; A 16-bit field at offset 16 compared with 201.
define i1 @offset_compare(i64 %x) {
  ; CHECK-LABEL: @offset_compare(
  %low = trunc i64 %x to i32
  %r = icmp ult i32 %low, 13172736
  ; CHECK: %[[H:[^ ]+]] = lshr i32 %{{[^ ]+}}, 16
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i32 %[[H]] to i16
  ; CHECK-NEXT: icmp ult i16 %[[T]], 201
  ret i1 %r
}

; The bounds of %x leave its low 32 bits, and the boundary frees the low 16:
; together, the 16-bit field at offset 16 of %a.
define i1 @bounded_offset_compare(i32 %a) {
  ; CHECK-LABEL: @bounded_offset_compare(
  %x = zext i32 %a to i64
  %r = icmp ult i64 %x, 13172736
  ; CHECK: %[[H:[^ ]+]] = lshr i32 %a, 16
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i32 %[[H]] to i16
  ; CHECK-NEXT: icmp ult i16 %[[T]], 201
  ret i1 %r
}

; 300 frees the low 2 bits, so the 9-bit sum would fit 8 bits from bit 1. The
; sum does not hold those bits as they are, so it is compared whole.
define i1 @unprovided_compare(i8 %x, i8 %y) {
  ; CHECK-LABEL: @unprovided_compare(
  ; CHECK-NOT: lshr
  ; CHECK: icmp ult i16 %{{[^ ]+}}, 300
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  %sum = add i64 %a, %b
  %r = icmp ult i64 %sum, 300
  ret i1 %r
}

; `x < (y << 32)`, written by InstCombine as `(y << 32) > x`.
define i1 @variable_boundary(i64 %x, i64 %y) {
  ; CHECK-LABEL: @variable_boundary(
  %boundary = shl i64 %y, 32
  %r = icmp ugt i64 %boundary, %x
  ; CHECK: %[[Y:[^ ]+]] = trunc i64 %y to i32
  ; CHECK-NEXT: %[[H:[^ ]+]] = lshr i64 %x, 32
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i64 %[[H]] to i32
  ; CHECK-NEXT: icmp ugt i32 %[[Y]], %[[T]]
  ret i1 %r
}

; `>` and `<=` ignore the trailing ones of the constant instead.
define i1 @greater_compare(i32 %x) {
  ; CHECK-LABEL: @greater_compare(
  %r = icmp ugt i32 %x, 13172735
  ; CHECK: %[[H:[^ ]+]] = lshr i32 %x, 16
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i32 %[[H]] to i16
  ; CHECK-NEXT: icmp ugt i16 %[[T]], 200
  ret i1 %r
}

define i1 @less_equal_compare(i32 %x) {
  ; CHECK-LABEL: @less_equal_compare(
  %r = icmp sle i32 %x, -589825
  ; CHECK: %[[H:[^ ]+]] = lshr i32 %x, 16
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i32 %[[H]] to i16
  ; CHECK-NEXT: icmp sle i16 %[[T]], u0xfff6
  ret i1 %r
}

; A constant on the left ending in ones: `0xc8ffff < x` is `x >= 0xc90000`.
define i1 @constant_on_the_left(i32 %x) {
  ; CHECK-LABEL: @constant_on_the_left(
  %r = icmp ult i32 13172735, %x
  ; CHECK: %[[H:[^ ]+]] = lshr i32 %x, 16
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i32 %[[H]] to i16
  ; CHECK-NEXT: icmp ult i16 200, %[[T]]
  ret i1 %r
}

; A mask clearing bit 50 changes the compared bits, so it is kept.
define i1 @masked_high_bit(i64 %x) {
  ; CHECK-LABEL: @masked_high_bit(
  %masked = and i64 %x, -1125899906842625
  %r = icmp slt i64 %masked, -2533274790395904
  ; CHECK: %[[A:[^ ]+]] = and i64 %x, u0xfffbffffffffffff
  ; CHECK-NEXT: %[[H:[^ ]+]] = lshr i64 %[[A]], 48
  ; CHECK-NEXT: %[[T:[^ ]+]] = trunc i64 %[[H]] to i16
  ; CHECK-NEXT: icmp slt i16 %[[T]], u0xfff7
  ret i1 %r
}

; Boundaries of zero or plus or minus a power of two are range checks.
define i1 @power_of_two_bound(i32 %x) {
  ; CHECK-LABEL: @power_of_two_bound(
  ; CHECK-NOT: lshr
  ; CHECK: icmp ult i32 %x, u0x10000
  %r = icmp ult i32 %x, 65536
  ret i1 %r
}

define i1 @power_of_two_bound_above(i32 %x) {
  ; CHECK-LABEL: @power_of_two_bound_above(
  ; CHECK-NOT: lshr
  ; CHECK: icmp ugt i32 %x, u0xffff
  %r = icmp ugt i32 %x, 65535
  ret i1 %r
}

define i1 @negated_power_of_two_bound(i32 %x) {
  ; CHECK-LABEL: @negated_power_of_two_bound(
  ; CHECK-NOT: lshr
  ; CHECK: icmp slt i32 %x, u0xffff0000
  %r = icmp slt i32 %x, -65536
  ret i1 %r
}

define i1 @sign_test(i64 %x) {
  ; CHECK-LABEL: @sign_test(
  ; CHECK-NOT: lshr
  ; CHECK: icmp slt i64 %x, 0
  %r = icmp slt i64 %x, 0
  ret i1 %r
}

; Equality depends on every bit, unless both operands are multiples.
define i1 @equality(i32 %x) {
  ; CHECK-LABEL: @equality(
  ; CHECK-NOT: lshr
  ; CHECK: icmp eq i32 %x, u0xc90000
  %r = icmp eq i32 %x, 13172736
  ret i1 %r
}

define i1 @variable_equality(i64 %x, i64 %y) {
  ; CHECK-LABEL: @variable_equality(
  ; CHECK-NOT: lshr
  ; CHECK: icmp eq i64 %x,
  %boundary = shl i64 %y, 32
  %r = icmp eq i64 %x, %boundary
  ret i1 %r
}

; 12 ignored bits leave no emittable width.
define i1 @too_few_ignored_bits(i32 %x) {
  ; CHECK-LABEL: @too_few_ignored_bits(
  ; CHECK-NOT: lshr
  ; CHECK: icmp ult i32 %x, u0xc9000
  %r = icmp ult i32 %x, 823296
  ret i1 %r
}
