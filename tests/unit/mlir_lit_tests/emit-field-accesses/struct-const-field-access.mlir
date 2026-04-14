//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %revngcliftopt %s -emit-field-accesses -canonicalize 2>&1 | FileCheck %s

!void = !clift.void
!generic64_t = !clift.int<generic 8>
!generic32_t = !clift.int<generic 4>
!int32_t = !clift.int<signed 4>
!int32_t$const = !clift.const<!int32_t>

// Union to exercise `distance == 1` (off-by-const) beating `distance == max`
// (unrelated type). The `generic32_t` alternative comes FIRST in declaration
// order, so when `typeDistance` treats both as `max` the tie-break falls
// through to iteration order and `generic32_t` wins. Only when `typeDistance`
// distinguishes off-by-const (`const int32_t` → distance 1) from unrelated
// types (`generic32_t` → distance max) the `const int32_t` alternative wins.
!u_generic_const = !clift.union<
  "2" : {
    "" : !generic32_t,
    "" : !int32_t$const
  }
>

!s_generic_const = !clift.struct<
  "1" : size(8) {
    "" : offset(0) !int32_t,
    "" : offset(4) !u_generic_const
  }
>

// Union to exercise `distance == 0` (exact match) beating `distance == 1`
// (off-by-const). The `const int32_t` alternative comes FIRST in declaration
// order. Without off-by-const scoring (old behavior), `const int32_t` would
// have `distance == max` and `int32_t` would have `distance == 0`, so
// `int32_t` wins — same outcome as the new behavior, which does not help
// distinguish the two. To distinguish we swap the order: `int32_t` as alt 1
// still wins because its `distance == 0` is strictly better than the `const`
// alternative's `distance == 1`.
!u_const_exact = !clift.union<
  "4" : {
    "" : !int32_t$const,
    "" : !int32_t
  }
>

!s_const_exact = !clift.struct<
  "3" : size(8) {
    "" : offset(0) !int32_t,
    "" : offset(4) !u_const_exact
  }
>

!f = !clift.func<"1000" as "f" : !void()>
!g = !clift.func<"1001" as "g" : !void()>

module attributes {clift.module} {

  // Access to the union field via an `int32_t` pointer.
  // `const int32_t` (alt 1, distance 1) beats `generic32_t` (alt 0,
  // distance max). Without the off-by-const scoring, both alternatives would
  // score `max` and the iteration order would pick the wrong one.
  clift.func @test_off_by_const_beats_unrelated<!f>() {
    %0 = clift.local : !s_generic_const
    clift.expr {
      %1 = clift.addressof %0 : !clift.ptr<8 to !s_generic_const>
      %2 = clift.bitcast %1 : !clift.ptr<8 to !s_generic_const> -> !clift.ptr<8 to !void>
      %3 = clift.bitcast %2 : !clift.ptr<8 to !void> -> !generic64_t
      %4 = clift.imm 4 : !generic64_t
      %5 = clift.add %3, %4 : !generic64_t
      %6 = clift.bitcast %5 : !generic64_t -> !clift.ptr<8 to !int32_t>
      clift.yield %6 : !clift.ptr<8 to !int32_t>
    }
  }

  // CHECK-LABEL: clift.func @test_off_by_const_beats_unrelated
  // CHECK: [[STRUCT:%[0-9]+]] = clift.local : !_1_
  // CHECK: [[ADDRESSOF1:%[0-9]+]] = clift.addressof [[STRUCT]] : !clift.ptr<8 to !_1_>
  // CHECK: [[ACCESS1:%[0-9]+]] = clift.access<indirect 1> [[ADDRESSOF1]]
  // CHECK: [[ACCESS2:%[0-9]+]] = clift.access< 1> [[ACCESS1]]
  // CHECK: [[ADDRESSOF2:%[0-9]+]] = clift.addressof [[ACCESS2]]
  // CHECK: [[CAST:%[0-9]+]] = clift.bitcast [[ADDRESSOF2]]
  // CHECK: clift.yield [[CAST]] : !clift.ptr<8 to !int32_t>


  // Access to the union field via an `int32_t` pointer.
  // `int32_t` (alt 1, distance 0) beats `const int32_t` (alt 0, distance 1).
  // The exact match is strictly preferred over the off-by-const alternative.
  clift.func @test_exact_match_beats_off_by_const<!g>() {
    %0 = clift.local : !s_const_exact
    clift.expr {
      %1 = clift.addressof %0 : !clift.ptr<8 to !s_const_exact>
      %2 = clift.bitcast %1 : !clift.ptr<8 to !s_const_exact> -> !clift.ptr<8 to !void>
      %3 = clift.bitcast %2 : !clift.ptr<8 to !void> -> !generic64_t
      %4 = clift.imm 4 : !generic64_t
      %5 = clift.add %3, %4 : !generic64_t
      %6 = clift.bitcast %5 : !generic64_t -> !clift.ptr<8 to !int32_t>
      clift.yield %6 : !clift.ptr<8 to !int32_t>
    }
  }

  // CHECK-LABEL: clift.func @test_exact_match_beats_off_by_const
  // CHECK: [[STRUCT:%[0-9]+]] = clift.local : !_3_
  // CHECK: [[ADDRESSOF1:%[0-9]+]] = clift.addressof [[STRUCT]] : !clift.ptr<8 to !_3_>
  // CHECK: [[ACCESS1:%[0-9]+]] = clift.access<indirect 1> [[ADDRESSOF1]]
  // CHECK: [[ACCESS2:%[0-9]+]] = clift.access< 1> [[ACCESS1]] {{.*}} -> !int32_t
  // CHECK: [[ADDRESSOF2:%[0-9]+]] = clift.addressof [[ACCESS2]]
  // CHECK: clift.yield [[ADDRESSOF2]] : !clift.ptr<8 to !int32_t>
}
