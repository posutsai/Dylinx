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
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <stdint.h>
#include <stdio.h>
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

          // {{{ Variable allocation for struct timespec
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
          // }}}

          Function *clock_gettime = M.getFunction("clock_gettime");
          if (!clock_gettime) {
            Type *types[2] = {
              IntegerType::get(M.getContext(), 32),
              PointerType::get(timespec, 0)
            };
            ArrayRef<Type *> arrRef(types);
            FunctionType *FT = FunctionType::get(
                  IntegerType::get(M.getContext(), 32),
                  arrRef, false
            );
            clock_gettime = Function::Create(FT, GlobalValue::ExternalLinkage, "clock_gettime", M);
          }

          // {{{ Insert CallInst: clock_gettime
          // Insert timer for non-critical section
          {
            Value *args[2] = {
              ConstantInt::get(IntegerType::get(M.getContext(), 32), 1, true),
              allocaNonCriticStart
            };
            ArrayRef<Value *> arrRef(args);
            CallInst::Create(clock_gettime, arrRef, "nonCriticTimerStart", allocaNonCriticStart->getNextNode());
          }

          // Note:
          //  Recursive locking or locking without post-unlocking may exist
          CallInst *lock_ci = nullptr;
          CallInst *unlock_ci = nullptr;
          CallInst *non_critic_end = nullptr;
          for (auto &task_inst: instructions(pthread_task)) {
            // Get the most exterior locking and unlocking trigger
            // Note:
            //  Potential bug is that if the source input unlock or lock in differennt
            //  executing path. Try to insert timer depending on lock instance.
            if (isa<CallInst>(task_inst)) {
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
            } else if (isa<ReturnInst>(task_inst)) {
              // To prevent miss measuring end of the non-critical section due to branch,
              // we insert timer in before every return instruction.
              ReturnInst *ri = dyn_cast<ReturnInst>(&task_inst);
              Value *args[2] = {
                ConstantInt::get(IntegerType::get(M.getContext(), 32), 1, true),
                allocaNonCriticEnd
              };
              ArrayRef<Value *> arrRef(args);
              non_critic_end = CallInst::Create(clock_gettime, arrRef, "nonCriticTimerEnd", ri);
            }
          }
          {
            Value *args[2] = {
              ConstantInt::get(IntegerType::get(M.getContext(), 32), 1, true),
              allocaCriticEnd
            };
            ArrayRef<Value *> arrRef(args);
            CallInst::Create(clock_gettime, arrRef, "criticTimerEnd", unlock_ci);
          }
          // }}}

          // {{{ Insert CallInst: get duration
          {
            Type *types[3] = {
              PointerType::get(timespec, 0),
              PointerType::get(timespec, 0),
              PointerType::get(IntegerType::get(M.getContext(), 64), 0)
            };
            ArrayRef<Type *>arrRefTypes(types);
            FunctionType *FT = FunctionType::get(
              IntegerType::get(M.getContext(), 64),
              arrRefTypes, false
            );
            Function *accu_duration = Function::Create(FT, GlobalValue::ExternalLinkage, "accu_duration", M);
            Value *critic_params[3] = {allocaCriticStart, allocaCriticEnd, M.getNamedValue("criticDurSum")};
            ArrayRef<Value *> arr_ref_critic(critic_params);
            Value *noncritic_params[3] = {allocaNonCriticStart, allocaNonCriticEnd, M.getNamedValue("nonCriticDurSum")};
            ArrayRef<Value *> arr_ref_noncritic(noncritic_params);
            CallInst *critic_measure = CallInst::Create(accu_duration, arr_ref_critic, "criticalDur", non_critic_end->getNextNode());
            CallInst *noncritic_measure = CallInst::Create(accu_duration, arr_ref_noncritic, "nonCriticalDur", critic_measure->getNextNode());
          }
          // }}}

          Value *dispatch_params[1] = { ci->getArgOperand(3) };
          ArrayRef<Value *> arr_ref_dispatch(dispatch_params);
          CallInst *flatten_dispatch = CallInst::Create(pthread_task, arr_ref_dispatch, "haha", ci);
          errs() << "Is pthread_create safe to remove? " << ci->isSafeToRemove() << '\n';
          errs() << "Does pthread_create have side effect? " << ci->mayHaveSideEffects() << '\n';
          // ReplaceInstWithInst(ci, flatten_dispatch);
          // ci->eraseFromParent();
        }
      }
    }
  }
  void createGV(Module &M) {
    // The global variable here would be used to accumulate time
    // duration including critical and non-critical.
    GlobalVariable *critic_dur_sum = new GlobalVariable(
        M, IntegerType::get(M.getContext(), 64),
        false,
        GlobalValue::CommonLinkage,
        ConstantInt::get(IntegerType::get(M.getContext(), 64), 0),
        "criticDurSum");
    critic_dur_sum->setAlignment(MaybeAlign(8));
    GlobalVariable *noncritic_dur_sum = new GlobalVariable(
        M, IntegerType::get(M.getContext(), 64),
        false,
        GlobalValue::CommonLinkage,
        ConstantInt::get(IntegerType::get(M.getContext(), 64), 0),
        "nonCriticDurSum");
    noncritic_dur_sum->setAlignment(MaybeAlign(8));
  }

  struct PthreadScopeDetectPass : public ModulePass {
    static char ID;
    PthreadScopeDetectPass() : ModulePass(ID) { }
    bool runOnModule(Module &M) override {
      FILE *fp = fopen("./hello.txt", "a+");
      fprintf(fp, "helloworld\n");
      fclose(fp);
      CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
      createGV(M);
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

