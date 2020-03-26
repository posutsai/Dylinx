#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Casting.h"
#include "clang/Tooling/Tooling.h"
#include <iostream>
#include <cstdio>
#include <cstdint>
#include <string>
#include <regex>

using namespace clang::tooling;
using namespace clang;
using namespace llvm;
class FindCommentsConsumer : public clang::ASTConsumer {
public:
  explicit FindCommentsConsumer(ASTContext *Context) {}

  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    SourceManager& sm = Context.getSourceManager();
    auto decls = Context.getTranslationUnitDecl()->decls();
    for (auto &decl: decls) {
      if (sm.isInSystemHeader(decl->getLocation()) || !isa<FunctionDecl>(decl) && !isa<LinkageSpecDecl>(decl))
        continue;
      if (auto *comments = Context.getRawCommentList().getCommentsInFile(
            sm.getFileID(decl->getLocation()))) {
        for (auto cmt : *comments) {
          std::cout << "vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n";
          llvm::outs() << "At " << sm.getFilename(decl->getLocation()).str() << '\n'
            << cmt.second->getRawText(Context.getSourceManager()).str() << '\n';
          std::cout << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
          std::string comb = cmt.second->getRawText(Context.getSourceManager()).str();
          std::smatch sm;
          std::regex re("\\/\\/! \\[LockSlot\\](.*)");
          std::regex_match(comb, sm, re);
          for (uint32_t i = 0; i < sm.size(); i++) {
            std::cout << sm[i] << std::endl;
          }
        }
      }
    }
    // auto *comments = Context.getRawCommentList().getCommentsInFile(sm.getMainFileID());
  }
};

class FindCommentsAction : public clang::ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new FindCommentsConsumer(&Compiler.getASTContext()));
  }
};


int main(int argc, const char **argv) {
  std::string err;
  const char *compiler_db_path = argv[1];
  std::unique_ptr<CompilationDatabase> compiler_db = CompilationDatabase::autoDetectFromSource(compiler_db_path, err);
  ClangTool tool(*compiler_db, compiler_db->getAllFiles());
  return tool.run(newFrontendActionFactory<FindCommentsAction>().get());
}
