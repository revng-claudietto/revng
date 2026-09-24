;
; This file is distributed under the MIT License. See LICENSE.md for details.
;

; RUN: %root/bin/revng opt -S -type-shrinking -early-cse -dce -verify %s | FileCheck --check-prefixes=CHECK,DIRECT %s
; RUN: %root/bin/revng opt -S -type-shrinking -instcombine -type-shrinking -early-cse -dce -verify %s | FileCheck %s
; RUN: %root/bin/revng opt -S -type-shrinking -verify %s | FileCheck --check-prefix=RAW %s
; RUN: %root/bin/revng opt -S -type-shrinking -min-width=16 -early-cse -dce -verify %s | FileCheck --check-prefix=MIN %s

; All result bits are observed, but the variable mask bounds the value.
define i64 @variable_mask(i64 %x, i8 %mask) {
  ; CHECK-LABEL: @variable_mask(
  ; CHECK: and i8
  ; CHECK: zext i8 {{.*}} to i64
  ; MIN-LABEL: @variable_mask(
  ; MIN: and i16
  %m = zext i8 %mask to i64
  %r = and i64 %x, %m
  ret i64 %r
}

; Forward bounds preserve the carry when the whole sum is returned.
define i64 @unsigned_sum(i8 %x, i8 %y) {
  ; CHECK-LABEL: @unsigned_sum(
  ; CHECK: add i16
  ; CHECK: zext i16 {{.*}} to i64
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  %r = add i64 %a, %b
  ret i64 %r
}

define i64 @unsigned_difference(i8 %x, i8 %y) {
  ; CHECK-LABEL: @unsigned_difference(
  ; CHECK: sub i16
  ; CHECK: sext i16 {{.*}} to i64
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  %r = sub i64 %a, %b
  ret i64 %r
}

define i64 @unsigned_product(i8 %x, i8 %y) {
  ; CHECK-LABEL: @unsigned_product(
  ; CHECK: mul i16
  ; CHECK: zext i16 {{.*}} to i64
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  %r = mul i64 %a, %b
  ret i64 %r
}

define i64 @signed_sum(i8 %x, i8 %y) {
  ; CHECK-LABEL: @signed_sum(
  ; CHECK: add i16
  ; CHECK: sext i16 {{.*}} to i64
  %a = sext i8 %x to i64
  %b = sext i8 %y to i64
  %r = add i64 %a, %b
  ret i64 %r
}

; The unknown sign bit is replicated, rather than individually known.
define i64 @signed_bits(i8 %x, i8 %y) {
  ; CHECK-LABEL: @signed_bits(
  ; CHECK: xor i8
  ; CHECK: sext i8 {{.*}} to i64
  %a = sext i8 %x to i64
  %b = sext i8 %y to i64
  %r = xor i64 %a, %b
  ret i64 %r
}

; Demand can narrow an operation whose full value has no narrow bound.
define i8 @demanded_sum(i64 %x, i64 %y) {
  ; CHECK-LABEL: @demanded_sum(
  ; CHECK: add i8
  ; CHECK: ret i8
  %r = add i64 %x, %y
  %low = trunc i64 %r to i8
  ret i8 %low
}

; Join zero and sign extension in a representation that preserves both ranges.
define i64 @mixed_select(i1 %c, i8 %x, i8 %y) {
  ; CHECK-LABEL: @mixed_select(
  ; CHECK: select i1 %c, i16
  ; CHECK: sext i16 {{.*}} to i64
  %a = zext i8 %x to i64
  %b = sext i8 %y to i64
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

define i64 @signed_select(i1 %c, i8 %x) {
  ; CHECK-LABEL: @signed_select(
  ; CHECK: select i1 %c, i8
  ; CHECK: sext i8 {{.*}} to i64
  %a = sext i8 %x to i64
  %r = select i1 %c, i64 %a, i64 -42
  ret i64 %r
}

define i64 @different_widths(i1 %c, i8 %x, i16 %y) {
  ; CHECK-LABEL: @different_widths(
  ; CHECK: select i1 %c, i16
  ; CHECK: zext i16 {{.*}} to i64
  %a = zext i8 %x to i64
  %b = zext i16 %y to i64
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

; Arbitrary incoming values must prevent exact narrowing of the merge.
define i64 @unknown_incoming(i1 %c, i8 %x, i64 %y) {
  ; CHECK-LABEL: @unknown_incoming(
  ; CHECK: select i1 %c, i64
  %a = zext i8 %x to i64
  %r = select i1 %c, i64 %a, i64 %y
  ret i64 %r
}

; Freezing a possibly poisoned extension can produce any wide value.
define i64 @freeze_boundary(i1 %c, i8 %x, i8 %y) {
  ; CHECK-LABEL: @freeze_boundary(
  ; DIRECT: freeze i64
  ; DIRECT: select i1 %c, i64
  %a = zext i8 %x to i64
  %f = freeze i64 %a
  %b = zext i8 %y to i64
  %r = select i1 %c, i64 %f, i64 %b
  ret i64 %r
}

; The common representation is inferred through arithmetic, not matched casts.
define i1 @compare_computations(i8 %x, i8 %y) {
  ; CHECK-LABEL: @compare_computations(
  ; CHECK: add i16
  ; CHECK: icmp u{{lt|gt}} i16
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  %sum = add i64 %a, %b
  %r = icmp slt i64 %sum, 300
  ret i1 %r
}

define i1 @compare_mixed_extensions(i8 %x, i8 %y) {
  ; CHECK-LABEL: @compare_mixed_extensions(
  ; CHECK: icmp slt i16
  %a = zext i8 %x to i64
  %b = sext i8 %y to i64
  %r = icmp slt i64 %a, %b
  ret i1 %r
}

; Execution needs both the demanded bits and those shifted into them.
define i8 @logical_shift_demand(i64 %x) {
  ; CHECK-LABEL: @logical_shift_demand(
  ; CHECK: lshr i16 {{.*}}, 8
  ; CHECK: trunc i16 {{.*}} to i8
  %shift = lshr i64 %x, 8
  %r = trunc i64 %shift to i8
  ret i8 %r
}

define i64 @signed_shift(i8 %x) {
  ; CHECK-LABEL: @signed_shift(
  ; CHECK: ashr i8 {{.*}}, 3
  ; CHECK: sext i8 {{.*}} to i64
  %a = sext i8 %x to i64
  %r = ashr i64 %a, 3
  ret i64 %r
}

define i64 @bounded_shift(i8 %x, i8 %amount) {
  ; CHECK-LABEL: @bounded_shift(
  ; CHECK: lshr i8
  ; CHECK: zext i8 {{.*}} to i64
  %a = zext i8 %x to i64
  %masked = and i8 %amount, 7
  %n = zext i8 %masked to i64
  %r = lshr i64 %a, %n
  ret i64 %r
}

; Eight is a defined wide shift count: do not turn it into an i8 overshift.
define i64 @shift_boundary(i8 %x, i8 %amount) {
  ; CHECK-LABEL: @shift_boundary(
  ; CHECK: lshr i16
  %a = zext i8 %x to i64
  %masked = and i8 %amount, 15
  %n = zext i8 %masked to i64
  %r = lshr i64 %a, %n
  ret i64 %r
}

define i64 @unknown_shift(i8 %x, i64 %amount) {
  ; CHECK-LABEL: @unknown_shift(
  ; CHECK: lshr i64
  %a = zext i8 %x to i64
  %r = lshr i64 %a, %amount
  ret i64 %r
}

; Low result bits do not suffice to truncate division operands.
define i8 @wide_division(i64 %x, i64 %y) {
  ; CHECK-LABEL: @wide_division(
  ; CHECK: udiv i64
  %q = udiv i64 %x, %y
  %r = trunc i64 %q to i8
  ret i8 %r
}

define i64 @unsigned_division(i8 %x, i8 %y) {
  ; CHECK-LABEL: @unsigned_division(
  ; CHECK: udiv i8
  ; CHECK: zext i8 {{.*}} to i64
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  %r = udiv i64 %a, %b
  ret i64 %r
}

define i64 @unsigned_remainder(i8 %x, i8 %y) {
  ; CHECK-LABEL: @unsigned_remainder(
  ; CHECK: urem i8
  ; CHECK: zext i8 {{.*}} to i64
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  %r = urem i64 %a, %b
  ret i64 %r
}

; Both sdiv and srem must avoid introducing INT_MIN / -1 at the new width.
define i64 @signed_division(i8 %x, i8 %y) {
  ; CHECK-LABEL: @signed_division(
  ; CHECK: sdiv i16
  ; CHECK: sext i16 {{.*}} to i64
  %a = sext i8 %x to i64
  %b = sext i8 %y to i64
  %r = sdiv i64 %a, %b
  ret i64 %r
}

define i64 @signed_remainder(i8 %x, i8 %y) {
  ; CHECK-LABEL: @signed_remainder(
  ; CHECK: srem i16
  ; CHECK: trunc i16 {{.*}} to i8
  ; CHECK: sext i8 {{.*}} to i64
  %a = sext i8 %x to i64
  %b = sext i8 %y to i64
  %r = srem i64 %a, %b
  ret i64 %r
}

define i64 @safe_signed_division(i8 %x) {
  ; CHECK-LABEL: @safe_signed_division(
  ; CHECK: {{sdiv|ashr}} i8
  ; CHECK: sext i8 {{.*}} to i64
  %a = sext i8 %x to i64
  %r = sdiv i64 %a, 2
  ret i64 %r
}

; A shared producer must still supply all bits observed by its wide consumer.
define i8 @shared_wide_use(i64 %x, i64 %y, ptr %out) {
  ; CHECK-LABEL: @shared_wide_use(
  ; CHECK: add i64
  ; CHECK: store i64
  %sum = add i64 %x, %y
  store i64 %sum, ptr %out
  %r = trunc i64 %sum to i8
  ret i8 %r
}

; Dropping nowrap is required even if narrowing only changes its operands.
define i8 @nowrap(i64 %x, i64 %y) {
  ; CHECK-LABEL: @nowrap(
  ; CHECK: add i8
  %sum = add nuw nsw i64 %x, %y
  %r = trunc i64 %sum to i8
  ret i8 %r
}

; A genuine cycle through arithmetic and a mask has a stable narrow range.
define i64 @masked_recurrence(i8 %initial, i32 %n) {
  ; CHECK-LABEL: @masked_recurrence(
entry:
  %a = zext i8 %initial to i64
  br label %loop
loop:
  ; CHECK: phi i8
  %value = phi i64 [ %a, %entry ], [ %masked, %loop ]
  %index = phi i32 [ 0, %entry ], [ %nextindex, %loop ]
  %next = add i64 %value, 1
  %masked = and i64 %next, 255
  %nextindex = add i32 %index, 1
  %done = icmp eq i32 %nextindex, %n
  br i1 %done, label %exit, label %loop
exit:
  ; CHECK: zext i8 {{.*}} to i64
  ret i64 %masked
}

; Without the mask, the seed's width does not bound the recurrence.
define i64 @growing_recurrence(i8 %initial, i64 %n) {
  ; CHECK-LABEL: @growing_recurrence(
entry:
  %a = zext i8 %initial to i64
  br label %loop
loop:
  ; CHECK: phi i64
  %value = phi i64 [ %a, %entry ], [ %next, %loop ]
  %index = phi i64 [ 0, %entry ], [ %nextindex, %loop ]
  ; CHECK: add i64
  %next = add i64 %value, 1
  %nextindex = add i64 %index, 1
  %done = icmp eq i64 %nextindex, %n
  br i1 %done, label %exit, label %loop
exit:
  ret i64 %next
}

; Multiple PHIs in the same block, cyclic references, and a select backedge.
define i64 @cyclic_merges(i8 %x, i8 %y, i1 %choose, i1 %again) {
  ; CHECK-LABEL: @cyclic_merges(
entry:
  %a = sext i8 %x to i64
  %b = sext i8 %y to i64
  br label %loop
loop:
  ; CHECK: phi i8
  ; CHECK: phi i8
  %p = phi i64 [ %a, %entry ], [ %q, %loop ]
  %q = phi i64 [ %b, %entry ], [ %s, %loop ]
  %s = select i1 %choose, i64 %p, i64 %q
  br i1 %again, label %loop, label %exit
exit:
  ; CHECK: sext i8 {{.*}} to i64
  ret i64 %s
}

; Reuse exactly the same incoming cast for duplicate predecessor edges.
define i64 @duplicate_edges(i32 %which, i8 %x, i8 %y) {
  ; CHECK-LABEL: @duplicate_edges(
entry:
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  switch i32 %which, label %other [ i32 0, label %join
                                  i32 1, label %join ]
other:
  br label %join
join:
  ; CHECK: phi i8
  %r = phi i64 [ %a, %entry ], [ %a, %entry ], [ %b, %other ]
  ret i64 %r
}

; Unreachable cycles are left out of analysis and rewriting.
define i64 @unseeded_cycle() {
  ; CHECK-LABEL: @unseeded_cycle(
entry:
  ret i64 0
unreachable:
  ; DIRECT: phi i64
  %p = phi i64 [ %p, %unreachable ]
  br i1 true, label %unreachable, label %exit
exit:
  ret i64 %p
}

; Neither an unseeded cycle nor a constant on a dead edge widens a live PHI.
define i64 @unreachable_incoming(i8 %x, i8 %y, i1 %c) {
  ; RAW-LABEL: @unreachable_incoming(
entry:
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  br i1 %c, label %live, label %join
live:
  br label %join
dead:
  ; RAW: %cycle = phi i64 [ %cycle, %dead ]
  %cycle = phi i64 [ %cycle, %dead ]
  br i1 %c, label %dead, label %join
dead_constant:
  br label %join
join:
  ; RAW: phi i8 [ %x, %entry ], [ %y, %live ], [ poison, %dead ], [ poison, %dead_constant ]
  ; RAW: zext i8 {{.*}} to i64
  %r = phi i64 [ %a, %entry ], [ %b, %live ], [ %cycle, %dead ], [ -1, %dead_constant ]
  ret i64 %r
}

; An unreachable store can conservatively keep a reachable producer wide.
; Rebuilding must still leave valid IR.
define i8 @unreachable_user(i64 %x, i64 %y, ptr %out) {
  ; RAW-LABEL: @unreachable_user(
entry:
  ; RAW: add i64
  %sum = add i64 %x, %y
  %r = trunc i64 %sum to i8
  ret i8 %r
dead:
  ; RAW: dead:
  ; RAW-NEXT: store i64 {{.*}}, ptr %out
  store i64 %sum, ptr %out
  ret i8 0
}

; Full-width arbitrary leaves must not have bottom truncable bits.
define i64 @undef_incoming(i1 %c, i8 %x) {
  ; RAW-LABEL: @undef_incoming(
  ; RAW: select i1 %c, i64
  %a = zext i8 %x to i64
  %r = select i1 %c, i64 %a, i64 undef
  ret i64 %r
}

define i64 @poison_incoming(i1 %c, i8 %x) {
  ; RAW-LABEL: @poison_incoming(
  ; RAW: select i1 %c, i64
  %a = zext i8 %x to i64
  %r = select i1 %c, i64 %a, i64 poison
  ret i64 %r
}

; Discard the unused division before narrowing its shared divisor. Otherwise
; dropping the nonzero high bit could introduce division by zero.
define i8 @unused_divisor(i64 %x) {
  ; RAW-LABEL: @unused_divisor(
  ; RAW-NOT: or i64
  ; RAW-NOT: udiv
  ; RAW: ret i8
  %d = or i64 %x, 256
  %unused = udiv i64 42, %d
  %r = trunc i64 %d to i8
  ret i8 %r
}

; Dead call chains must not keep a live producer wide either.
declare i64 @pure(i64 noundef) readnone nounwind willreturn
define i8 @unused_calls(i64 %x) {
  ; RAW-LABEL: @unused_calls(
  ; RAW-NOT: add i64
  ; RAW-NOT: call
  ; RAW: add i8
  ; RAW-NOT: call
  ; RAW: ret i8
  %sum = add i64 %x, 1
  %unused = call i64 @pure(i64 %sum)
  %also_unused = call i64 @pure(i64 %unused)
  %r = trunc i64 %sum to i8
  ret i8 %r
}

; Removing a dead cycle must leave its live input and the loop intact.
define i8 @unused_cycle(i64 %x, i1 %continue) {
  ; RAW-LABEL: @unused_cycle(
  ; RAW-NOT: phi
  ; RAW-NOT: add i64
  ; RAW: add i8
  ; RAW-NOT: phi
  ; RAW-NOT: add i64
  ; RAW: br i1 %continue
  ; RAW-NOT: phi
  ; RAW-NOT: add i64
  ; RAW: ret i8
entry:
  %sum = add i64 %x, 1
  br label %loop
loop:
  %unused = phi i64 [ %sum, %entry ], [ %next, %loop ]
  %next = add i64 %unused, 1
  br i1 %continue, label %loop, label %exit
exit:
  %r = trunc i64 %sum to i8
  ret i8 %r
}

; A call with observable effects still needs the full argument value.
declare void @observe(i64)
define i8 @live_call(i64 %x) {
  ; RAW-LABEL: @live_call(
  ; RAW: %[[SUM:[^ ]+]] = add i64 %x, 1
  ; RAW: call void @observe(i64 %[[SUM]])
  ; RAW: ret i8
  %sum = add i64 %x, 1
  call void @observe(i64 %sum)
  %r = trunc i64 %sum to i8
  ret i8 %r
}

; Exact and nowrap flags must not migrate to a narrowed computation.
define i8 @exact_shift(i64 %x) {
  ; CHECK-LABEL: @exact_shift(
  ; CHECK: lshr i16 {{.*}}, 8
  %r = lshr exact i64 %x, 8
  %low = trunc i64 %r to i8
  ret i8 %low
}

define i128 @wide_source(i8 %x, i8 %y) {
  ; CHECK-LABEL: @wide_source(
  ; CHECK: add i16
  ; CHECK: zext i16 {{.*}} to i128
  %a = zext i8 %x to i128
  %b = zext i8 %y to i128
  %r = add i128 %a, %b
  ret i128 %r
}

define <2 x i64> @vector_unchanged(<2 x i64> %x, <2 x i64> %y) {
  ; CHECK-LABEL: @vector_unchanged(
  ; CHECK: add <2 x i64>
  %r = add <2 x i64> %x, %y
  ret <2 x i64> %r
}

; Four merge layers share the same narrow sources.
define i64 @layered_merges(i8 %x, i8 %y, i1 %c) {
  ; CHECK-LABEL: @layered_merges(
  ; CHECK-NOT: phi i64
  ; CHECK: phi i8
  ; CHECK-NOT: phi i64
  ; CHECK: ret i64
entry:
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  br i1 %c, label %other0, label %join0
other0:
  br label %join0
join0:
  %p0 = phi i64 [ %a, %entry ], [ %b, %other0 ]
  br i1 %c, label %other1, label %join1
other1:
  br label %join1
join1:
  %p1 = phi i64 [ %p0, %join0 ], [ %b, %other1 ]
  br i1 %c, label %other2, label %join2
other2:
  br label %join2
join2:
  %p2 = phi i64 [ %p1, %join1 ], [ %b, %other2 ]
  br i1 %c, label %other3, label %join3
other3:
  br label %join3
join3:
  %p3 = phi i64 [ %p2, %join2 ], [ %b, %other3 ]
  ret i64 %p3
}

; Width information must cross more than 64 operations.
define i64 @deep_expression(i8 %x) {
  ; CHECK-LABEL: @deep_expression(
  ; CHECK-NOT: xor i64
  ; CHECK: xor i8
  ; CHECK-NOT: xor i64
  ; CHECK: ret i64
  %a = zext i8 %x to i64
  %v0 = xor i64 %a, 1
  %v1 = xor i64 %v0, 2
  %v2 = xor i64 %v1, 3
  %v3 = xor i64 %v2, 4
  %v4 = xor i64 %v3, 5
  %v5 = xor i64 %v4, 6
  %v6 = xor i64 %v5, 7
  %v7 = xor i64 %v6, 8
  %v8 = xor i64 %v7, 9
  %v9 = xor i64 %v8, 10
  %v10 = xor i64 %v9, 11
  %v11 = xor i64 %v10, 12
  %v12 = xor i64 %v11, 13
  %v13 = xor i64 %v12, 14
  %v14 = xor i64 %v13, 15
  %v15 = xor i64 %v14, 16
  %v16 = xor i64 %v15, 17
  %v17 = xor i64 %v16, 18
  %v18 = xor i64 %v17, 19
  %v19 = xor i64 %v18, 20
  %v20 = xor i64 %v19, 21
  %v21 = xor i64 %v20, 22
  %v22 = xor i64 %v21, 23
  %v23 = xor i64 %v22, 24
  %v24 = xor i64 %v23, 25
  %v25 = xor i64 %v24, 26
  %v26 = xor i64 %v25, 27
  %v27 = xor i64 %v26, 28
  %v28 = xor i64 %v27, 29
  %v29 = xor i64 %v28, 30
  %v30 = xor i64 %v29, 31
  %v31 = xor i64 %v30, 32
  %v32 = xor i64 %v31, 33
  %v33 = xor i64 %v32, 34
  %v34 = xor i64 %v33, 35
  %v35 = xor i64 %v34, 36
  %v36 = xor i64 %v35, 37
  %v37 = xor i64 %v36, 38
  %v38 = xor i64 %v37, 39
  %v39 = xor i64 %v38, 40
  %v40 = xor i64 %v39, 41
  %v41 = xor i64 %v40, 42
  %v42 = xor i64 %v41, 43
  %v43 = xor i64 %v42, 44
  %v44 = xor i64 %v43, 45
  %v45 = xor i64 %v44, 46
  %v46 = xor i64 %v45, 47
  %v47 = xor i64 %v46, 48
  %v48 = xor i64 %v47, 49
  %v49 = xor i64 %v48, 50
  %v50 = xor i64 %v49, 51
  %v51 = xor i64 %v50, 52
  %v52 = xor i64 %v51, 53
  %v53 = xor i64 %v52, 54
  %v54 = xor i64 %v53, 55
  %v55 = xor i64 %v54, 56
  %v56 = xor i64 %v55, 57
  %v57 = xor i64 %v56, 58
  %v58 = xor i64 %v57, 59
  %v59 = xor i64 %v58, 60
  %v60 = xor i64 %v59, 61
  %v61 = xor i64 %v60, 62
  %v62 = xor i64 %v61, 63
  %v63 = xor i64 %v62, 64
  %v64 = xor i64 %v63, 65
  %v65 = xor i64 %v64, 66
  %v66 = xor i64 %v65, 67
  %v67 = xor i64 %v66, 68
  %v68 = xor i64 %v67, 69
  %v69 = xor i64 %v68, 70
  %v70 = xor i64 %v69, 71
  %v71 = xor i64 %v70, 72
  %v72 = xor i64 %v71, 73
  %v73 = xor i64 %v72, 74
  %v74 = xor i64 %v73, 75
  %v75 = xor i64 %v74, 76
  %v76 = xor i64 %v75, 77
  %v77 = xor i64 %v76, 78
  %v78 = xor i64 %v77, 79
  %v79 = xor i64 %v78, 80
  ret i64 %v79
}

; A terminator's result is unavailable for a cast before that terminator.
; Preserve this graph until rebuilding supports splitting exceptional edges.
define i8 @terminator_incoming() personality ptr @personality {
  ; CHECK-LABEL: @terminator_incoming(
  ; RAW-LABEL: @terminator_incoming(
  ; CHECK: invoke i64 @may_throw()
  ; RAW: phi i64
  ; CHECK: trunc i64 {{.*}} to i8
entry:
  %value = invoke i64 @may_throw() to label %normal unwind label %unwind
normal:
  %merged = phi i64 [ %value, %entry ]
  %low = trunc i64 %merged to i8
  ret i8 %low
unwind:
  %exception = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %exception
}

; An unreachable invoke needs no edge cast and must not prevent narrowing.
define i64 @unreachable_invoke(i8 %x) personality ptr @personality {
  ; RAW-LABEL: @unreachable_invoke(
entry:
  %a = zext i8 %x to i64
  br label %normal
dead:
  ; RAW: %value = invoke i64 @may_throw()
  %value = invoke i64 @may_throw() to label %normal unwind label %unwind
normal:
  ; RAW: phi i8 [ %x, %entry ], [ poison, %dead ]
  %merged = phi i64 [ %a, %entry ], [ %value, %dead ]
  ret i64 %merged
unwind:
  %exception = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %exception
}

; A catchswitch block has no insertion point for an incoming edge cast.
define i8 @exceptional_predecessor(i64 %x) personality ptr @personality {
  ; CHECK-LABEL: @exceptional_predecessor(
  ; RAW-LABEL: @exceptional_predecessor(
  ; CHECK: catchswitch
  ; RAW: phi i64
  ; CHECK: catchpad
entry:
  %unused = invoke i64 @may_throw() to label %normal unwind label %dispatch
normal:
  ret i8 0
dispatch:
  %switch = catchswitch within none [label %handler] unwind to caller
handler:
  %merged = phi i64 [ %x, %dispatch ]
  %pad = catchpad within %switch [ptr null, i32 64, ptr null]
  %low = trunc i64 %merged to i8
  catchret from %pad to label %return
return:
  ret i8 %low
}

declare i64 @may_throw()
declare i32 @personality(...)

; The common select demand reaches its i1 condition without keeping data wide.
define i64 @select_condition(i8 %x, i8 %y) {
  ; CHECK-LABEL: @select_condition(
  ; CHECK: icmp ult i8
  ; CHECK: select i1 {{.*}}, i8 %x, i8 %y
  ; CHECK: zext i8 {{.*}} to i64
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  %condition = icmp slt i64 %a, 200
  %selected = select i1 %condition, i64 %a, i64 %b
  ret i64 %selected
}

; An unused select and its condition are discarded.
define i64 @dead_select(i8 %x, i8 %y) {
  ; RAW-LABEL: @dead_select(
  ; RAW-NEXT: ret i64 0
  %a = zext i8 %x to i64
  %b = zext i8 %y to i64
  %condition = icmp slt i64 %a, 200
  %unused = select i1 %condition, i64 %a, i64 %b
  ret i64 0
}

; Value demand is 8 but count demand is 16. Keep their transfers separate.
define i8 @shift_roles(i64 %x, i64 %y, i64 %amount) {
  ; CHECK-LABEL: @shift_roles(
  ; CHECK: add i8
  ; CHECK: shl i16
  ; CHECK: trunc i16 {{.*}} to i8
  %value = add i64 %x, %y
  %count = and i64 %amount, 15
  %shifted = shl i64 %value, %count
  %result = trunc i64 %shifted to i8
  ret i8 %result
}

; Different operand roles can refer to the same producer and must join there.
define i8 @shift_same_producer(i64 %x) {
  ; CHECK-LABEL: @shift_same_producer(
  ; CHECK: %[[VALUE:[^ ]+]] = zext i8 {{.*}} to i16
  ; CHECK: shl i16 %[[VALUE]], %[[VALUE]]
  %value = and i64 %x, 15
  %shifted = shl i64 %value, %value
  %result = trunc i64 %shifted to i8
  ret i8 %result
}

; Truncable bits and backward demand both cross operand nodes inside a cycle.
define i64 @shift_cycle(i8 %initial, i64 %n) {
  ; CHECK-LABEL: @shift_cycle(
  ; CHECK: phi i8
  ; CHECK: shl i16
  ; CHECK: zext i8 {{.*}} to i64
entry:
  %start = zext i8 %initial to i64
  br label %loop
loop:
  %value = phi i64 [ %start, %entry ], [ %next, %loop ]
  %iteration = phi i64 [ 0, %entry ], [ %increment, %loop ]
  %count = and i64 %iteration, 15
  %shifted = shl i64 %value, %count
  %next = and i64 %shifted, 255
  %increment = add i64 %iteration, 1
  %done = icmp eq i64 %increment, %n
  br i1 %done, label %exit, label %loop
exit:
  ret i64 %next
}

; Eight operands' truncable bits spill past the inline lattice storage. Their
; mixed extensions require a signed 16-bit representation for the merged value.
define i64 @many_inputs(i3 %which, i8 %x0, i8 %x1, i8 %x2, i8 %x3,
                       i8 %x4, i8 %x5, i8 %x6, i8 %x7) {
  ; CHECK-LABEL: @many_inputs(
  ; CHECK: phi i16
  ; CHECK: sext i16 {{.*}} to i64
entry:
  %v0 = zext i8 %x0 to i64
  %v1 = zext i8 %x1 to i64
  %v2 = zext i8 %x2 to i64
  %v3 = zext i8 %x3 to i64
  %v4 = zext i8 %x4 to i64
  %v5 = zext i8 %x5 to i64
  %v6 = zext i8 %x6 to i64
  %v7 = sext i8 %x7 to i64
  switch i3 %which, label %b0 [ i3 1, label %b1
                              i3 2, label %b2
                              i3 3, label %b3
                              i3 4, label %b4
                              i3 5, label %b5
                              i3 6, label %b6
                              i3 7, label %b7 ]
b0:
  br label %merge
b1:
  br label %merge
b2:
  br label %merge
b3:
  br label %merge
b4:
  br label %merge
b5:
  br label %merge
b6:
  br label %merge
b7:
  br label %merge
merge:
  %value = phi i64 [ %v0, %b0 ], [ %v1, %b1 ], [ %v2, %b2 ], [ %v3, %b3 ],
                   [ %v4, %b4 ], [ %v5, %b5 ], [ %v6, %b6 ], [ %v7, %b7 ]
  ret i64 %value
}

; The forward solver preserves a signed-byte invariant across an XOR backedge.
define i64 @signed_xor_cycle(i8 %seed, i8 %mask) {
  ; RAW-LABEL: @signed_xor_cycle(
entry:
  %start = sext i8 %seed to i64
  %wide_mask = sext i8 %mask to i64
  br label %loop
loop:
  ; RAW: phi i8
  ; RAW: xor i8
  %value = phi i64 [ %start, %entry ], [ %next, %loop ]
  %next = xor i64 %value, %wide_mask
  %continue = call i1 @again()
  br i1 %continue, label %loop, label %exit
exit:
  ; RAW: sext i8 {{.*}} to i64
  ret i64 %next
}

declare i1 @again()

; A range-aware backward transfer needs only 15 bits of the addition. LLVM
; 16's DemandedBits requests all 64 bits through this variable shift.
define i8 @bounded_shift_producer(i64 %x, i64 %y, i64 %count) {
  ; RAW-LABEL: @bounded_shift_producer(
  ; RAW: add i16
  ; RAW: lshr i16
  %value = add i64 %x, %y
  %amount = and i64 %count, 7
  %shifted = lshr i64 %value, %amount
  %low = trunc i64 %shifted to i8
  ret i8 %low
}
