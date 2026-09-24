//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %root/bin/revng clift-opt --emit-c %s -o /dev/null | FileCheck %s
// RUN: %root/bin/revng clift-opt --emit-c=ptml %s -o /dev/null | %root/bin/revng ptml | FileCheck %s

!void = !clift.void
!int32 = !clift.int<signed 4>

!frame = !clift.struct<
  "/type-definition/0-StructDefinition" as "frame" : size(8) {
    "/struct-field/0-StructDefinition/0" as "a" : offset(0) !int32,
    "/struct-field/0-StructDefinition/4" as "b" : offset(4) !int32
  } [#clift.c_attribute<"_SINGLETON" : "/macro/_SINGLETON">]
>

!plain = !clift.struct<
  "/type-definition/1-StructDefinition" as "plain" : size(4) {
    "/struct-field/1-StructDefinition/0" as "x" : offset(0) !int32
  }
>

!f = !clift.func<
  "/type-definition/1001-CABIFunctionDefinition" : !void()
>

module attributes {clift.module} {
  clift.func @f<!f>() attributes {
    handle = "/function/0x1000:Code_x86_64"
  } {
    // The stack-frame local gets its struct type inlined.
    //
    // CHECK:      struct _SINGLETON _PACKED _SIZE(8) frame {
    // CHECK-NEXT:   int32_t a _STARTS_AT(0);
    // CHECK-NEXT:   int32_t b _STARTS_AT(4);
    // CHECK-NEXT: } frame_var;
    %frame = clift.local : !frame attributes {
      handle = "/stack-frame-variable/0x1000:Code_x86_64",
      name = "frame_var",
      clift.stack_frame = true
    }

    // Ordinary struct types are referenced by name.
    //
    // CHECK: plain regular_var;
    %regular = clift.local : !plain attributes {
      handle = "/local-variable/0x1000:Code_x86_64",
      name = "regular_var"
    }
  }
}
