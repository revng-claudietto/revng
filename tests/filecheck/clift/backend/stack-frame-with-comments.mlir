//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %root/bin/revng clift-opt --emit-c %s -o /dev/null | FileCheck %s
// RUN: %root/bin/revng clift-opt --emit-c=ptml %s -o /dev/null | %root/bin/revng ptml | FileCheck %s

!void = !clift.void
!int32 = !clift.int<signed 4>

// Preserve comments on the singleton type, its fields and its variable.
!frame = !clift.struct<
  "/type-definition/0-StructDefinition" as "frame" : size(8) {
    "/struct-field/0-StructDefinition/0" as "a" : offset(0) !int32
      comment "comment for field a",
    "/struct-field/0-StructDefinition/4" as "b" : offset(4) !int32
      comment "comment for field b"
  } [#clift.c_attribute<"_SINGLETON" : "/macro/_SINGLETON">] comment "struct-level comment for the frame">

!f = !clift.func<
  "/type-definition/1001-CABIFunctionDefinition" : !void()
>

module attributes {clift.module} {
  clift.func @f<!f>() attributes {
    handle = "/function/0x1000:Code_x86_64"
  } {
    // The stack-frame local gets its struct type inlined, with all three
    // comment surfaces preserved: the struct-level Doxygen comment, both
    // per-field Doxygen comments, and the local variable's `clift.comments`
    // list. The local comment is rendered before the struct definition
    // because the inlined emission is anchored by the local declaration.
    //
    // CHECK:      // variable comment
    // CHECK:      /// struct-level comment for the frame
    // CHECK-NEXT: struct _SINGLETON _PACKED _SIZE(8) frame {
    // CHECK:   /// comment for field a
    // CHECK-NEXT:   int32_t a _STARTS_AT(0);
    // CHECK:   /// comment for field b
    // CHECK-NEXT:   int32_t b _STARTS_AT(4);
    // CHECK-NEXT: } frame_var;
    //
    %frame = clift.local : !frame attributes {
      handle = "/stack-frame-variable/0x1000:Code_x86_64",
      name = "frame_var",
      clift.stack_frame = true,
      clift.comments = ["variable comment"]
    }
  }
}
