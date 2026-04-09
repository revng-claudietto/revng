//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %revngcliftopt %s -emit-field-accesses -canonicalize 2>&1 | FileCheck %s

!void = !clift.void
!generic64_t = !clift.int<generic 8>
!generic32_t = !clift.int<generic 4>
!int32_t = !clift.int<signed 4>
!int32_t$const = !clift.const<!int32_t>

!u = !clift.union<
  "2" : {
    "" : !int32_t$const,
    "" : !generic32_t
  }
>

!s = !clift.struct<
  "1" : size(8) {
    "" : offset(0) !int32_t,
    "" : offset(4) !u
  }
>

!f = !clift.func<"1000" as "f" : !void()>

module attributes {clift.module} {

  // Access to the union field via an `int32_t` pointer.
  // Since we are equiparing `const` and non-`const` in the typedistance, the
  // `const` alternative is selected.
  clift.func @test_const_union_field_access<!f>() {
    %0 = clift.local : !s
    clift.expr {
      %1 = clift.addressof %0 : !clift.ptr<8 to !s>
      %2 = clift.bitcast %1 : !clift.ptr<8 to !s> -> !clift.ptr<8 to !void>
      %3 = clift.bitcast %2 : !clift.ptr<8 to !void> -> !generic64_t
      %4 = clift.imm 4 : !generic64_t
      %5 = clift.add %3, %4 : !generic64_t
      %6 = clift.bitcast %5 : !generic64_t -> !clift.ptr<8 to !int32_t>
      clift.yield %6 : !clift.ptr<8 to !int32_t>
    }
  }


  // CHECK-LABEL: clift.func @test_const_union_field_access
  // CHECK: [[STRUCT:%[0-9]+]] = clift.local : !_1_
  // CHECK: [[ADDRESSOF1:%[0-9]+]] = clift.addressof [[STRUCT]] : !clift.ptr<8 to !_1_>
  // CHECK: [[ACCESS1:%[0-9]+]] = clift.access<indirect 1> [[ADDRESSOF1]]
  // CHECK: [[ACCESS2:%[0-9]+]] = clift.access< 0> [[ACCESS1]]
  // CHECK: [[ADDRESSOF2:%[0-9]+]] = clift.addressof [[ACCESS2]]
  // CHECK: [[CAST:%[0-9]+]] = clift.bitcast [[ADDRESSOF2]]
  // CHECK: clift.yield [[CAST]] : !clift.ptr<8 to !int32_t>
}
