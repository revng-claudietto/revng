//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %revngcliftopt %s -emit-field-accesses -canonicalize 2>&1 | FileCheck %s

!void = !clift.primitive<void 0>
!generic64_t = !clift.primitive<generic 8>
!int16_t = !clift.primitive<signed 2>
!int32_t = !clift.primitive<signed 4>
!int64_t = !clift.primitive<signed 8>
!int16_t$ptr = !clift.ptr<8 to !int16_t>
!int32_t$ptr = !clift.ptr<8 to !int32_t>
!int64_t$ptr = !clift.ptr<8 to !int64_t>

!f1 = !clift.func<"1000" as "f1" : !void(!int32_t$ptr)>
!f2 = !clift.func<"1001" as "f2" : !void(!int32_t$ptr, !generic64_t)>
!f3 = !clift.func<"1002" as "f3" : !void(!int16_t$ptr, !generic64_t)>
!f4 = !clift.func<"1003" as "f4" : !void(!int64_t$ptr, !generic64_t)>

module attributes {clift.module} {

  // Constant index: *(p + 3) → p[3]
  // The BaseOffset is 12 (3 * sizeof(int32_t)), no LinearCombination.
  // getExplicitArithmetic divides BaseOffset by Stride to get
  // IndexConstantComponent = 3.
  clift.func @test_constant_index<!f1>(%arg0 : !int32_t$ptr) {
    clift.expr {
      %0 = clift.cast<bitcast> %arg0 : !int32_t$ptr -> !generic64_t
      %1 = clift.imm 12 : !generic64_t
      %2 = clift.add %0, %1 : !generic64_t
      %3 = clift.cast<bitcast> %2 : !generic64_t -> !int32_t$ptr
      clift.yield %3 : !int32_t$ptr
    }
  }

  // CHECK: clift.func @test_constant_index<!f1_>([[PTR:%[a-z0-9]+]]: {{.*}})
  // CHECK: [[IMM:%[0-9]+]] = clift.imm 3
  // CHECK: [[SUB:%[0-9]+]] = clift.subscript [[PTR]], [[IMM]]
  // CHECK: [[ADDR:%[0-9]+]] = clift.addressof [[SUB]]
  // CHECK: clift.yield [[ADDR]] : !clift.ptr<8 to !int32_t>

  // Variable + constant index: *(p + i + 2) → p[i + 2]
  // PA has BaseOffset=8 (2 * 4) and LinearCombination=[Stride=4, Variable=i].
  // getExplicitArithmetic produces IndexConstantComponent=2 (from BaseOffset/4)
  // and IndexVariableComponent=i (from the stride match). The replacement emits
  // subscript with (i + 2) as the combined index.
  clift.func @test_variable_plus_constant<!f2>(%arg0 : !int32_t$ptr, %arg1 : !generic64_t) {
    clift.expr {
      %0 = clift.cast<bitcast> %arg0 : !int32_t$ptr -> !generic64_t
      %1 = clift.imm 4 : !generic64_t
      %2 = clift.mul %arg1, %1 : !generic64_t
      %3 = clift.imm 8 : !generic64_t
      %4 = clift.add %2, %3 : !generic64_t
      %5 = clift.add %0, %4 : !generic64_t
      %6 = clift.cast<bitcast> %5 : !generic64_t -> !int32_t$ptr
      clift.yield %6 : !int32_t$ptr
    }
  }

  // CHECK: clift.func @test_variable_plus_constant<!f2_>({{[^)]*}}[[PTR:%[a-z0-9]+]]: {{[^,]*}}, [[IDX:%[a-z0-9]+]]: {{.*}})
  // CHECK: [[CONST:%[0-9]+]] = clift.imm 2
  // CHECK: [[ADD:%[0-9]+]] = clift.add [[CONST]], [[IDX]]
  // CHECK: [[SUB:%[0-9]+]] = clift.subscript [[PTR]], [[ADD]]
  // CHECK: [[ADDR:%[0-9]+]] = clift.addressof [[SUB]]
  // CHECK: clift.yield [[ADDR]] : !clift.ptr<8 to !int32_t>

  // int16_t pointer with stride=2 via shift: *(p + i) where sizeof(*p)=2
  // PA has LinearCombination=[Stride=2, Variable=i] (from i << 1).
  // deriveBaseType wraps int16_t in array<1 x int16_t> with Stride=2.
  clift.func @test_int16_pointer<!f3>(%arg0 : !int16_t$ptr, %arg1 : !generic64_t) {
    clift.expr {
      %0 = clift.cast<bitcast> %arg0 : !int16_t$ptr -> !generic64_t
      %1 = clift.imm 1 : !generic64_t
      %2 = clift.shl %arg1, %1 : !generic64_t
      %3 = clift.add %0, %2 : !generic64_t
      %4 = clift.cast<bitcast> %3 : !generic64_t -> !int16_t$ptr
      clift.yield %4 : !int16_t$ptr
    }
  }

  // CHECK: clift.func @test_int16_pointer<!f3_>({{[^)]*}}[[PTR:%[a-z0-9]+]]: {{[^,]*}}, [[IDX:%[a-z0-9]+]]: {{.*}})
  // CHECK: [[SUB:%[0-9]+]] = clift.subscript [[PTR]], [[IDX]]
  // CHECK: [[ADDR:%[0-9]+]] = clift.addressof [[SUB]]
  // CHECK: clift.yield [[ADDR]] : !clift.ptr<8 to !int16_t>

  // int64_t pointer with stride=8 via shift: *(p + i) where sizeof(*p)=8
  // PA has LinearCombination=[Stride=8, Variable=i] (from i << 3).
  clift.func @test_int64_pointer<!f4>(%arg0 : !int64_t$ptr, %arg1 : !generic64_t) {
    clift.expr {
      %0 = clift.cast<bitcast> %arg0 : !int64_t$ptr -> !generic64_t
      %1 = clift.imm 3 : !generic64_t
      %2 = clift.shl %arg1, %1 : !generic64_t
      %3 = clift.add %0, %2 : !generic64_t
      %4 = clift.cast<bitcast> %3 : !generic64_t -> !int64_t$ptr
      clift.yield %4 : !int64_t$ptr
    }
  }

  // CHECK: clift.func @test_int64_pointer<!f4_>({{[^)]*}}[[PTR:%[a-z0-9]+]]: {{[^,]*}}, [[IDX:%[a-z0-9]+]]: {{.*}})
  // CHECK: [[SUB:%[0-9]+]] = clift.subscript [[PTR]], [[IDX]]
  // CHECK: [[ADDR:%[0-9]+]] = clift.addressof [[SUB]]
  // CHECK: clift.yield [[ADDR]] : !clift.ptr<8 to !int64_t>

  // ptr_add on non-struct pointer: ptr_add %ptr, %idx
  // Exercises the composePtrAdd + pointer-as-array combination.
  // ptr_add scales by PointeeSize, producing Stride=4 in the PA,
  // which matches the implicit array<1 x int32_t>.
  clift.func @test_ptradd_pointer_as_array<!f2>(%arg0 : !int32_t$ptr, %arg1 : !generic64_t) {
    clift.expr {
      %0 = clift.ptr_add %arg0, %arg1 : (!int32_t$ptr, !generic64_t)
      clift.yield %0 : !int32_t$ptr
    }
  }

  // CHECK: clift.func @test_ptradd_pointer_as_array<!f2_>({{[^)]*}}[[PTR:%[a-z0-9]+]]: {{[^,]*}}, [[IDX:%[a-z0-9]+]]: {{.*}})
  // CHECK: [[SUB:%[0-9]+]] = clift.subscript [[PTR]], [[IDX]]
  // CHECK: [[ADDR:%[0-9]+]] = clift.addressof [[SUB]]
  // CHECK: clift.yield [[ADDR]] : !clift.ptr<8 to !int32_t>
}
