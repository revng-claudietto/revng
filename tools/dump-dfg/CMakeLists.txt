#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_executable(dump-dfg Main.cpp)

llvm_map_components_to_libnames(LLVM_LIBRARIES IRReader BitReader)
target_link_libraries(dump-dfg
                      revngSupport revngValueMaterializer ${LLVM_LIBRARIES})
