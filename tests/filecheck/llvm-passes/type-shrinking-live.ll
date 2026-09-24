;
; This file is distributed under the MIT License. See LICENSE.md for details.
;
; RUN: %root/bin/revng opt -S -early-cse -dce -type-shrinking -verify %s | FileCheck %s

; All computations contribute to a returned value or an opaque call.
; Clean up before analysis so every instruction contributes to the result.
declare void @observe(i64)

; The opaque call demands the quotient, whose operands fit in unsigned bytes.
define i8 @live_division(i16 %x, i8 %y) {
  ; CHECK-LABEL: @live_division(
  ; CHECK: [[SUM:%[a-zA-Z0-9._]+]] = add i8
  ; CHECK: [[QUOTIENT:%[a-zA-Z0-9._]+]] = udiv i8
  ; CHECK: [[WIDE:%[a-zA-Z0-9._]+]] = zext i8 [[QUOTIENT]] to i64
  ; CHECK: call void @observe(i64 [[WIDE]])
  ; CHECK: ret i8 [[SUM]]
  %wide = zext i16 %x to i64
  %sum = add i64 %wide, 255
  %low = trunc i64 %sum to i8
  %numerator = and i64 %sum, 255
  %wide_y = zext i8 %y to i64
  %divisor = or i64 %wide_y, 1
  %quotient = udiv i64 %numerator, %divisor
  call void @observe(i64 %quotient)
  ret i8 %low
}

; DemandedBits currently retains 16 bits of the sum. Only its low eight bits
; affect the returned product, because multiplication supplies eight low zeros.
define i16 @live_product(i64 %x, i64 %y) {
  ; CHECK-LABEL: @live_product(
  ; CHECK: [[SUM:%[a-zA-Z0-9._]+]] = add i16
  ; CHECK: [[PRODUCT:%[a-zA-Z0-9._]+]] = mul i16 [[SUM]], 256
  ; CHECK: ret i16 [[PRODUCT]]
  %sum = add i64 %x, %y
  %product = mul i64 %sum, 256
  %low = trunc i64 %product to i16
  ret i16 %low
}

; The same reasoning applies to a variable factor with eight known low zeros.
; Neither the product nor its factor is a compile-time constant.
define i16 @live_variable_product(i64 %x, i64 %y, i8 %factor) {
  ; CHECK-LABEL: @live_variable_product(
  ; CHECK: [[SUM:%[a-zA-Z0-9._]+]] = add i16
  ; CHECK: [[WIDE:%[a-zA-Z0-9._]+]] = zext i8 %factor to i16
  ; CHECK: [[FACTOR:%[a-zA-Z0-9._]+]] = shl i16 [[WIDE]], 8
  ; CHECK: [[PRODUCT:%[a-zA-Z0-9._]+]] = mul i16 [[SUM]], [[FACTOR]]
  ; CHECK: ret i16 [[PRODUCT]]
  %sum = add i64 %x, %y
  %wide = zext i8 %factor to i64
  %scaled_factor = shl i64 %wide, 8
  %product = mul i64 %sum, %scaled_factor
  %low = trunc i64 %product to i16
  ret i16 %low
}
