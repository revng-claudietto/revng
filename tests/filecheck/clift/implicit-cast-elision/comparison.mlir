//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %root/bin/revng clift-opt %s --elide-implicit-casts | FileCheck %s

!void = !clift.void
!generic16_t = !clift.int<generic 2>
!int8_t = !clift.int<signed 1>
!uint8_t = !clift.int<unsigned 1>
!int16_t = !clift.int<signed 2>
!uint16_t = !clift.int<unsigned 2>
!int32_t = !clift.int<signed 4>
!uint32_t = !clift.int<unsigned 4>

!f = !clift.func<"/model-type/1001" : !void(!generic16_t, !int32_t, !int8_t, !uint8_t)>

module attributes {clift.module} {
  clift.func @f<!f>(%arg0 : !generic16_t, %arg1 : !int32_t, %arg2 : !int8_t, %arg3 : !uint8_t) -> !void {
    // Both operands are promoted to int either way, but the cast changes their
    // value, so a signed comparison would become unsigned.
    // CHECK: clift.expr {
    clift.expr {
      // CHECK: %0 = clift.bitcast %arg0 : !generic16_t -> !int16_t
      %0 = clift.bitcast %arg0 : !generic16_t -> !int16_t
      // CHECK: %1 = clift.bitcast %arg0 : !generic16_t -> !int16_t
      %1 = clift.bitcast %arg0 : !generic16_t -> !int16_t
      // CHECK: %2 = clift.slt %0, %1 : !int16_t
      %2 = clift.slt %0, %1 : !int16_t
      clift.yield %2 : !clift.bool
    }
    // CHECK: }

    // Without the truncation, all 32 bits would be compared.
    // CHECK: clift.expr {
    clift.expr {
      // CHECK: %0 = clift.truncate %arg1 : !int32_t -> !int8_t
      %0 = clift.truncate %arg1 : !int32_t -> !int8_t
      // CHECK: %1 = clift.truncate %arg1 : !int32_t -> !int8_t
      %1 = clift.truncate %arg1 : !int32_t -> !int8_t
      // CHECK: %2 = clift.eq %0, %1 : !int8_t
      %2 = clift.eq %0, %1 : !int8_t
      clift.yield %2 : !clift.bool
    }
    // CHECK: }

    // A negative value sign-extended to an unsigned type becomes positive.
    // CHECK: clift.expr {
    clift.expr {
      // CHECK: %0 = clift.sext %arg2 : !int8_t -> !uint16_t
      %0 = clift.sext %arg2 : !int8_t -> !uint16_t
      // CHECK: %1 = clift.sext %arg2 : !int8_t -> !uint16_t
      %1 = clift.sext %arg2 : !int8_t -> !uint16_t
      // CHECK: %2 = clift.ult %0, %1 : !uint16_t
      %2 = clift.ult %0, %1 : !uint16_t
      clift.yield %2 : !clift.bool
    }
    // CHECK: }

    // Extensions matching the signedness of their operand preserve its value.
    // CHECK: clift.expr {
    clift.expr {
      // CHECK: %0 = clift.implicit_cast %arg2 : !int8_t -> !int16_t
      %0 = clift.sext %arg2 : !int8_t -> !int16_t
      // CHECK: %1 = clift.implicit_cast %arg3 : !uint8_t -> !int16_t
      %1 = clift.zext %arg3 : !uint8_t -> !int16_t
      // CHECK: %2 = clift.slt %0, %1 : !int16_t
      %2 = clift.slt %0, %1 : !int16_t
      clift.yield %2 : !clift.bool
    }
    // CHECK: }

    // At the size of the operator type, a change of signedness is a modular
    // conversion, which C applies implicitly.
    // CHECK: clift.expr {
    clift.expr {
      // CHECK: %0 = clift.implicit_cast %arg1 : !int32_t -> !uint32_t
      %0 = clift.bitcast %arg1 : !int32_t -> !uint32_t
      // CHECK: %1 = clift.zext %arg3 : !uint8_t -> !uint32_t
      %1 = clift.zext %arg3 : !uint8_t -> !uint32_t
      // CHECK: %2 = clift.ult %0, %1 : !uint32_t
      %2 = clift.ult %0, %1 : !uint32_t
      clift.yield %2 : !clift.bool
    }
    // CHECK: }
  }
}
