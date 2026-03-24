//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %revngcliftopt %s -emit-field-accesses -canonicalize 2>&1 | FileCheck %s

!void = !clift.primitive<void 0>
!generic64_t = !clift.primitive<generic 8>
!int32_t = !clift.primitive<signed 4>
!int32_t$ptr = !clift.ptr<8 to !int32_t>

!f = !clift.func<
  "1000" as "f" : !void(!int32_t$ptr, !generic64_t)
>

// Pointer-as-array access: *(p + i) should become p[i].
// Previously, when the BasePointer was `ptr<int32_t>`, the BaseType was
// `int32_t` (a primitive leaf with no arrays or fields), so the traversal
// analyzer found no Traversals and the pass left the expression unchanged.
//
// With the `deriveBaseType` change, `ptr<int32_t>` now produces
// BaseType = `array<(1<<32) x int32_t>`, enabling the traversal analyzer
// to find an array traversal that matches the stride=4 LinearCombination.

module attributes {clift.module} {
  clift.func @test_pointer_as_array<!f>(%arg0 : !int32_t$ptr, %arg1 : !generic64_t) {
    clift.expr {
      %0 = clift.cast<bitcast> %arg0 : !int32_t$ptr -> !generic64_t
      %1 = clift.imm 4 : !generic64_t
      %2 = clift.mul %arg1, %1 : !generic64_t
      %3 = clift.add %0, %2 : !generic64_t
      %4 = clift.cast<bitcast> %3 : !generic64_t -> !int32_t$ptr
      clift.yield %4 : !int32_t$ptr
    }
  }

  // CHECK: clift.func @test_pointer_as_array<!f>([[PTR:%[a-z0-9]+]]: {{.*}}, [[IDX:%[a-z0-9]+]]: {{.*}})
  // CHECK: [[SUB:%[0-9]+]] = clift.subscript [[PTR]], [[IDX]]
  // CHECK: [[ADDR:%[0-9]+]] = clift.addressof [[SUB]]
  // CHECK: clift.yield [[ADDR]] : !clift.ptr<8 to !int32_t>
}
