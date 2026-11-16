//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#define BOOST_TEST_MODULE ConvertToCABI
bool init_unit_test();
#include "boost/test/unit_test.hpp"

#include "revng/ABI/FunctionType/Conversion.h"
#include "revng/Model/Binary.h"

using abi::FunctionType::tryConvertToCABI;
using model::PrimitiveType;

/// A binary with a raw function returning \p ReturnValue, that leaves the stack
/// pointer \p FinalStackOffset bytes above its entry value
struct TestBinary {
  TupleTree<model::Binary> Binary;
  model::RawFunctionDefinition *Prototype = nullptr;

  TestBinary(model::Architecture::Values Architecture,
             model::ABI::Values ABI,
             model::Register::Values ReturnValue,
             uint64_t FinalStackOffset) {
    Binary->Architecture() = Architecture;
    Binary->DefaultABI() = ABI;

    auto &&[Definition, Type] = Binary->makeRawFunctionDefinition();
    Definition.Architecture() = Architecture;
    Definition.addReturnValue(ReturnValue, PrimitiveType::make(ReturnValue));
    Definition.FinalStackOffset() = FinalStackOffset;
    Prototype = &Definition;
  }

  bool convert() { return tryConvertToCABI(*Prototype, Binary).has_value(); }
};

/// A SystemV x86-64 function taking rdi and returning rax
struct SystemVBinary : public TestBinary {
  explicit SystemVBinary(uint64_t FinalStackOffset) :
    TestBinary(model::Architecture::x86_64,
               model::ABI::SystemV_x86_64,
               model::Register::rax_x86_64,
               FinalStackOffset) {
    Prototype->addArgument(model::Register::rdi_x86_64,
                           PrimitiveType::make(model::Register::rdi_x86_64));
  }
};

/// An x86 stdcall function taking 8 bytes on the stack and returning eax
struct StdcallBinary : public TestBinary {
  explicit StdcallBinary(uint64_t FinalStackOffset) :
    TestBinary(model::Architecture::x86,
               model::ABI::Microsoft_x86_stdcall,
               model::Register::eax_x86,
               FinalStackOffset) {
    auto &&[Stack, Type] = Binary->makeStructDefinition();
    Stack.Size() = 8;
    Stack.addField(0, PrimitiveType::makeGeneric(4));
    Stack.addField(4, PrimitiveType::makeGeneric(4));
    Prototype->StackArgumentsType() = Type;
  }
};

BOOST_AUTO_TEST_CASE(CallerCleansUp) {
  SystemVBinary Test(8);
  revng_check(Test.convert());
}

BOOST_AUTO_TEST_CASE(UnknownFinalStackOffset) {
  SystemVBinary Test(0);
  revng_check(Test.convert());
}

BOOST_AUTO_TEST_CASE(CalleePopsArguments) {
  // `ret 16`: SystemV has the caller clean up, so the conversion would lose
  // the 16 bytes popped by the callee
  SystemVBinary Test(24);
  revng_check(not Test.convert());
  revng_check(Test.Binary->TypeDefinitions().size() == 1);
}

BOOST_AUTO_TEST_CASE(CalleePopsArgumentsReturningStruct) {
  // Returning rax and rdx needs a new struct, which must not be left behind
  SystemVBinary Test(24);
  Test.Prototype
    ->addReturnValue(model::Register::rdx_x86_64,
                     PrimitiveType::make(model::Register::rdx_x86_64));
  revng_check(not Test.convert());
  revng_check(Test.Binary->TypeDefinitions().size() == 1);
}

BOOST_AUTO_TEST_CASE(StdcallCalleePopsArguments) {
  // `ret 8`
  StdcallBinary Test(12);
  revng_check(Test.convert());
}

BOOST_AUTO_TEST_CASE(StdcallCallerCleansUp) {
  StdcallBinary Test(4);
  revng_check(not Test.convert());
  revng_check(Test.Binary->TypeDefinitions().size() == 2);
}
