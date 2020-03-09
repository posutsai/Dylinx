#include "llvm/Pass.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <stdint.h>
#include <string>

#define LOCK_TRIGGER "pthread_mutex_lock"
#define UNLOCK_TRIGGER "pthread_mutex_unlock"

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

  void insertTimer(Function *func, Module &M) {
    for (auto &inst: instructions(func)) {
      if (inst.getOpcode() == 56) {
        CallInst *ci = dyn_cast<CallInst>(&inst);
        if (ci && ci->getCalledFunction()->getName().compare("pthread_create") == 0) {
          Function *pthread_task = dyn_cast<Function>(ci->getArgOperand(2));
          errs() << raw_ostream::GREEN << "pthread is going to execute [" << pthread_task->getName() << "] function \n" << raw_ostream::RESET;

          // Check if struct.timespec is available in module
          StructType *timespec = M.getTypeByName("struct.timespec");
          if (!timespec) {
            Type *types[2] = {
              IntegerType::get(M.getContext(), 64),
              IntegerType::get(M.getContext(), 64)
            };
            ArrayRef<Type *> arrRef(types);
            timespec = StructType::create(M.getContext(), arrRef, "struct.timespec");
          }

          BasicBlock &entryBB = pthread_task->getEntryBlock();
          // declare nonCricticStart and nonCriticEnd
          // Note:
          //  The AllocaInst pointer is also the address pointer to the allocated value.
          AllocaInst *allocaNonCriticStart = new AllocaInst(timespec, 0, "nonCriticStart");
          allocaNonCriticStart->setAlignment(MaybeAlign(8));
          entryBB.getInstList().insert(entryBB.begin(), allocaNonCriticStart);
          AllocaInst *allocaNonCriticEnd = new AllocaInst(timespec, 0, "nonCriticEnd");
          allocaNonCriticEnd->setAlignment(MaybeAlign(8));
          entryBB.getInstList().insert(entryBB.begin(), allocaNonCriticEnd);
          // declare criticStart and criticEnd
          AllocaInst *allocaCriticStart = new AllocaInst(timespec, 0, "criticStart");
          allocaCriticStart->setAlignment(MaybeAlign(8));
          entryBB.getInstList().insert(entryBB.begin(), allocaCriticStart);
          AllocaInst *allocaCriticEnd = new AllocaInst(timespec, 0, "criticEnd");
          allocaCriticEnd->setAlignment(MaybeAlign(8));
          entryBB.getInstList().insert(entryBB.begin(), allocaCriticEnd);

          Function *clock_gettime = M.getFunction("clock_gettime");

          // Note:
          //  Recursive locking or locking without post-unlocking may exist
          CallInst *lock_ci = nullptr;
          CallInst *unlock_ci = nullptr;
          for (auto &task_inst: instructions(pthread_task)) {
            // Get the most exterior locking and unlocking trigger
            if (inst.getOpcode() == 56) {
              CallInst *ci = dyn_cast<CallInst>(&task_inst);
              if (ci && !lock_ci && ci->getCalledFunction()->getName().compare(LOCK_TRIGGER) == 0) {
                // Call clock_gettime
                lock_ci = ci;
                Value *args[2] = {
                  ConstantInt::get(IntegerType::get(M.getContext(), 32), 1, true),
                  allocaCriticStart
                };
                ArrayRef<Value *> arrRef(args);
                CallInst::Create(clock_gettime, arrRef, "criticTimerStart", lock_ci->getNextNode());
              } else if (ci && ci->getCalledFunction()->getName().compare(UNLOCK_TRIGGER) == 0)
                unlock_ci = ci;
            }
          }
          Value *args[2] = {
            ConstantInt::get(IntegerType::get(M.getContext(), 32), 1, true),
            allocaCriticEnd
          };
          ArrayRef<Value *> arrRef(args);
          CallInst::Create(clock_gettime, arrRef, "criticTimerEnd", unlock_ci);
        }
      }
    }
  }
  GlobalVariable *createGV(std::string name, Module &M) {
    // The global variable here would be used to accumulate time
    // duration including critical and non-critical.
    GlobalVariable *gVar = new GlobalVariable(
        M, IntegerType::get(M.getContext(), 32),
        false,
        GlobalValue::CommonLinkage,
        ConstantInt::get(IntegerType::get(M.getContext(), 32), 0),
        "testGV");
    gVar->setAlignment(MaybeAlign(4)); // Issue may happen here.
    return gVar;
  }

  struct PthreadScopeDetectPass : public ModulePass {
    static char ID;
    PthreadScopeDetectPass() : ModulePass(ID) { }

    bool runOnModule(Module &M) override {
      CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
      GlobalVariable *gVar = createGV("testGV", M);
      uint32_t nSCC  = 0;
      for (scc_iterator<CallGraph *> iterSCC = scc_begin(&CG); !iterSCC.isAtEnd(); ++iterSCC) {
        auto nodes = *iterSCC;
        for (CallGraphNode *node: nodes) {
          Function *currFunc = node -> getFunction();
          int32_t target_i = isDispatchPthread(node);
          if (target_i >= 0 && currFunc) {
            errs() << "The function " << currFunc -> getName() << " will use pthread_create function at " << target_i << '\n';
            insertTimer(currFunc, M);
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

