#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/Expr.h"
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
#include "yaml-cpp/yaml.h"
#include "util.h"

// [TODO]
// 1. Find a way to pass yaml output file path as construct parameter

using namespace clang;
using namespace llvm;
using namespace clang::tooling;
using namespace clang::ast_matchers;

class SlotLockerHandler: public MatchFinder::MatchCallback {
public:
  SlotLockerHandler(Rewriter &rw): rw(rw), i(0) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("functions")) {
      ASTContext *Context = result.Context;
      std::string slot_name("___lock_slot_");
      rw.ReplaceText(e->getCallee()->getSourceRange(), slot_name + std::to_string(i));
      i++;
    }
  }
private:
  unsigned int i;
  Rewriter &rw;
};

class SlotIdentificationConsumer : public clang::ASTConsumer {
public:
  explicit SlotIdentificationConsumer(ASTContext *Context, Rewriter &rw): handler_for_lock(rw) {}
  ~SlotIdentificationConsumer() {
    this->yamlfout.close();
  }
  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    matcher.addMatcher(
      callExpr(callee(functionDecl(hasName("pthread_mutex_lock")))).bind("functions"),
      &handler_for_lock
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
            mark["filepath"] = sm.getFileManager().getVirtualFileSystem().getCurrentWorkingDirectory().get()
              + "/" + sm.getFileEntryForID(src_id)->getName().str();
            mark["line"] = sm.getSpellingLineNumber(loc);
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
    matcher.matchAST(Context);
    out << node;
    this->yamlfout << out.c_str();
  }
  void setYamlFout(std::string path) {
    this->yamlfout = std::ofstream(path.c_str());
  }
private:
  uint32_t mark_i = 0;
  std::ofstream yamlfout;
  MatchFinder matcher;
  SlotLockerHandler handler_for_lock;
  // SlotUnlockerHandler handler_for_unlock;
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
  void EndSourceFileAction() override {
    SourceManager &sm = rw.getSourceMgr();
    FileManager &fm = sm.getFileManager();
    // rw.getEditBuffer(sm.getOrCreateFileID(fm.getFile("test.cc").get(), SrcMgr::CharacteristicKind::C_User)).write(llvm::outs());
    rw.getEditBuffer(rw.getSourceMgr().getMainFileID())
              .write(llvm::outs());
  }
private:
  std::string yaml_path;
  Rewriter rw;
};


std::unique_ptr<FrontendActionFactory> newSlotIdentificationActionFactory(std::string yaml_conf) {
  class SlotIdentificationActionFactory: public FrontendActionFactory {
  public:
    std::unique_ptr<FrontendAction> create() override {
      std::unique_ptr<SlotIdentificationAction> action(new SlotIdentificationAction);
      action->setYamlPath(this->yaml_path);
      return action;
    };
    void setYamlPath(std::string path) {
      this->yaml_path = path;
    }
  private:
    std::string yaml_path;
  };
  std::unique_ptr<SlotIdentificationActionFactory> factory(new SlotIdentificationActionFactory);
  factory->setYamlPath(yaml_conf);
  return factory;
}

int main(int argc, const char **argv) {
  std::string err;
  const char *compiler_db_path = argv[1];
  std::unique_ptr<CompilationDatabase> compiler_db = CompilationDatabase::autoDetectFromSource(compiler_db_path, err);
  ClangTool tool(*compiler_db, compiler_db->getAllFiles());
  return tool.run(newSlotIdentificationActionFactory("test-output.yaml").get());
}
