#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Inclusions/HeaderIncludes.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Casting.h"
#include <iostream>
#include <system_error>
#include <filesystem>
#include <array>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <string>
#include <regex>
#include <utility>
#include "yaml-cpp/yaml.h"
#include "util.h"

// TODO
// 1. Implement the header file for definition of
//    each lock and glue code.
// 2. Copy all the dependency to the processing dir.
// 3. If there is modifications in dependent headers,
//    Dylinx should rename them to unique temp name
//    and modify the code in "#include <....>" part.
// 4. Things getting more complicated when users
//    implement their function in different files.
//    For example:
//
//    ------------------------------------------
//    > In foo.h
//    void foo();
//    -------------------------------------------
//    > In foo.cpp
//    void foo() { }
//    ------------------------------------------
//
//    In this case, merging ASTs should be considered.
//    Or maybe compiler_commands.json would solve it
//    for us. Still not test with "bear".

using namespace clang;
using namespace llvm;
using namespace clang::tooling;
using namespace clang::ast_matchers;
namespace fs = std::filesystem;

uint64_t g_lock_cnt = 0;
std::string processing_dir = "./processing/";

typedef std::pair<FileID, uint32_t> CommentID;

class SlotFuncInterfaceHandler: public MatchFinder::MatchCallback {
public:
  SlotFuncInterfaceHandler(Rewriter &rw): rw(rw) {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("interfaces")) {
      rw.ReplaceText(
        e->getCallee()->getSourceRange(),
        interface_LUT[e->getDirectCallee()->getNameInfo().getName().getAsString()]
      );
    }
  }
private:
  Rewriter &rw;
  std::map<std::string, std::string>interface_LUT = {
    {"pthread_mutex_init", "___dylinx_slot_init_"},
    {"pthread_mutex_lock", "___dylinx_slot_lock_"},
    {"pthread_mutex_unlock", "___dylinx_slot_unlock_"},
    {"pthread_mutex_destroy", "___dylinx_slot_destroy_"}
  };
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
      // } else if (const ParmVarDecl *pvd = dyn_cast<ParmVarDecl>(d)) {
      //     rw.ReplaceText(d->getBeginLoc(), 15, "BaseLock");
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
  explicit SlotIdentificationConsumer(
    ASTContext *Context,
    Rewriter &rw,
    std::map<FileID, bool> *is_req_header_modified
  ):
    handler_for_interface(rw),
    handler_for_vars(rw, &commented_locks, &replaced_vars),
    handler_for_func_args(rw, &replaced_vars),
    is_req_header_modified(is_req_header_modified)
  {}
  ~SlotIdentificationConsumer() {
    this->yamlfout.close();
  }
  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    matcher_1st.addMatcher(
      varDecl(eachOf(
          hasType(asString("pthread_mutex_t")), hasType(asString("pthread_mutex_t *"))
      )).bind("vars"),
      &handler_for_vars
    );
    matcher_2nd.addMatcher(
      callExpr(eachOf(
        callee(functionDecl(hasName("pthread_mutex_lock"))),
        callee(functionDecl(hasName("pthread_mutex_unlock"))),
        callee(functionDecl(hasName("pthread_mutex_init"))),
        callee(functionDecl(hasName("pthread_mutex_destroy")))
      )).bind("interfaces"),
      &handler_for_interface
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
      (*is_req_header_modified)[sm.getFileID(decl->getLocation())] = false;
      if (auto *comments = Context.getRawCommentList().getCommentsInFile(
            sm.getFileID(decl->getLocation()))) {
        (*is_req_header_modified)[sm.getFileID(decl->getLocation())] = true;
        for (auto cmt : *comments) {
          YAML::Node mark;
          std::string comb = cmt.second->getRawText(Context.getSourceManager()).str();
          std::smatch conf_sm;
          std::regex re("\\/\\/! \\[LockSlot\\](.*)");
          std::regex_match(comb, conf_sm, re);
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
  SlotFuncInterfaceHandler handler_for_interface;
  SlotVarsHandler handler_for_vars;
  SlotFuncArgumentHandler handler_for_func_args;
  std::map<CommentID, bool> commented_locks;
  std::map<std::string, uint32_t> replaced_vars;
  std::map<FileID, bool> *is_req_header_modified;
};

class SlotIdentificationAction : public clang::ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile
  ) {
    rw.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
    std::unique_ptr<SlotIdentificationConsumer> consumer(new SlotIdentificationConsumer(
      &Compiler.getASTContext(),
      rw,
      &is_req_header_modified
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
    std::map<FileID, bool>::iterator dep_f;
    for (
      dep_f = is_req_header_modified.begin();
      dep_f != is_req_header_modified.end();
      dep_f++
    ) {
      // TODO
      // Use #ifdef to prevent double declaration of glue.h
      std::string filename = sm.getFileEntryForID(dep_f->first)->getName().str();
      size_t dir_pos = filename.find_last_of("/");
      filename = filename.substr(dir_pos + 1, filename.length());
      if (dep_f->second) {
        rw.InsertText(sm.getLocForStartOfFile(dep_f->first), "#include \"glue.h\"\n");
        std::error_code err;
        raw_fd_ostream fstream(processing_dir + filename, err);
        rw.getEditBuffer(dep_f->first) .write(fstream);
      } else {
        fs::copy(
          getAbsolutePath(sm.getFileEntryForID(dep_f->first)->getName()),
          processing_dir + filename
        );
      }
    }
    // TODO
    // overflow security issue;
    char compile_cmd[500];
    std::string main_file = sm.getFileEntryForID(rw.getSourceMgr().getMainFileID())->getName().str();
    size_t dir_pos = main_file.find_last_of("/");
    main_file = main_file.substr(dir_pos + 1, main_file.length());
    size_t dot_pos = main_file.find_last_of(".");
    std::string bc_name = main_file.substr(0, dot_pos) + ".ll";
    sprintf(
      compile_cmd,
      "clang++ -S -emit-llvm %s%s -o %s%s",
      processing_dir.c_str(),
      main_file.c_str(),
      processing_dir.c_str(),
      bc_name.c_str()
    );
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(compile_cmd, "r"), pclose);
    if (!pipe) {
      throw std::runtime_error("popen() failed!");
    }

    std::string result;
    std::array<char, 128> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
      result += buffer.data();
    }

    for (
      dep_f = is_req_header_modified.begin();
      dep_f != is_req_header_modified.end();
      dep_f++
    ) {
      std::string filename = sm.getFileEntryForID(dep_f->first)->getName().str();
      std::size_t dir_pos = filename.find_last_of("/");
      filename = filename.substr(dir_pos + 1, filename.length());
      filename = processing_dir + filename;
      std::remove(filename.c_str());
    }
    rw.getEditBuffer(rw.getSourceMgr().getMainFileID())
              .write(llvm::outs());
  }
private:
  std::string yaml_path;
  Rewriter rw;
  std::shared_ptr<CompilationDatabase> compiler_db;
  std::map<FileID, bool> is_req_header_modified;
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
