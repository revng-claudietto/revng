//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %root/bin/revng clift-opt --emit-c %s -o /dev/null | FileCheck %s
// RUN: %root/bin/revng clift-opt --emit-c=ptml %s -o /dev/null | %root/bin/revng ptml | FileCheck %s
// RUN: %root/bin/revng clift-opt --emit-c=ptml %s -o /dev/null | FileCheck --check-prefix=PTML %s

!void = !clift.void
!int32_t = !clift.int<signed 4>

!f = !clift.func<"/type-definition/1004-CABIFunctionDefinition" : !void()>

module attributes { clift.module } {
  // CHECK: void fun_0x40001004(void) {
  clift.func @fun_0x40001004<!f>() attributes {
    handle = "/function/0x40001004:Code_x86_64"
  } {
    // CHECK: // hello
    // CHECK: // world
    // CHECK: 0;
    //
    // A statement carries only the comments placed on it, so the position of
    // a comment in this list is not the one it has in the model. The handle
    // is what locates it for an edit, and it is emitted as it stands.
    // PTML: statement-comment/0x40001004:Code_x86_64/3
    // PTML: statement-comment/0x40001004:Code_x86_64/7
    clift.expr {
      %0 = clift.imm 0 : !int32_t
      clift.yield %0 : !int32_t
    } attributes {
      clift.comments = [
        { handle = "/statement-comment/0x40001004:Code_x86_64/3",
          body = "hello" },
        { handle = "/statement-comment/0x40001004:Code_x86_64/7",
          body = "world" }
      ]
    }
  // CHECK: }
  }
}
