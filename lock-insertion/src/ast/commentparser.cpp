#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Casting.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <string>
#include <regex>
#include <utility>
#include "yaml-cpp/yaml.h"
#include "util.h"

// [TODO]
// 1. Find a way to manipulate YAML content in different stage. Global variable may be
//    a good choice.
// 2. Output the rewritten buffer to a new file under certain path.

using namespace clang;
using namespace llvm;
using namespace clang::tooling;
using namespace clang::ast_matchers;

uint64_t g_lock_cnt = 0;

typedef std::pair<FileID, uint32_t> CommentID;

class SlotLockerHandler: public MatchFinder::MatchCallback {
public:
  SlotLockerHandler(Rewriter &rw): rw(rw) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("locks")) {
      std::string slot_name("___dylinx_slot_lock_");
      rw.ReplaceText(e->getCallee()->getSourceRange(), slot_name);
    }
  }
private:
  Rewriter &rw;
};

class SlotUnlockerHandler: public MatchFinder::MatchCallback {
public:
  SlotUnlockerHandler(Rewriter &rw) : rw(rw) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("unlocks")) {
      std::string slot_name("___dylinx_slot_unlock_");
      rw.ReplaceText(e->getCallee()->getSourceRange(), slot_name);
    }
  }
private:
  Rewriter &rw;
};

class SlotInitHandler: public MatchFinder::MatchCallback {
public:
  SlotInitHandler(Rewriter &rw) : rw(rw) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("inits")) {
      std::string slot_name("___dylinx_slot_init_");
      rw.ReplaceText(e->getCallee()->getSourceRange(), slot_name);
    }
  }
private:
  Rewriter &rw;
};


class SlotDestroyHandler: public MatchFinder::MatchCallback {
public:
  SlotDestroyHandler(Rewriter &rw) : rw(rw) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("destroys")) {
      std::string slot_name("___dylinx_slot_destroy_");
      rw.ReplaceText(e->getCallee()->getSourceRange(), slot_name);
    }
  }
private:
  Rewriter &rw;
};

class SlotVarsHandler: public MatchFinder::MatchCallback {
public:
  SlotVarsHandler(
    Rewriter &rw,
    std::map<CommentID, bool> *commented_locks,
    std::map<std::string, uint32_t> *replaced_vars
  ) :
    rw(rw), commented_locks(commented_locks), replaced_vars(replaced_vars) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    // Take care to the varDecl in the function argument
    if (const VarDecl *d = result.Nodes.getNodeAs<VarDecl>("vars")) {
      SourceManager& sm = result.Context->getSourceManager();
      SourceLocation loc = d->getSourceRange().getBegin();
      FileID src_id = sm.getFileID(loc);
      uint32_t line = sm.getSpellingLineNumber(loc);
      printf("Hash:%u Line %u variable name is %s @(%u, %u) isFirstDecl: %u\n",
        src_id.getHashValue(),
        line,
        d->getName().str().c_str(),
        sm.getSpellingColumnNumber(d->getBeginLoc()),
        sm.getSpellingColumnNumber(d->getEndLoc()),
        d->isFirstDecl()
      );
      CommentID key = std::make_pair(src_id, line);
      if (commented_locks->find(key) != commented_locks->end()) {
        std::string cur_lock_id = "_lock_id";
        cur_lock_id += std::to_string(g_lock_cnt);
        (*replaced_vars)[d->getName().str()] = g_lock_cnt;
        rw.ReplaceText(d->getEndLoc(), d->getName().size(), cur_lock_id);
        g_lock_cnt++;
        if (!(*commented_locks)[key]) {
          rw.ReplaceText(d->getBeginLoc(), 15, "BaseLock");
          (*commented_locks)[key] = true;
        }
      } else {
        fprintf(stderr, "no matched statement!!! %u\n", d->isFunctionOrMethodVarDecl());
      }
    }
  }
private:
  Rewriter &rw;
  std::map<CommentID, bool> *commented_locks;
  std::map<std::string, uint32_t> *replaced_vars;
};

class SlotFuncArgumentHandler: public MatchFinder::MatchCallback {
public:
  SlotFuncArgumentHandler(
    Rewriter &rw,
    std::map<std::string, uint32_t> *replaced_vars
  ) :
    rw(rw), replaced_vars(replaced_vars) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const DeclRefExpr *e = result.Nodes.getNodeAs<DeclRefExpr>("declRefs")) {
      const VarDecl *d = dyn_cast<VarDecl>(e->getDecl());
      if(d && (d->isLocalVarDecl() || d->hasGlobalStorage())) {
        std::string lock_id = "_lock_id";
        lock_id += std::to_string(
          (*replaced_vars)[e->getNameInfo().getName().getAsString()]
        );
        rw.ReplaceText(SourceRange(e->getBeginLoc(), e->getEndLoc()), lock_id);
      }
    }
  }
private:
  Rewriter &rw;
  std::map<std::string, uint32_t> *replaced_vars;
};


class SlotIdentificationConsumer : public clang::ASTConsumer {
public:
  explicit SlotIdentificationConsumer(ASTContext *Context, Rewriter &rw):
    handler_for_lock(rw),
    handler_for_unlock(rw),
    handler_for_init(rw),
    handler_for_destroy(rw),
    handler_for_vars(rw, &commented_locks, &replaced_vars),
    handler_for_func_args(rw, &replaced_vars)
  {}
  ~SlotIdentificationConsumer() {
    this->yamlfout.close();
  }
  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    matcher_1st.addMatcher(
      varDecl(hasType(asString("pthread_mutex_t"))).bind("vars"),
      &handler_for_vars
    );
    matcher_2nd.addMatcher(
      callExpr(callee(functionDecl(hasName("pthread_mutex_lock")))).bind("locks"),
      &handler_for_lock
    );
    matcher_2nd.addMatcher(
      callExpr(callee(functionDecl(hasName("pthread_mutex_unlock")))).bind("unlocks"),
      &handler_for_unlock
    );
    matcher_2nd.addMatcher(
      callExpr(callee(functionDecl(hasName("pthread_mutex_init")))).bind("inits"),
      &handler_for_init
    );
    matcher_2nd.addMatcher(
      callExpr(callee(functionDecl(hasName("pthread_mutex_destroy")))).bind("destroys"),
      &handler_for_destroy
    );
    matcher_2nd.addMatcher(
      declRefExpr(to(varDecl(hasType(asString("pthread_mutex_t"))))).bind("declRefs"),
      &handler_for_func_args
    );
    YAML::Node node;
    YAML::Emitter out;
    SourceManager& sm = Context.getSourceManager();
    auto decls = Context.getTranslationUnitDecl()->decls();
    YAML::Node markers;
    for (auto &decl: decls) {
      if (sm.isInSystemHeader(decl->getLocation()) || !isa<FunctionDecl>(decl) && !isa<LinkageSpecDecl>(decl))
        continue;
      if (auto *comments = Context.getRawCommentList().getCommentsInFile(
            sm.getFileID(decl->getLocation()))) {
        for (auto cmt : *comments) {
          std::cout << "vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n";
          llvm::outs() << "At " << sm.getFilename(decl->getLocation()).str() << '\n'
            << cmt.second->getRawText(Context.getSourceManager()).str() << '\n';
          YAML::Node mark;
          std::cout << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
          std::string comb = cmt.second->getRawText(Context.getSourceManager()).str();
          std::smatch conf_sm;
          std::regex re("\\/\\/! \\[LockSlot\\](.*)");
          std::regex_match(comb, conf_sm, re);
          printf("the size of sm is %lu\n", conf_sm.size());
          for (uint32_t i = 0; i < conf_sm.size(); i++) {
            std::cout << "sm[" << i << "] = " << conf_sm[i] << std::endl;
          }
          if (conf_sm.size() == 2) {
            SourceLocation loc = cmt.second->getBeginLoc();
            FileID src_id = sm.getFileID(loc);
            uint32_t line = sm.getSpellingLineNumber(loc);
            mark["filepath"] = getAbsolutePath(sm.getFileEntryForID(src_id)->getName());
            mark["line"] = line;
            commented_locks[std::make_pair(src_id, line)] = false;
            printf("key is (%s, %u)\n", mark["filepath"].as<std::string>().c_str(), mark["line"].as<uint32_t>());
            std::smatch lock_sm;
            std::string conf = conf_sm[1];
            std::regex lock_pat(getLockPattern());
            YAML::Node cmb;
            while(std::regex_search(conf, lock_sm, lock_pat)) {
              std::string t = lock_sm[1];
              cmb.push_back(t);
              conf = lock_sm.suffix().str();
            }
            mark["comb"] = cmb;
            node["Mark"].push_back(mark);
          }
        }
        mark_i++;
      }
      //! TODO
      // Add extra pair of slot_lock_i and slot_unlock_i FunctionDecl Pair in
      // corresponding TranslationUnitDecl
    }
    matcher_1st.matchAST(Context);
    matcher_2nd.matchAST(Context);
    out << node;
    this->yamlfout << out.c_str();
  }
  void setYamlFout(std::string path) {
    this->yamlfout = std::ofstream(path.c_str());
  }
private:
  uint32_t mark_i = 0;
  std::ofstream yamlfout;
  MatchFinder matcher_1st;
  MatchFinder matcher_2nd;
  SlotLockerHandler handler_for_lock;
  SlotUnlockerHandler handler_for_unlock;
  SlotInitHandler handler_for_init;
  SlotDestroyHandler handler_for_destroy;
  SlotVarsHandler handler_for_vars;
  SlotFuncArgumentHandler handler_for_func_args;
  std::map<CommentID, bool> commented_locks;
  std::map<std::string, uint32_t> replaced_vars;
};

class SlotIdentificationAction : public clang::ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile
  ) {
    rw.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
    std::unique_ptr<SlotIdentificationConsumer> consumer(new SlotIdentificationConsumer(
      &Compiler.getASTContext(),
      rw
    ));
    consumer->setYamlFout(this->yaml_path);
    return consumer;
  }

  void setYamlPath(std::string yaml_path) {
    this->yaml_path = yaml_path;
  }

  void setCompileDB(std::shared_ptr<CompilationDatabase> compiler_db) {
    this->compiler_db = compiler_db;
  }

  void EndSourceFileAction() override {
    // Manually add the definition of BaseLock, slot_lock and
    // slot_unlock here.
    SourceManager &sm = rw.getSourceMgr();
    FileManager &fm = sm.getFileManager();
    std::string buffer;
    raw_string_ostream stream(buffer);
    rw.getEditBuffer(rw.getSourceMgr().getMainFileID())
              .write(stream);
    rw.getEditBuffer(rw.getSourceMgr().getMainFileID())
              .write(llvm::outs());
    return;
    std::vector<std::string>::iterator it = std::find(
      compiler_db->getAllFiles().begin(),
      compiler_db->getAllFiles().end(),
      getAbsolutePath(getCurrentFile())
    );
    if (it == compiler_db->getAllFiles().end()) {
      fprintf(
        stderr,
        "[ ERROR ]Can't identify the index of current file: \n%s\n",
        getAbsolutePath(getCurrentFile()).c_str()
      );
      std::abort();
    }
    uint32_t index = std::distance(compiler_db->getAllFiles().begin(), it);
    std::vector<std::string> args {
      "-std=c++14",
      "-I/usr/local/lib/clang/10.0.0/include",
      "-I/home/plate/git/ContentionModel/lock-insertion/target",
      "-I/usr/local/include",
      "-I/usr/include"
    };
    buildASTFromCodeWithArgs(
        stream.str(),
        // compiler_db->getAllCompileCommands()[index].CommandLine,
        args,
        "/tmp/temp-file.cc"
    );
  }
private:
  std::string yaml_path;
  Rewriter rw;
  std::shared_ptr<CompilationDatabase> compiler_db;
};


std::unique_ptr<FrontendActionFactory> newSlotIdentificationActionFactory(
    std::string yaml_conf,
    std::shared_ptr<CompilationDatabase> compiler_db
    ) {
  class SlotIdentificationActionFactory: public FrontendActionFactory {
  public:
    std::unique_ptr<FrontendAction> create() override {
      std::unique_ptr<SlotIdentificationAction> action(new SlotIdentificationAction);
      action->setYamlPath(this->yaml_path);
      action->setCompileDB(this->compiler_db);
      return action;
    };
    void setYamlPath(std::string path) {
      this->yaml_path = path;
    }
    void setCompileDB(std::shared_ptr<CompilationDatabase> compiler_db) {
      this->compiler_db = compiler_db;
    }
  private:
    std::string yaml_path;
    std::shared_ptr<CompilationDatabase> compiler_db;
  };
  std::unique_ptr<SlotIdentificationActionFactory> factory(new SlotIdentificationActionFactory);
  factory->setYamlPath(yaml_conf);
  factory->setCompileDB(compiler_db);
  return factory;
}

int main(int argc, const char **argv) {
  std::string err;
  const char *compiler_db_path = argv[1];
  std::shared_ptr<CompilationDatabase> compiler_db = CompilationDatabase::autoDetectFromSource(compiler_db_path, err);
  ClangTool tool(*compiler_db, compiler_db->getAllFiles());
  return tool.run(newSlotIdentificationActionFactory("test-output.yaml", compiler_db).get());
}
