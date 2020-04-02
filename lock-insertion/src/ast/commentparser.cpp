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

typedef std::map<std::string, std::vector<std::pair<uint32_t, FullSourceLoc>>> FilePath2InsertingPtMap_t;
typedef std::vector<std::pair<uint32_t, FullSourceLoc>> InsertingPts_t;

class SlotLockerHandler: public MatchFinder::MatchCallback {
public:
  SlotLockerHandler(Rewriter &rw, std::map<std::pair<std::string, uint32_t>, FullSourceLoc> *line2func_loc)
    : rw(rw), i(0), file_path2inserting_pt(file_path2inserting_pt) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("locks")) {
      ASTContext *Context = result.Context;
      SourceManager& sm = Context->getSourceManager();
      std::string slot_key = getAbsolutePath(sm.getFileEntryForID(sm.getFileID(e->getBeginLoc()))->getName());
      if ((*file_path2inserting_pt).find(slot_key) == (*file_path2inserting_pt).end())
        return;
      InsertingPts_t points = (*file_path2inserting_pt)[slot_key];
      int32_t i_loc = haveSlotComb(sm.getSpellingLineNumber(e->getBeginLoc()), points);
      if (i_loc < 0)
        return;
      FullSourceLoc func_begin = points[i_loc].second;
      std::string slot_name("___lock_slot_");
      slot_name += std::to_string(i);
      rw.InsertTextBefore(
        func_begin,
        "\nextern int " + slot_name + "(pthread_mutex_t *mutex);"
      );
      rw.ReplaceText(e->getCallee()->getSourceRange(), slot_name);
      i++;
    }
  }
  int32_t haveSlotComb(uint32_t matched_line, InsertingPts_t points) {
    for (uint32_t i = 0; i < points->size(); i++) {
      if (points[0].first == matched_line)
        return i;
    }
    return -1;
  }
private:
  unsigned int i;
  Rewriter &rw;
  FilePath2InsertingPtMap_t *file_path2inserting_pt;
};

class SlotUnlockerHandler: public MatchFinder::MatchCallback {
public:
  SlotUnlockerHandler(Rewriter &rw, std::map<std::pair<std::string, uint32_t>, FullSourceLoc> *line2func_loc)
  : rw(rw), i(0), line2func_loc(line2func_loc) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("unlocks")) {
      ASTContext *Context = result.Context;
      SourceManager& sm = Context->getSourceManager();
      std::pair<std::string, uint32_t> slot_key = make_pair(
        getAbsolutePath(sm.getFileEntryForID(sm.getFileID(e->getBeginLoc()))->getName()),
        sm.getSpellingLineNumber(e->getBeginLoc())
      );
      if ((*line2func_loc).find(slot_key) == (*line2func_loc).end())
        return;
      FullSourceLoc func_begin = (*line2func_loc)[slot_key];
      std::string slot_name("___unlock_slot_");
      slot_name += std::to_string(i);
      rw.InsertTextBefore(
        func_begin,
        "\nextern int " + slot_name + "(pthread_mutex_t *mutex);"
      );
      rw.ReplaceText(e->getCallee()->getSourceRange(), slot_name);
      i++;
    }
  }
private:
  unsigned int i;
  Rewriter &rw;
  std::map<std::pair<std::string, uint32_t>, FullSourceLoc> *line2func_loc;
};

class SlotIdentificationConsumer : public clang::ASTConsumer {
public:
  explicit SlotIdentificationConsumer(ASTContext *Context, Rewriter &rw)
    : handler_for_lock(rw, &line2func_loc), handler_for_unlock(rw, &line2func_loc) {}
  ~SlotIdentificationConsumer() {
    this->yamlfout.close();
  }
  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    matcher.addMatcher(
      callExpr(callee(functionDecl(hasName("pthread_mutex_lock")))).bind("locks"),
      &handler_for_lock
    );
    matcher.addMatcher(
      callExpr(callee(functionDecl(hasName("pthread_mutex_unlock")))).bind("unlocks"),
      &handler_for_unlock
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
            mark["filepath"] = getAbsolutePath(sm.getFileEntryForID(src_id)->getName());
            mark["line"] = sm.getSpellingLineNumber(loc);
            line2func_loc.insert(
              std::make_pair(
                std::make_pair(mark["filepath"].as<std::string>(), mark["line"].as<uint32_t>()),
                Context.getFullLoc(decl->getBeginLoc()))
            );
            printf("Insertion line is %s %u\n",
                getAbsolutePath(sm.getFileEntryForID(sm.getFileID(decl->getBeginLoc()))->getName()).c_str(),
                Context.getFullLoc(decl->getBeginLoc())
            );
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
  SlotUnlockerHandler handler_for_unlock;
  std::map<std::pair<std::string, uint32_t>, FullSourceLoc> line2func_loc;
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
    SourceManager &sm = rw.getSourceMgr();
    FileManager &fm = sm.getFileManager();
    std::string buffer;
    raw_string_ostream stream(buffer);
    rw.getEditBuffer(rw.getSourceMgr().getMainFileID())
              .write(stream);
    rw.getEditBuffer(rw.getSourceMgr().getMainFileID())
              .write(llvm::outs());
    std::vector<std::string>::iterator it = std::find(
      compiler_db->getAllFiles().begin(),
      compiler_db->getAllFiles().end(),
      getAbsolutePath(getCurrentFile())
    );
    if (it == compiler_db->getAllFiles().end()) {
      fprintf(
        stderr,
        "Can't identify the index of current file: \n%s\n",
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
