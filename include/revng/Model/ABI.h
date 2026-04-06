#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/Model/Architecture.h"

namespace model::ABI {

inline bool isValid(llvm::StringRef V) { return !V.empty(); }

inline constexpr llvm::StringRef getName(llvm::StringRef V) { return V; }

inline model::Architecture::Values
getArchitecture(llvm::StringRef V) {
  if (V == "SystemV_x86_64"
      || V == "Microsoft_x86_64"
      || V == "Microsoft_x86_64_vectorcall")
    return model::Architecture::x86_64;

  if (V == "SystemV_x86"
      || V == "SystemV_x86_regparm_3"
      || V == "SystemV_x86_regparm_2"
      || V == "SystemV_x86_regparm_1"
      || V == "Microsoft_x86_vectorcall"
      || V == "Microsoft_x86_cdecl"
      || V == "Microsoft_x86_cdecl_gcc"
      || V == "Microsoft_x86_stdcall"
      || V == "Microsoft_x86_stdcall_gcc"
      || V == "Microsoft_x86_fastcall"
      || V == "Microsoft_x86_fastcall_gcc"
      || V == "Microsoft_x86_thiscall")
    return model::Architecture::x86;

  if (V == "AAPCS64"
      || V == "Microsoft_AAPCS64"
      || V == "Apple_AAPCS64")
    return model::Architecture::aarch64;
  if (V == "AAPCS")
    return model::Architecture::arm;

  if (V == "SystemV_MIPS_o32")
    return model::Architecture::mips;

  if (V == "SystemV_MIPSEL_o32")
    return model::Architecture::mipsel;

  if (V == "SystemZ_s390x")
    return model::Architecture::systemz;

  revng_abort();
}

/// A workaround for the model not having dedicated `mipsel` registers.
///
/// The returned architecture is the one registers of which is used inside
/// the `model::ABIDefinition`.
inline model::Architecture::Values
getRegisterArchitecture(llvm::StringRef V) {
  if (V == "SystemV_MIPSEL_o32")
    return model::Architecture::mips;
  else
    return getArchitecture(V);
}

/// \return the size of the pointer under the specified ABI.
inline uint64_t getPointerSize(llvm::StringRef V) {
  return model::Architecture::getPointerSize(getArchitecture(V));
}

// TODO: move binary specific information away from a major header
inline std::string
getDefaultForELF(model::Architecture::Values V) {
  switch (V) {
  case model::Architecture::x86_64:
    return "SystemV_x86_64";
  case model::Architecture::x86:
    return "SystemV_x86";
  case model::Architecture::aarch64:
    return "AAPCS64";
  case model::Architecture::arm:
    return "AAPCS";
  case model::Architecture::mips:
    return "SystemV_MIPS_o32";
  case model::Architecture::mipsel:
    return "SystemV_MIPSEL_o32";
  case model::Architecture::systemz:
    return "SystemZ_s390x";
  default:
    return "";
  }
}

inline std::string
getDefaultForPECOFF(model::Architecture::Values V) {
  switch (V) {
  case model::Architecture::x86_64:
    return "Microsoft_x86_64";
  case model::Architecture::x86:
    return "Microsoft_x86_cdecl";
  case model::Architecture::aarch64:
    return "Microsoft_AAPCS64";
  default:
    return "";
  }
}

inline std::string
getDefaultForMachO(model::Architecture::Values V) {
  switch (V) {
  case model::Architecture::x86_64:
    return "SystemV_x86_64";
  case model::Architecture::x86:
    return "SystemV_x86";
  case model::Architecture::aarch64:
    return "Apple_AAPCS64";
  default:
    return "";
  }
}

inline llvm::StringRef getDescription(llvm::StringRef V) {
  if (V == "SystemV_x86_64")
    return "64-bit SystemV x86 abi";
  if (V == "Microsoft_x86_64")
    return "64-bit Microsoft x86 abi";
  if (V == "Microsoft_x86_64_vectorcall")
    return "64-bit Microsoft x86 abi with extra vector "
           "registers designited for passing function "
           "arguments";

  if (V == "SystemV_x86")
    return "32-bit SystemV x86 abi";
  if (V == "SystemV_x86_regparm_3")
    return "32-bit SystemV x86 abi that allows the first three GPR-sized "
           "scalar arguments to be passed using the registers";
  if (V == "SystemV_x86_regparm_2")
    return "32-bit SystemV x86 abi that allows the first two GPR-sized "
           "scalar arguments to be passed using the registers";
  if (V == "SystemV_x86_regparm_1")
    return "32-bit SystemV x86 abi that allows the first "
           "GPR-sized scalar argument to be passed using "
           "the registers";
  if (V == "Microsoft_x86_vectorcall")
    return "64-bit Microsoft x86 abi, it extends `fastcall` "
           "by allowing extra vector registers to be used for "
           "function argument passing";
  if (V == "Microsoft_x86_cdecl")
    return "32-bit Microsoft x86 abi that was intended to "
           "mimic 32-bit SystemV x86 abi but has minor "
           "differences";
  if (V == "Microsoft_x86_cdecl_gcc")
    return "32-bit Microsoft x86 `cdecl` abi as implemented in GCC (subtly "
           "different from the original).";
  if (V == "Microsoft_x86_stdcall")
    return "32-bit Microsoft x86 abi, it is a modification of "
           "`cdecl` that's different in a sense that the "
           "callee is responsible for stack cleanup instead "
           "of the caller";
  if (V == "Microsoft_x86_stdcall_gcc")
    return "32-bit Microsoft x86 `stdcall` abi as implemented in GCC (subtly "
           "different from the original).";
  if (V == "Microsoft_x86_fastcall")
    return "32-bit Microsoft x86 abi, it extends `stdcall` by "
           "allowing two first GPR-sized function arguments "
           "to be passed using the registers";
  if (V == "Microsoft_x86_fastcall_gcc")
    return "32-bit Microsoft x86 `fastcall` abi as implemented in GCC (subtly "
           "different from the original).";
  if (V == "Microsoft_x86_thiscall")
    return "32-bit Microsoft x86 abi, it extends `stdcall` by "
           "allowing `this` pointer in method-style calls to "
           "be passed using a register. It is never used for "
           "`free` functions";

  if (V == "AAPCS64")
    return "64-bit ARM abi";
  if (V == "Microsoft_AAPCS64")
    return "Microsoft version of 64-bit ARM abi";
  if (V == "Apple_AAPCS64")
    return "Apple version of 64-bit ARM abi";
  if (V == "AAPCS")
    return "32-bit ARM abi";

  if (V == "SystemV_MIPS_o32")
    return "The \"old\" 32-bit MIPS abi";

  if (V == "SystemV_MIPSEL_o32")
    return "The \"old\" 32-bit MIPS abi (little endian edition)";

  if (V == "SystemZ_s390x")
    return "The s390x SystemZ ABI";

  return "Unknown and/or unsupported ABI";
}

} // namespace model::ABI
