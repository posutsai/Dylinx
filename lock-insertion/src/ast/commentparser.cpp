#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/Support/CommandLine.h"
#include "clang/Tooling/Tooling.h"
#include <iostream>

using namespace clang::tooling;
using namespace llvm;
using namespace clang;

class FindCommentsConsumer : public clang::ASTConsumer {
public:
  explicit FindCommentsConsumer(ASTContext *Context) {}

  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    SourceManager& sm = Context.getSourceManager();
    auto *comments = Context.getRawCommentList().getCommentsInFile(sm.getMainFileID());
    for (auto comment : *comments) {
      std::cout << comment.second->getRawText(Context.getSourceManager()).str() << std::endl;
    }
    // std::cout << "Finished parsing for comments" << std::endl;
    // llvm::DenseMap<const Decl *, const RawComment *> map = Context.DeclRawComments;
    // llvm::outs() << "The size of comment_map " << map.size() << '\n';
    // llvm::DenseMap<const Decl *, const RawComment *>::iterator it = map.begin();
    // while (it != map.end()) {
    //   // it->second->dump();
    //   llvm::outs() << it->second->getRawText(sm) << '\n';
    // }
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

static llvm::cl::OptionCategory MyToolCategory("My tool options");
int main(int argc, const char **argv) {
      CommonOptionsParser OptionsParser(argc, argv, MyToolCategory);
      ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
      return Tool.run(newFrontendActionFactory<FindCommentsAction>().get());
}
