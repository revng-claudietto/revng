//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %root/bin/revng clift-opt --emit-type-and-global-header --emit-c %s -o /dev/null > %t.c
// RUN: FileCheck %s --implicit-check-not='typedef struct _PACKED singleton_' < %t.c
// RUN: clang -x c -std=c11 -Werror -Wno-pragma-once-outside-header -c -I%root/share/revng/include %t.c -o /dev/null
// RUN: %root/bin/revng clift-opt --emit-type-and-global-header=ptml %s -o /dev/null | %root/bin/revng ptml > %t.ptml.c
// RUN: %root/bin/revng clift-opt --emit-c=ptml %s -o /dev/null | %root/bin/revng ptml >> %t.ptml.c
// RUN: FileCheck %s --implicit-check-not='typedef struct _PACKED singleton_' < %t.ptml.c

!void = !clift.void
!int32 = !clift.int<signed 4>

// An ordinary type used inside both singleton trees must precede their uses.
// Its const qualifier belongs on the fields, not on its standalone definition.
// CHECK: typedef struct _PACKED object object;
// CHECK: struct _PACKED _SIZE(4) object {
// CHECK-NEXT: int32_t value _STARTS_AT(0);
// CHECK-NEXT: };
!object = !clift.const<!clift.struct<
  "/type-definition/0-StructDefinition" as "object" : size(4) {
    "/struct-field/0-StructDefinition/0" as "value" : offset(0) !int32
  }
>>

!section = !clift.struct<
  "/type-definition/1-StructDefinition" as "singleton_section" : size(8) {
    "/struct-field/1-StructDefinition/0" as "data" : offset(0) !object
  } [#clift.c_attribute<"_SINGLETON" : "/macro/_SINGLETON">]
  comment "A singleton section."
>

!segment = !clift.struct<
  "/type-definition/2-StructDefinition" as "singleton_segment" : size(16) {
    "/struct-field/2-StructDefinition/4" as "section" : offset(4) !clift.const<!section>
      comment "A read-only section."
  } [#clift.c_attribute<"_SINGLETON" : "/macro/_SINGLETON">]
>

!nested_frame = !clift.struct<
  "/type-definition/3-StructDefinition" as "singleton_nested_frame" : size(4) {
    "/struct-field/3-StructDefinition/0" as "data" : offset(0) !object
  } [#clift.c_attribute<"_SINGLETON" : "/macro/_SINGLETON">]
>

!frame = !clift.struct<
  "/type-definition/4-StructDefinition" as "singleton_frame" : size(4) {
    "/struct-field/4-StructDefinition/0" as "nested" : offset(0) !nested_frame
  } [#clift.c_attribute<"_SINGLETON" : "/macro/_SINGLETON">]
>

!f = !clift.func<
  "/type-definition/1001-CABIFunctionDefinition" as "prototype" : !void()
>

// None of the singletons have a separate declaration or definition.
// CHECK-NOT: singleton_
// CHECK: void f(void);
// CHECK-NOT: singleton_
// CHECK: struct _SINGLETON _PACKED _SIZE(16) singleton_segment {
// CHECK-NEXT: uint8_t padding_at_0[4];
// CHECK: /// A read-only section.
// CHECK: /// A singleton section.
// CHECK-NEXT: const struct _SINGLETON _PACKED _SIZE(8) singleton_section {
// CHECK-NEXT: const object data _STARTS_AT(0);
// CHECK-NEXT: uint8_t padding_at_4[4];
// CHECK-NEXT: } section _STARTS_AT(4);
// CHECK-NEXT: uint8_t padding_at_12[4];
// CHECK-NEXT: } segment;
// CHECK-NOT: singleton_
// CHECK: void f(void) {
// CHECK-NEXT: struct _SINGLETON _PACKED _SIZE(4) singleton_frame {
// CHECK-NEXT: struct _SINGLETON _PACKED _SIZE(4) singleton_nested_frame {
// CHECK-NEXT: const object data _STARTS_AT(0);
// CHECK-NEXT: } nested _STARTS_AT(0);
// CHECK-NEXT: } frame_var = bit_cast(struct singleton_frame, 0);
// CHECK-NEXT: struct singleton_frame *frame_ptr = &frame_var;
// CHECK-NEXT: }
// CHECK-NOT: singleton_

module attributes {clift.module, clift.types = [!frame, !segment, !object]} {
  clift.global @segment : !segment attributes {
    handle = "/segment/0x400000:Generic64"
  }

  clift.func @f<!f>() attributes {
    handle = "/function/0x1000:Code_x86_64"
  } {
    // Inlining follows the type, without requiring a stack-frame tag.
    %frame = clift.local : !frame = {
      %zero = clift.imm 0 : !int32
      %initial = clift.bitcast %zero : !int32 -> !frame
      clift.yield %initial : !frame
    } attributes {
      handle = "/stack-frame-variable/0x1000:Code_x86_64",
      name = "frame_var"
    }
    %pointer = clift.local : !clift.ptr<8 to !frame> = {
      %address = clift.addressof %frame : !clift.ptr<8 to !frame>
      clift.yield %address : !clift.ptr<8 to !frame>
    } attributes {
      handle = "/local-variable/0x1000:Code_x86_64/0",
      name = "frame_ptr"
    }
  }
}
