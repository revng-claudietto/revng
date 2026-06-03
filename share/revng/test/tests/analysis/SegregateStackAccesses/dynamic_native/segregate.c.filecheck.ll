;
; This file is distributed under the MIT License. See LICENSE.md for details.
;

CHECK-x86_64: define i64 @local_raw_primitives_on_registers(i64 %[[ARG1:.*]], i64 %[[ARG2:.*]]) [[IGN:.*]] {
CHECK-x86_64-DAG: add i64 [[IGN:.*]]%[[ARG1]]
CHECK-x86_64-DAG: add i64 [[IGN:.*]]%[[ARG2]]
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_raw_primitives_on_registers() [[IGN:.*]] {
CHECK-x86_64-DAG:   = call i64 @local_raw_primitives_on_registers(i64 2, i64 1)
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_raw_pointers_on_registers(i64 %[[ARG1:.*]], i64 %[[ARG2:.*]]) [[IGN:.*]] {
CHECK-x86_64-DAG: %[[ARG1_PTR:.*]] = inttoptr i64 %[[ARG1]] to ptr
CHECK-x86_64-DAG: load i64, ptr %[[ARG1_PTR]]
CHECK-x86_64-DAG: %[[ARG2_PTR:.*]] = inttoptr i64 %[[ARG2]] to ptr
CHECK-x86_64-DAG: load i64, ptr %[[ARG2_PTR]]
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_raw_pointers_on_registers() [[IGN:.*]] {
CHECK-x86_64-DAG:   = call i64 @local_raw_pointers_on_registers(i64 [[ARG:.*]], i64 [[ARG]])
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_raw_primitives_on_stack(i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[STACK_ARG:.*]]) [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[STACK_ARG8:.*]] = add i64 %[[STACK_ARG]], 8
CHECK-x86_64-DAG:   %[[STACK_ARG8_PTR:.*]] = inttoptr i64 %[[STACK_ARG8]] to ptr
CHECK-x86_64-DAG:   load i64, ptr %[[STACK_ARG8_PTR]]
CHECK-x86_64-DAG:   %[[STACK_ARG_PTR:.*]] = inttoptr i64 %[[STACK_ARG]] to ptr
CHECK-x86_64-DAG:   load i64, ptr %[[STACK_ARG_PTR]]
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_raw_primitives_on_stack() [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[STACK:.*]] = alloca [16 x i8]
CHECK-x86_64-DAG:   %[[STACK_INT:.*]] = ptrtoint ptr %[[STACK]] to i64
CHECK-x86_64-DAG:   %[[STACK_INT_8:.*]] = add i64 %[[STACK_INT]], 8
CHECK-x86_64-DAG:   %[[STACK_8:.*]] = inttoptr i64 %[[STACK_INT_8]] to ptr
CHECK-x86_64-DAG:   store i64 8, ptr %[[STACK_8]]
CHECK-x86_64-DAG:   store i64 7, ptr %[[STACK]]
CHECK-x86_64-DAG:   = call i64 @local_raw_primitives_on_stack(i64 4, i64 3, i64 2, i64 1, i64 5, i64 6, i64 %[[STACK_INT]])
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_cabi_primitives_on_registers(i64 %[[ARG1:.*]], i64 %[[ARG2:.*]]) [[IGN:.*]] {
CHECK-x86_64-DAG: add i64 [[IGN:.*]]%[[ARG1]]
CHECK-x86_64-DAG: add i64 [[IGN:.*]]%[[ARG2]]
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_cabi_primitives_on_registers() [[IGN:.*]] {
CHECK-x86_64-DAG:   = call i64 @local_cabi_primitives_on_registers(i64 1, i64 2)
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_cabi_primitives_on_stack(i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[STACK_ARG1:.*]], i64 %[[STACK_ARG2:.*]]) [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[IGN:.*]] = add i64 %[[IGN:.*]]%[[STACK_ARG1]]
CHECK-x86_64-DAG:   %[[IGN:.*]] = add i64 %[[IGN:.*]]%[[STACK_ARG2]]
CHECK-x86_64: }

// WIP: we get undef here, wrong!
CHECK-x86_64: define i64 @local_call_cabi_primitives_on_stack() [[IGN:.*]] {
CHECK-x86_64-DAG:   = call i64 @local_cabi_primitives_on_stack(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 [[SCALAR1:.*]], i64 [[SCALAR2:.*]])
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_cabi_aggregate_on_registers(i64 %[[ARG1:.*]]) [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[FIELD1_PTR:.*]] = inttoptr i64 %[[ARG1]] to ptr
CHECK-x86_64-DAG:   load i64, ptr %[[FIELD1_PTR]]
CHECK-x86_64-DAG:   %[[FIELD2_ADDR:.*]] = add i64 %[[ARG1]], 8
CHECK-x86_64-DAG:   %[[FIELD2_PTR:.*]] = inttoptr i64 %[[FIELD2_ADDR]] to ptr
CHECK-x86_64-DAG:   load i64, ptr %[[FIELD2_PTR]], align 8
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_cabi_aggregate_on_registers() [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[STACK:.*]] = alloca [16 x i8]
CHECK-x86_64-DAG:   %[[STACK_INT:.*]] = ptrtoint ptr %[[STACK]] to i64
CHECK-x86_64-DAG:   store i64 1, ptr %[[STACK]]
CHECK-x86_64-DAG:   %[[STACK_INT_8:.*]] = add i64 %[[STACK_INT]], 8
CHECK-x86_64-DAG:   %[[STACK_8:.*]] = inttoptr i64 %[[STACK_INT_8]] to ptr
CHECK-x86_64-DAG:   store i64 2, ptr %[[STACK_8]]
CHECK-x86_64-DAG:   = call i64 @local_cabi_aggregate_on_registers(i64 %[[STACK_INT]])
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_cabi_aggregate_on_stack(i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[STACK_ARG:.*]]) [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[FIELD1_PTR:.*]] = inttoptr i64 %[[STACK_ARG]] to ptr
CHECK-x86_64-DAG:   load i64, ptr %[[FIELD1_PTR]]
CHECK-x86_64-DAG:   %[[FIELD2_ADDR:.*]] = add i64 %[[STACK_ARG]], 8
CHECK-x86_64-DAG:   %[[FIELD2_PTR:.*]] = inttoptr i64 %[[FIELD2_ADDR]] to ptr
CHECK-x86_64-DAG:   load i64, ptr %[[FIELD2_PTR]]
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_cabi_aggregate_on_stack() [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[STACK:.*]] = alloca [16 x i8]
CHECK-x86_64-DAG:   %[[STACK_INT:.*]] = ptrtoint ptr %[[STACK]] to i64
CHECK-x86_64-DAG:   store i64 1, ptr %[[STACK]]
CHECK-x86_64-DAG:   %[[STACK_INT_8:.*]] = add i64 %[[STACK_INT]], 8
CHECK-x86_64-DAG:   %[[STACK_8:.*]] = inttoptr i64 %[[STACK_INT_8]] to ptr
CHECK-x86_64-DAG:   store i64 2, ptr %[[STACK_8]]
CHECK-x86_64-DAG:   = call i64 @local_cabi_aggregate_on_stack(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 %[[STACK_INT]])
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_cabi_aggregate_on_stack_and_registers(i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[IGN:.*]], i64 %[[STACK_ARG:.*]]) [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[FIELD1_PTR:.*]] = inttoptr i64 %[[STACK_ARG]] to ptr
CHECK-x86_64-DAG:   load i64, ptr %[[FIELD1_PTR]]
CHECK-x86_64-DAG:   %[[FIELD2_ADDR:.*]] = add i64 %[[STACK_ARG]], 8
CHECK-x86_64-DAG:   %[[FIELD2_PTR:.*]] = inttoptr i64 %[[FIELD2_ADDR]] to ptr
CHECK-x86_64-DAG:   load i64, ptr %[[FIELD2_PTR]]
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_cabi_aggregate_on_stack_and_registers() [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[STACK:.*]] = alloca [16 x i8]
CHECK-x86_64-DAG:   %[[STACK_INT:.*]] = ptrtoint ptr %[[STACK]] to i64
CHECK-x86_64-DAG:   store i64 1, ptr %[[STACK]]
CHECK-x86_64-DAG:   %[[STACK_INT_8:.*]] = add i64 %[[STACK_INT]], 8
CHECK-x86_64-DAG:   %[[STACK_8:.*]] = inttoptr i64 %[[STACK_INT_8]] to ptr
CHECK-x86_64-DAG:   store i64 2, ptr %[[STACK_8]]
CHECK-x86_64-DAG:   = call i64 @local_cabi_aggregate_on_stack_and_registers(i64 1, i64 2, i64 3, i64 4, i64 5, i64 %[[STACK_INT]])
CHECK-x86_64: }

CHECK-x86_64: define <{ i64, i64 }> @local_raw_return_small_aggregate() [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[RESULT:.*]] = call <{ i64, i64 }> @struct_initializer(i64 124, i64 123)
CHECK-x86_64-DAG:   ret <{ i64, i64 }> %[[RESULT]]
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_raw_return_small_aggregate() [[IGN:.*]] {
CHECK-x86_64:   %[[RESULT:.*]] = call <{ i64, i64 }> @local_raw_return_small_aggregate()
CHECK-x86_64-DAG:   call i64 @OpaqueExtractvalue(<{ i64, i64 }> %[[RESULT]], i64 1)
CHECK-x86_64: }

CHECK-x86_64: define [16 x i8] @local_cabi_return_small_aggregate() [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA:.*]] = alloca [16 x i8]
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_INT:.*]] = ptrtoint ptr %[[RETURN_ALLOCA]] to i64
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_INT_8:.*]] = add i64 %[[RETURN_ALLOCA_INT]], 8
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_8:.*]] = inttoptr i64 %[[RETURN_ALLOCA_INT_8]] to ptr
CHECK-x86_64-DAG:   store i64 124, ptr %[[RETURN_ALLOCA]]
CHECK-x86_64-DAG:   store i64 123, ptr %[[RETURN_ALLOCA_8]]
CHECK-x86_64-DAG:   %[[TO_RETURN:.*]] = load [16 x i8], ptr %[[RETURN_ALLOCA]]
CHECK-x86_64-DAG:   ret [16 x i8] %[[TO_RETURN]]
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_cabi_return_small_aggregate() [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA:.*]] = alloca [16 x i8]
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_INT:.*]] = ptrtoint ptr %[[RETURN_ALLOCA]] to i64
CHECK-x86_64-DAG:   %[[RETURN_VALUE:.*]] = call [16 x i8] @local_cabi_return_small_aggregate()
CHECK-x86_64-DAG:   store [16 x i8] %[[RETURN_VALUE]], ptr %[[RETURN_ALLOCA]]
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_INT_8:.*]] = add i64 %[[RETURN_ALLOCA_INT]], 8
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_8:.*]] = inttoptr i64 %[[RETURN_ALLOCA_INT_8]] to ptr
CHECK-x86_64-DAG:   %[[TO_RETURN:.*]] = load i64, ptr %[[RETURN_ALLOCA_8]]
CHECK-x86_64: }

CHECK-x86_64: define [64 x i8] @local_cabi_return_big_aggregate() [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA:.*]] = alloca [64 x i8]
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_INT:.*]] = ptrtoint ptr %[[RETURN_ALLOCA]] to i64
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_INT_16:.*]] = add i64 %[[RETURN_ALLOCA_INT]], 16
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_16:.*]] = inttoptr i64 %[[RETURN_ALLOCA_INT_16]] to ptr
CHECK-x86_64-DAG:   store i64 123, ptr %[[RETURN_ALLOCA_16]]
CHECK-x86_64-DAG:   %[[TO_RETURN:.*]] = load [64 x i8], ptr %[[RETURN_ALLOCA]]
CHECK-x86_64-DAG:   ret [64 x i8] %[[TO_RETURN]]
CHECK-x86_64: }

CHECK-x86_64: define i64 @local_call_cabi_return_big_aggregate() [[IGN:.*]] {
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA:.*]] = alloca [64 x i8]
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_INT:.*]] = ptrtoint ptr %[[RETURN_ALLOCA]] to i64
CHECK-x86_64-DAG:   %[[RETURN_VALUE:.*]] = call [64 x i8] @local_cabi_return_big_aggregate()
CHECK-x86_64-DAG:   store [64 x i8] %[[RETURN_VALUE]], ptr %[[RETURN_ALLOCA]]
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_INT_16:.*]] = add i64 %[[RETURN_ALLOCA_INT]], 16
CHECK-x86_64-DAG:   %[[RETURN_ALLOCA_16:.*]] = inttoptr i64 %[[RETURN_ALLOCA_INT_16]] to ptr
CHECK-x86_64-DAG:   %[[TO_RETURN:.*]] = load i64, ptr %[[RETURN_ALLOCA_16]]
CHECK-x86_64: }

;; --- i386 (SystemV_x86, intptr_t == i32) patterns ---
;
; This file is distributed under the MIT License. See LICENSE.md for details.
;
; FileCheck patterns for the i386 (SystemV_x86) lowering of segregate.c, with
; intptr_t (== i32) primitives.  Only cabi_* functions are checked; raw_* are
; deliberately omitted.
;
; Each pattern traces the dataflow from the function arguments to the value
; reaching the final `ret`.  Loads of absolute addresses (the global Unknown)
; are deliberately not anchored — they appear as wildcards in the add chain.
;
; The post-pipeline applied to the segregate-stack-accesses artifact is
;   revng opt -remove-llvmassume-calls -instcombine-noarrays -sroa-noarrays
; The -noarrays variants prevent LLVM from decomposing `load/store [N x i8]`
; into N-element byte chains, which we need to keep for our analyses.
;

; ---- cabi_primitives_on_registers ------------------------------------------
; ret = (Unknown + A) + B
CHECK-i386: define i32 @local_cabi_primitives_on_registers(i32 %[[A:.*]], i32 %[[B:.*]]) [[IGN:.*]] {
CHECK-i386:         %[[T1:.*]] = add i32 {{.*}}, %[[A]]
CHECK-i386:         %[[T2:.*]] = add i32 %[[T1]], %[[B]]
CHECK-i386:         ret i32 %[[T2]]
CHECK-i386:       }

CHECK-i386: define i32 @local_call_cabi_primitives_on_registers() [[IGN:.*]] {
CHECK-i386:         %[[CALL:.*]] = call i32 @local_cabi_primitives_on_registers(i32 1, i32 2)
CHECK-i386:         %[[RET:.*]] = add i32 %[[CALL]], {{.*}}
CHECK-i386:         ret i32 %[[RET]]
CHECK-i386:       }

; ---- cabi_primitives_on_stack ----------------------------------------------
; ret = (Unknown + G) + H
CHECK-i386: define i32 @local_cabi_primitives_on_stack(i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[G:.*]], i32 %[[H:.*]]) [[IGN:.*]] {
CHECK-i386:         %[[T1:.*]] = add i32 {{.*}}, %[[G]]
CHECK-i386:         %[[T2:.*]] = add i32 %[[T1]], %[[H]]
CHECK-i386:         ret i32 %[[T2]]
CHECK-i386:       }

CHECK-i386: define i32 @local_call_cabi_primitives_on_stack() [[IGN:.*]] {
CHECK-i386:         %[[CALL:.*]] = call i32 @local_cabi_primitives_on_stack(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8)
CHECK-i386:         %[[RET:.*]] = add i32 %[[CALL]], {{.*}}
CHECK-i386:         ret i32 %[[RET]]
CHECK-i386:       }

; ---- cabi_aggregate_on_registers -------------------------------------------
; Stack pointer arrives as i32; *(p+0) is .A and *(p+4) is .B; ret = (Unknown+A)+B
; WIP: each field deref repeats zext+inttoptr. Would be cleaner to convert the
; i32 arg to ptr once at the top of the function.
CHECK-i386: define i32 @local_cabi_aggregate_on_registers(i32 %[[PTR:.*]]) [[IGN:.*]] {
CHECK-i386:         %[[A_INT:.*]] = zext i32 %[[PTR]] to i64
CHECK-i386:         %[[A_PTR:.*]] = inttoptr i64 %[[A_INT]] to ptr
CHECK-i386:         %[[A:.*]] = load i32, ptr %[[A_PTR]]
CHECK-i386:         %[[T1:.*]] = add i32 {{.*}}, %[[A]]
CHECK-i386:         %[[B_OFF:.*]] = add i32 %[[PTR]], 4
CHECK-i386:         %[[B_INT:.*]] = zext i32 %[[B_OFF]] to i64
CHECK-i386:         %[[B_PTR:.*]] = inttoptr i64 %[[B_INT]] to ptr
CHECK-i386:         %[[B:.*]] = load i32, ptr %[[B_PTR]]
CHECK-i386:         %[[T2:.*]] = add i32 %[[T1]], %[[B]]
CHECK-i386:         ret i32 %[[T2]]
CHECK-i386:       }

; Caller allocates {1, 2} into a stack slot and passes its i32 address.
; WIP: each store goes through ptrtoint -> and u0xffffffff -> inttoptr instead
; of being a direct pointer store.
CHECK-i386: define i32 @local_call_cabi_aggregate_on_registers() [[IGN:.*]] {
CHECK-i386:         %[[SLOT:.*]] = alloca [8 x i8]
CHECK-i386:         %[[SLOT_INT:.*]] = ptrtoint ptr %[[SLOT]] to i64
CHECK-i386:         %[[SLOT_I32:.*]] = trunc i64 %[[SLOT_INT]] to i32
CHECK-i386:         %[[B_OFF:.*]] = add i64 %[[SLOT_INT]], 4
CHECK-i386:         %[[B_MASKED:.*]] = and i64 %[[B_OFF]], u0xffffffff
CHECK-i386:         %[[B_PTR:.*]] = inttoptr i64 %[[B_MASKED]] to ptr
CHECK-i386:         store i32 2, ptr %[[B_PTR]]
CHECK-i386:         %[[A_MASKED:.*]] = and i64 %[[SLOT_INT]], u0xffffffff
CHECK-i386:         %[[A_PTR:.*]] = inttoptr i64 %[[A_MASKED]] to ptr
CHECK-i386:         store i32 1, ptr %[[A_PTR]]
CHECK-i386:         %[[CALL:.*]] = call i32 @local_cabi_aggregate_on_registers(i32 %[[SLOT_I32]])
CHECK-i386:         %[[RET:.*]] = add i32 %[[CALL]], {{.*}}
CHECK-i386:         ret i32 %[[RET]]
CHECK-i386:       }

; ---- cabi_aggregate_on_stack -----------------------------------------------
; Same body shape as cabi_aggregate_on_registers but with 6 leading i32 args.
CHECK-i386: define i32 @local_cabi_aggregate_on_stack(i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[PTR:.*]]) [[IGN:.*]] {
CHECK-i386:         %[[A_INT:.*]] = zext i32 %[[PTR]] to i64
CHECK-i386:         %[[A_PTR:.*]] = inttoptr i64 %[[A_INT]] to ptr
CHECK-i386:         %[[A:.*]] = load i32, ptr %[[A_PTR]]
CHECK-i386:         %[[T1:.*]] = add i32 {{.*}}, %[[A]]
CHECK-i386:         %[[B_OFF:.*]] = add i32 %[[PTR]], 4
CHECK-i386:         %[[B_INT:.*]] = zext i32 %[[B_OFF]] to i64
CHECK-i386:         %[[B_PTR:.*]] = inttoptr i64 %[[B_INT]] to ptr
CHECK-i386:         %[[B:.*]] = load i32, ptr %[[B_PTR]]
CHECK-i386:         %[[T2:.*]] = add i32 %[[T1]], %[[B]]
CHECK-i386:         ret i32 %[[T2]]
CHECK-i386:       }

CHECK-i386: define i32 @local_call_cabi_aggregate_on_stack() [[IGN:.*]] {
CHECK-i386:         %[[SLOT:.*]] = alloca [8 x i8]
CHECK-i386:         %[[SLOT_INT:.*]] = ptrtoint ptr %[[SLOT]] to i64
CHECK-i386:         %[[SLOT_I32:.*]] = trunc i64 %[[SLOT_INT]] to i32
CHECK-i386:         %[[B_OFF:.*]] = add i64 %[[SLOT_INT]], 4
CHECK-i386:         %[[B_MASKED:.*]] = and i64 %[[B_OFF]], u0xffffffff
CHECK-i386:         %[[B_PTR:.*]] = inttoptr i64 %[[B_MASKED]] to ptr
CHECK-i386:         store i32 2, ptr %[[B_PTR]]
CHECK-i386:         %[[A_MASKED:.*]] = and i64 %[[SLOT_INT]], u0xffffffff
CHECK-i386:         %[[A_PTR:.*]] = inttoptr i64 %[[A_MASKED]] to ptr
CHECK-i386:         store i32 1, ptr %[[A_PTR]]
CHECK-i386:         %[[CALL:.*]] = call i32 @local_cabi_aggregate_on_stack(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 %[[SLOT_I32]])
CHECK-i386:         %[[RET:.*]] = add i32 %[[CALL]], {{.*}}
CHECK-i386:         ret i32 %[[RET]]
CHECK-i386:       }

; ---- cabi_aggregate_on_stack_and_registers ---------------------------------
; Same body shape, 5 leading i32 args + ptr.
CHECK-i386: define i32 @local_cabi_aggregate_on_stack_and_registers(i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[IGN:.*]], i32 %[[PTR:.*]]) [[IGN:.*]] {
CHECK-i386:         %[[A_INT:.*]] = zext i32 %[[PTR]] to i64
CHECK-i386:         %[[A_PTR:.*]] = inttoptr i64 %[[A_INT]] to ptr
CHECK-i386:         %[[A:.*]] = load i32, ptr %[[A_PTR]]
CHECK-i386:         %[[T1:.*]] = add i32 {{.*}}, %[[A]]
CHECK-i386:         %[[B_OFF:.*]] = add i32 %[[PTR]], 4
CHECK-i386:         %[[B_INT:.*]] = zext i32 %[[B_OFF]] to i64
CHECK-i386:         %[[B_PTR:.*]] = inttoptr i64 %[[B_INT]] to ptr
CHECK-i386:         %[[B:.*]] = load i32, ptr %[[B_PTR]]
CHECK-i386:         %[[T2:.*]] = add i32 %[[T1]], %[[B]]
CHECK-i386:         ret i32 %[[T2]]
CHECK-i386:       }

CHECK-i386: define i32 @local_call_cabi_aggregate_on_stack_and_registers() [[IGN:.*]] {
CHECK-i386:         %[[SLOT:.*]] = alloca [8 x i8]
CHECK-i386:         %[[SLOT_INT:.*]] = ptrtoint ptr %[[SLOT]] to i64
CHECK-i386:         %[[SLOT_I32:.*]] = trunc i64 %[[SLOT_INT]] to i32
CHECK-i386:         %[[B_OFF:.*]] = add i64 %[[SLOT_INT]], 4
CHECK-i386:         %[[B_MASKED:.*]] = and i64 %[[B_OFF]], u0xffffffff
CHECK-i386:         %[[B_PTR:.*]] = inttoptr i64 %[[B_MASKED]] to ptr
CHECK-i386:         store i32 2, ptr %[[B_PTR]]
CHECK-i386:         %[[A_MASKED:.*]] = and i64 %[[SLOT_INT]], u0xffffffff
CHECK-i386:         %[[A_PTR:.*]] = inttoptr i64 %[[A_MASKED]] to ptr
CHECK-i386:         store i32 1, ptr %[[A_PTR]]
CHECK-i386:         %[[CALL:.*]] = call i32 @local_cabi_aggregate_on_stack_and_registers(i32 1, i32 2, i32 3, i32 4, i32 5, i32 %[[SLOT_I32]])
CHECK-i386:         %[[RET:.*]] = add i32 %[[CALL]], {{.*}}
CHECK-i386:         ret i32 %[[RET]]
CHECK-i386:       }

; ---- cabi_return_small_aggregate (no add returned: returns aggregate) ------
; The body reads an i32 from offset 0 of the alloca, treats it as a pointer
; X, then stores 124 at *X and 123 at *(X+4), and finally returns
; the alloca's [8 x i8] contents.
; WIP: it's not obvious why an extra i32 is loaded from the alloca and used
; as the destination pointer for the field stores, instead of just storing
; 124/123 at offsets 0/4 of the alloca directly. SystemV_x86 does not use
; SPTAR for an 8-byte struct return, so this isn't the hidden-pointer ABI
; arg shape either.
CHECK-i386: define [8 x i8] @local_cabi_return_small_aggregate() [[IGN:.*]] {
CHECK-i386:         %[[SLOT:.*]] = alloca [8 x i8]
CHECK-i386:         %[[SLOT_INT:.*]] = ptrtoint ptr %[[SLOT]] to i64
CHECK-i386:         %[[MASKED:.*]] = and i64 %[[SLOT_INT]], u0xffffffff
CHECK-i386:         %[[SLOT_PTR:.*]] = inttoptr i64 %[[MASKED]] to ptr
CHECK-i386:         %[[BASE:.*]] = load i32, ptr %[[SLOT_PTR]]
CHECK-i386-DAG:     store i32 124, ptr {{.*}}
CHECK-i386-DAG:     %[[OFF:.*]] = add i32 %[[BASE]], 4
CHECK-i386-DAG:     store i32 123, ptr {{.*}}
CHECK-i386:         %[[RV:.*]] = load [8 x i8], ptr %[[SLOT]]
CHECK-i386:         ret [8 x i8] %[[RV]]
CHECK-i386:       }

; Caller: spills the [8 x i8] return value into a stack slot via a single
; aggregate store, reads i32 .B from it, adds Unknown, returns.
; Dataflow anchored: call -> store-aggregate -> load-from-slot -> add -> ret.
CHECK-i386: define i32 @local_call_cabi_return_small_aggregate() [[IGN:.*]] {
CHECK-i386:         %[[SLOT:.*]] = alloca [8 x i8]
CHECK-i386:         %[[SLOT_INT:.*]] = ptrtoint ptr %[[SLOT]] to i64
CHECK-i386:         %[[RV:.*]] = call [8 x i8] @local_cabi_return_small_aggregate()
CHECK-i386:         store [8 x i8] %[[RV]], ptr %[[SLOT]]
CHECK-i386:         %[[MASKED:.*]] = and i64 %[[SLOT_INT]], u0xffffffff
CHECK-i386:         %[[SLOT_PTR:.*]] = inttoptr i64 %[[MASKED]] to ptr
CHECK-i386:         %[[B:.*]] = load i32, ptr %[[SLOT_PTR]]
CHECK-i386:         %[[RET:.*]] = add i32 {{.*}}%[[B]]
CHECK-i386:         ret i32 %[[RET]]
CHECK-i386:       }

; ---- cabi_return_big_aggregate ---------------------------------------------
; Same WIP as cabi_return_small_aggregate: i32 loaded from the alloca and
; used as a base pointer for the field store. .D is written at base+8.
CHECK-i386: define [28 x i8] @local_cabi_return_big_aggregate() [[IGN:.*]] {
CHECK-i386:         %[[SLOT:.*]] = alloca [28 x i8]
CHECK-i386:         %[[SLOT_INT:.*]] = ptrtoint ptr %[[SLOT]] to i64
CHECK-i386:         %[[MASKED:.*]] = and i64 %[[SLOT_INT]], u0xffffffff
CHECK-i386:         %[[SLOT_PTR:.*]] = inttoptr i64 %[[MASKED]] to ptr
CHECK-i386:         %[[BASE:.*]] = load i32, ptr %[[SLOT_PTR]]
CHECK-i386:         %[[OFF:.*]] = add i32 %[[BASE]], 8
CHECK-i386:         store i32 123, ptr {{.*}}
CHECK-i386:         %[[RV:.*]] = load [28 x i8], ptr %[[SLOT]]
CHECK-i386:         ret [28 x i8] %[[RV]]
CHECK-i386:       }

; Caller mirrors call_cabi_return_small_aggregate but with [28 x i8].
; WIP: the load is at slot+4, but the callee writes .D at base+8 — the
; offsets don't match, so the caller is reading either a different field
; or undefined memory.
CHECK-i386: define i32 @local_call_cabi_return_big_aggregate() [[IGN:.*]] {
CHECK-i386:         %[[SLOT:.*]] = alloca [28 x i8]
CHECK-i386:         %[[SLOT_INT:.*]] = ptrtoint ptr %[[SLOT]] to i64
CHECK-i386:         %[[RV:.*]] = call [28 x i8] @local_cabi_return_big_aggregate()
CHECK-i386:         store [28 x i8] %[[RV]], ptr %[[SLOT]]
CHECK-i386:         %[[OFF:.*]] = add i64 %[[SLOT_INT]], 4
CHECK-i386:         %[[MASKED:.*]] = and i64 %[[OFF]], u0xffffffff
CHECK-i386:         %[[SLOT_PTR:.*]] = inttoptr i64 %[[MASKED]] to ptr
CHECK-i386:         %[[D:.*]] = load i32, ptr %[[SLOT_PTR]]
CHECK-i386:         %[[RET:.*]] = add i32 {{.*}}%[[D]]
CHECK-i386:         ret i32 %[[RET]]
CHECK-i386:       }
