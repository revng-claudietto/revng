//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %root/bin/revng pipeline run-pipe import-descriptive-info %S/../model.yml <(tar -c --transform 's;.*;/binary;' %s) /dev/stdout | %root/bin/revng clift-opt | FileCheck %s

!void = !clift.void
!int32_t = !clift.int<signed 4>

!f = !clift.func<"/type-definition/1004-CABIFunctionDefinition" : !void()>

// The address of the branch a jump was lifted from identifies it, so a comment
// located there is placed on it.
#unshared = loc("/instruction/0x40001009:Code_x86_64/0x40001009:Code_x86_64/0x4000100a:Code_x86_64")

// The conditional branch closing a loop lifts to one break and one continue,
// which therefore share an address and cannot be told apart by it. The comment
// the model holds for that address is placed on neither: the two CHECKs below
// require the jumps to end their line, with no attributes attached.
#shared = loc("/instruction/0x40001009:Code_x86_64/0x40001009:Code_x86_64/0x4000100b:Code_x86_64")

module attributes { clift.module } {
  // CHECK: clift.func @fun_0x40001009
  clift.func @f<!f>() attributes {
    handle = "/function/0x40001009:Code_x86_64"
  } {
    clift.for body {
      // CHECK: clift.break_to {clift.comments = [{body = "on the lone break", handle = "/statement-comment/0x40001009:Code_x86_64/0"}]}
      clift.break_to loc(#unshared)
    }

    clift.for body {
      clift.if {
        %0 = clift.imm 0 : !int32_t
        clift.yield %0 : !int32_t
      } then {
        // CHECK: clift.break_to{{$}}
        clift.break_to loc(#shared)
      } else {
        // CHECK: clift.continue_to{{$}}
        clift.continue_to loc(#shared)
      }
    }
  }
}
