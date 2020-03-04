#include "llvm/Pass.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "stdint.h"

using namespace llvm;

namespace {
  int32_t isDispatchPthread(CallGraphNode *node) {
    for (int32_t i = 0; i < node -> size(); i++) {
      Function *f = (*node)[i] -> getFunction();
      if (f && f -> getName().compare("pthread_create") == 0) {
        return i;
      }
    }
    return -1;
  }

  void insertTimer(Function *func) {
    for (auto &inst: instructions(func)) {
        if (inst.getOpcode() == 56) {
          CallInst *ci = dyn_cast<CallInst>(&inst);
          if (ci && ci->getCalledFunction()->getName().compare("pthread_create") == 0) {
            Function *pthread_task = dyn_cast<Function>(ci->getArgOperand(2));
            errs() << "pthread is going to execute " << pthread_task->getName() << "function \n";
          }
        }
    }
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
          int32_t target_i = isDispatchPthread(node);
          if (target_i >= 0 && currFunc) {
            errs() << "The function " << currFunc -> getName() << " will use pthread_create function at " << target_i << '\n';
            insertTimer(currFunc);
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

