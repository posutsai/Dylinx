#include "llvm/Pass.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "stdint.h"

using namespace llvm;

namespace {
  bool isDispatchPthread(CallGraphNode *node) {
    for (const auto &callee: *node) {
      Function *f = callee.second -> getFunction();
      if (f && f -> getName().compare("pthread_create") == 0)
        return true;
    }
    return false;
  }
  struct PthreadScopeDetectPass : public ModulePass {
    static char ID;
    PthreadScopeDetectPass() : ModulePass(ID) {
    }

    bool runOnModule(Module &M) override {
      CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
      uint32_t nSCC  = 0;
      for (scc_iterator<CallGraph *> iterSCC = scc_begin(&CG); !iterSCC.isAtEnd(); ++iterSCC) {
        auto nodes = *iterSCC;
        for (CallGraphNode *node: nodes) {
          Function *currFunc = node -> getFunction();
          if (isDispatchPthread(node) && currFunc) {
            errs() << "The function " << currFunc -> getName() << " will use pthread_create function \n";
          }
        }
      }
      return true;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
      AU.addRequired<CallGraphWrapperPass>();
    }
  };
}

char PthreadScopeDetectPass::ID = 0;
static RegisterPass<PthreadScopeDetectPass> X(
    "pthread-detect",
    "Extract which function dispatch multiple pthread and execute which task.",
    false /* Only looks at CFG */,
    false /* Analysis Pass */);

