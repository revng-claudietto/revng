//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %root/bin/revng clift-opt --emit-c %s -o /dev/null | FileCheck %s
// RUN: %root/bin/revng clift-opt --emit-c=ptml %s -o /dev/null | %root/bin/revng ptml | FileCheck %s

!void = !clift.void
!int32 = !clift.int<signed 4>

!inner = !clift.struct<
  "/type-definition/0-StructDefinition" as "inner" : size(4) {
    "/struct-field/0-StructDefinition/0" as "x" : offset(0) !int32
  }
>

!ptr = !clift.ptr<8 to !inner>

!f = !clift.func<
  "/type-definition/1001-CABIFunctionDefinition" : !void()
>

module attributes {clift.module} {
  clift.func @f<!f>() attributes {
    handle = "/function/0x1000:Code_x86_64"
  } {
    // CHECK:  inner *frame_var;
    %ptr_v = clift.local : !ptr attributes {
      handle = "/stack-frame-variable/0x1000:Code_x86_64",
      name = "frame_var",
      clift.stack_frame = true
    }
  }
}
