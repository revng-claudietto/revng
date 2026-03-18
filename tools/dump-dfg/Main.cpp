//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/IR/Dominators.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include "revng/Support/IRHelpers.h"
#include "revng/Support/InitRevng.h"
#include "revng/Support/ZstdStream.h"
#include "revng/ValueMaterializer/DataFlowGraph.h"

namespace cl = llvm::cl;

static cl::opt<std::string> InputModule(cl::Positional,
                                        cl::desc("<input module>"),
                                        cl::Required);

static cl::opt<std::string> CalleeName("callee",
                                       cl::desc("Name (or substring) of the "
                                                "callee whose argument's DFG "
                                                "to dump"),
                                       cl::Required);

static cl::opt<unsigned> ArgIndex("arg-index",
                                  cl::desc("Index of the argument to trace "
                                           "(default: 0)"),
                                  cl::init(0));

static cl::opt<unsigned> MaxPhiLike("max-phi-like",
                                    cl::desc("Max phi-like nodes in the DFG "
                                             "(default: 1000)"),
                                    cl::init(1000));

static cl::opt<unsigned> MaxLoad("max-load",
                                 cl::desc("Max load nodes in the DFG "
                                          "(default: 1)"),
                                 cl::init(1));

static cl::opt<std::string> OutputFile("o",
                                       cl::desc("Output DOT file (default: "
                                                "stdout)"),
                                       cl::init("-"));

static std::unique_ptr<llvm::Module>
loadModule(llvm::StringRef Path, llvm::LLVMContext &Context) {
  if (Path.ends_with(".zstd") or Path.ends_with(".zst")) {
    auto BufferOrError = llvm::MemoryBuffer::getFileOrSTDIN(Path);
    if (auto EC = BufferOrError.getError()) {
      llvm::errs() << "Error opening " << Path << ": " << EC.message() << "\n";
      return nullptr;
    }

    auto &Buffer = *BufferOrError.get();
    auto Decompressed = zstdDecompress(Buffer.getBuffer());
    return readBitcode(Decompressed, Context);
  }

  return parseIR(Context, Path);
}

int main(int Argc, char *Argv[]) {
  revng::InitRevng X(Argc, Argv, "Dump the data flow graph of a call argument");

  llvm::LLVMContext Context;
  auto Module = loadModule(InputModule, Context);
  if (!Module) {
    llvm::errs() << "Failed to load module\n";
    return 1;
  }

  // Find call sites matching the callee name
  llvm::SmallVector<llvm::CallInst *, 4> MatchingCalls;

  for (llvm::Function &F : *Module) {
    for (llvm::BasicBlock &BB : F) {
      for (llvm::Instruction &I : BB) {
        auto *Call = llvm::dyn_cast<llvm::CallInst>(&I);
        if (!Call)
          continue;

        auto *Callee = getCalledFunction(Call);
        if (!Callee)
          continue;

        if (Callee->getName().contains(CalleeName.getValue()))
          MatchingCalls.push_back(Call);
      }
    }
  }

  if (MatchingCalls.empty()) {
    llvm::errs() << "No calls to a function matching '" << CalleeName
                 << "' found\n";
    return 1;
  }

  llvm::errs() << "Found " << MatchingCalls.size() << " matching call(s):\n";
  for (auto *Call : MatchingCalls) {
    llvm::errs() << "  in " << Call->getFunction()->getName() << ": ";
    Call->print(llvm::errs());
    llvm::errs() << "\n";
  }

  // For each matching call, build and dump the DFG for the specified argument
  unsigned CallIndex = 0;
  for (auto *Call : MatchingCalls) {
    if (ArgIndex >= Call->arg_size()) {
      llvm::errs() << "Call #" << CallIndex << " has only " << Call->arg_size()
                   << " arguments, skipping\n";
      ++CallIndex;
      continue;
    }

    llvm::Value *Arg = Call->getArgOperand(ArgIndex);

    llvm::errs() << "\n=== Call #" << CallIndex << " in "
                 << Call->getFunction()->getName() << " ===\n";
    llvm::errs() << "Argument " << ArgIndex << ": ";
    Arg->print(llvm::errs());
    llvm::errs() << "\n";

    DataFlowGraph::Limits Limits(MaxPhiLike, MaxLoad);
    DataFlowGraph DFG = DataFlowGraph::fromValue(Arg, Limits);
    DFG.removeCycles();
    DFG.enforceLimits(DataFlowGraph::Limits(MaxPhiLike, MaxLoad));
    DFG.purgeUnreachable();

    // Write the DOT graph
    std::string Filename;
    if (OutputFile == "-") {
      Filename = ("dfg-" + llvm::Twine(CallIndex) + ".dot").str();
    } else if (MatchingCalls.size() > 1) {
      Filename = (OutputFile + "." + llvm::Twine(CallIndex)).str();
    } else {
      Filename = OutputFile;
    }

    std::error_code EC;
    llvm::raw_fd_ostream OS(Filename, EC);
    if (EC) {
      llvm::errs() << "Error opening " << Filename << ": " << EC.message()
                   << "\n";
      ++CallIndex;
      continue;
    }

    llvm::WriteGraph(OS, &DFG, /*ShortNames=*/false,
                     ("DFG for argument " + llvm::Twine(ArgIndex) + " of call #"
                      + llvm::Twine(CallIndex))
                       .str());

    llvm::errs() << "Wrote " << Filename << "\n";
    ++CallIndex;
  }

  return 0;
}
