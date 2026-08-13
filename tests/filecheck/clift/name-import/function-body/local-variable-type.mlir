//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %root/bin/revng pipeline run-pipe import-descriptive-info %S/../model.yml <(tar -c --transform 's;.*;/binary;' %s) /dev/stdout | %root/bin/revng clift-opt | FileCheck %s

!void = !clift.void
!int32_t = !clift.int<signed 4>

!f = !clift.func<"/type-definition/1004-CABIFunctionDefinition" : !void()>

// A local variable is identified by the addresses of the statements using it,
// which is why the type the model records for it is applied here rather than
// where the variable was created: only now do those addresses agree with the
// ones the model was written with.
#read = loc("/instruction/0x4000100c:Code_x86_64/0x4000100c:Code_x86_64/0x4000100d:Code_x86_64")
#write = loc("/instruction/0x4000100c:Code_x86_64/0x4000100c:Code_x86_64/0x4000100e:Code_x86_64")
#narrow = loc("/instruction/0x4000100c:Code_x86_64/0x4000100c:Code_x86_64/0x4000100f:Code_x86_64")

module attributes { clift.module } {
  // CHECK: clift.func @fun_0x4000100c
  clift.func @f<!f>() attributes {
    handle = "/function/0x4000100c:Code_x86_64"
  } {
    // CHECK: %0 = clift.local : !uint32_t attributes {handle = "/local-variable/0x4000100c:Code_x86_64/0", name = "retyped"}
    %0 = clift.local : !int32_t attributes {
      handle = "/local-variable/0x4000100c:Code_x86_64/0"
    }

    // The accesses stay as wide as the code made them, so a read of the
    // retyped variable reinterprets it rather than being left ill-typed.
    // CHECK: clift.expr {
    clift.expr {
      // CHECK: %2 = clift.bitcast %0 : !uint32_t -> !int32_t
      // CHECK: clift.yield %2 : !int32_t
      clift.yield %0 : !int32_t loc(#read)
    }

    // An assignment keeps the variable on the left and converts what is stored
    // into it, rather than writing through the variable's address. C converts
    // between two integers on its own, so the conversion is left implicit and
    // the assignment reads as it did before the retype.
    // CHECK: clift.expr {
    clift.expr {
      %1 = clift.undef : !int32_t loc(#write)
      // CHECK: %3 = clift.bitcast %2 {clift.implicit} : !int32_t -> !uint32_t
      // CHECK: %4 = clift.assign %0, %3 : !uint32_t
      %2 = clift.assign %0, %1 : !int32_t loc(#write)
      // CHECK: clift.yield %4 : !uint32_t
      clift.yield %2 : !int32_t loc(#write)
    }

    // A type narrower than the accesses around the variable would have them
    // reach past its end, so it is passed over: this one keeps !int32_t and,
    // with it, the name the model gives it.
    // CHECK: %1 = clift.local : !int32_t attributes {handle = "/local-variable/0x4000100c:Code_x86_64/1", name = "too_narrow"}
    %3 = clift.local : !int32_t attributes {
      handle = "/local-variable/0x4000100c:Code_x86_64/1"
    }

    // CHECK: clift.expr {
    clift.expr {
      // CHECK: clift.yield %1 : !int32_t
      clift.yield %3 : !int32_t loc(#narrow)
    }
  }
}
