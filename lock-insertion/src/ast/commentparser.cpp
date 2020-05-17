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
#include <tuple>
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

std::string processing_dir = ".processing/";

// Consider FileID and line number combination as an unique ID.
typedef std::pair<FileID, uint32_t> LocID;
class Dylinx {
public:
  static Dylinx& Instance() {
    static Dylinx dylinx;
    return dylinx;
  }
  std::vector<LocID> commented_locks;
  std::pair<FileID, uint32_t> last_altered_loc;
  Rewriter rw;
  std::map<FileID, bool> should_header_modify;
  std::string yaml_path;
  YAML::Node lock_decl;
  uint32_t lock_i = 0;
private:
  Dylinx() {};
  ~Dylinx() {};
};

class FuncInterfaceMatchHandler: public MatchFinder::MatchCallback {
public:
  FuncInterfaceMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("interfaces")) {
      SourceManager& sm = result.Context->getSourceManager();
      FileID src_id = sm.getFileID(e->getBeginLoc());
      Dylinx::Instance().rw.ReplaceText(
        e->getCallee()->getSourceRange(),
        interface_LUT[e->getDirectCallee()->getNameInfo().getName().getAsString()]
      );
      Dylinx::Instance().should_header_modify[src_id] = true;
    }
  }
private:
  std::map<std::string, std::string>interface_LUT = {
    {"pthread_mutex_init", "___dylinx_init_"},
    {"pthread_mutex_lock", "___dylinx_lock_"},
    {"pthread_mutex_unlock", "___dylinx_unlock_"},
    {"pthread_mutex_destroy", "___dylinx_destroy_"}
  };
};

// class TypedefMatchHandler: public MatchFinder::MatchCallback {
// public:
//   TypedefMatchHandler() {}
//   virtual void run(const MatchFinder::MatchResult &result) {
//     if (const TypedefNameDecl *d = result.Nodes.getNodeAs<TypedefNameDecl>("typedefs")) {
//       SourceManager& sm = result.Context->getSourceManager();
//       SourceLocation loc = d->getBeginLoc();
//     }
//   }
// };

class MemAllocaMatchHandler: public MatchFinder::MatchCallback {
public:
  MemAllocaMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    const CallExpr *decl_expr = result.Nodes.getNodeAs<CallExpr>("malloc2decls");
    const CallExpr *assign_expr = result.Nodes.getNodeAs<CallExpr>("malloc2assign");
    const CallExpr *e = decl_expr? decl_expr: assign_expr;
    if(!e)
      return;
    SourceManager& sm = result.Context->getSourceManager();
    SourceLocation loc = e->getBeginLoc();
    FileID src_id = sm.getFileID(loc);
    uint32_t line = sm.getSpellingLineNumber(loc);
    YAML::Node alloca;
    alloca["file_name"] = sm.getFileEntryForID(src_id)->getName().str();
    alloca["line"] = line;
    alloca["id"] = Dylinx::Instance().lock_i;
    LocID com = std::make_pair(src_id, line);
    std::vector<LocID>::iterator iter = std::find(
      Dylinx::Instance().commented_locks.begin(),
      Dylinx::Instance().commented_locks.end(),
      com
    );
    if (iter != Dylinx::Instance().commented_locks.end())
      alloca["is_commented"] = 1;
    else
      alloca["is_commented"] = 0;
    char format[50];
    sprintf(format, ", DYLINX_LOCK_MACRO_%d)", Dylinx::Instance().lock_i);
    printf("format is %s\n", format);
    Dylinx::Instance().rw.InsertTextBefore(e->getBeginLoc(), "__dylinx_ptr_init_(");
    Dylinx::Instance().rw.InsertTextAfter(e->getRParenLoc().getLocWithOffset(1), format);
    SourceLocation sizeof_begin = dyn_cast<UnaryExprOrTypeTraitExpr>(e->getArg(0))->getBeginLoc();
    Dylinx::Instance().rw.ReplaceText(sizeof_begin.getLocWithOffset(7), 15, "AbstractLock");
    Dylinx::Instance().lock_i++;
    Dylinx::Instance().lock_decl["LockEntity"].push_back(alloca);
  }
};

class VarsMatchHandler: public MatchFinder::MatchCallback {
public:
  VarsMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    // Take care to the varDecl in the function argument
    if (const VarDecl *d = result.Nodes.getNodeAs<VarDecl>("vars")) {
      SourceManager& sm = result.Context->getSourceManager();
      SourceLocation loc = d->getBeginLoc();
      FileID src_id = sm.getFileID(loc);
      uint32_t line = sm.getSpellingLineNumber(loc);
      printf("Hash:%u path:%s Line %u variable name is %s @(%u, %u) isFunctionDecl: %u\n",
        src_id.getHashValue(),
        loc.printToString(sm).c_str(),
        line,
        d->getName().str().c_str(),
        sm.getSpellingColumnNumber(d->getBeginLoc()),
        sm.getSpellingColumnNumber(d->getEndLoc()),
        d->isFunctionOrMethodVarDecl()
      );
      LocID key = std::make_pair(src_id, line);
      const ParmVarDecl *pvd = dyn_cast<ParmVarDecl>(d);
      std::vector<LocID>::iterator iter = std::find(
        Dylinx::Instance().commented_locks.begin(),
        Dylinx::Instance().commented_locks.end(),
        key
      );

      if (iter != Dylinx::Instance().commented_locks.end()) {
        // Variable declaration with comment mark.
        if (!strcmp(d->getType().getAsString().c_str(), "pthread_mutex_t")) {
          char format[50];
          sprintf(format, " = DYLINX_LOCK_MACRO_%d", Dylinx::Instance().lock_i);
          Dylinx::Instance().rw.InsertTextAfterToken(d->getEndLoc(), format);
          YAML::Node loc;
          loc["file_name"] = sm.getFileEntryForID(src_id)->getName().str();
          loc["line"] = sm.getSpellingLineNumber(d->getBeginLoc());
          loc["id"] = Dylinx::Instance().lock_i;
          loc["is_commented"] = 1;
          Dylinx::Instance().lock_i++;
          Dylinx::Instance().lock_decl["LockEntity"].push_back(loc);
        }
        if (Dylinx::Instance().last_altered_loc != key) {
          Dylinx::Instance().rw.ReplaceText(d->getBeginLoc(), 15, "AbstractLock");
        }
        Dylinx::Instance().should_header_modify[src_id] = true;
        Dylinx::Instance().last_altered_loc = key;
      } else if (!sm.isInSystemHeader(loc) && pvd) {
        // Variable declaration in function arguments list.
        Dylinx::Instance().rw.ReplaceText(d->getBeginLoc(), 15, "AbstractLock");
        Dylinx::Instance().should_header_modify[src_id] = true;
      } else {
        // Commentless lock declaration.

        // Prevent pthread_mutex_t variable declaration in system header
        if (sm.isInSystemHeader(loc))
          return;

        Dylinx::Instance().should_header_modify[src_id] = true;
        if (!strcmp(d->getType().getAsString().c_str(), "pthread_mutex_t")) {
          char format[50];
          sprintf(format, " = DYLINX_LOCK_MACRO_%d", Dylinx::Instance().lock_i);
          Dylinx::Instance().rw.InsertTextAfterToken(d->getEndLoc(), format);
          YAML::Node loc;
          loc["file_name"] = sm.getFileEntryForID(src_id)->getName().str();
          loc["line"] = sm.getSpellingLineNumber(d->getBeginLoc());
          loc["id"] = Dylinx::Instance().lock_i;
          loc["is_commented"] = 0;
          Dylinx::Instance().lock_i++;
          Dylinx::Instance().lock_decl["LockEntity"].push_back(loc);
        }
        if (Dylinx::Instance().last_altered_loc != key) {
          Dylinx::Instance().rw.ReplaceText(d->getBeginLoc(), 15, "AbstractLock");
        }
        Dylinx::Instance().should_header_modify[src_id] = true;
        Dylinx::Instance().last_altered_loc = key;
      }
    }
  }
};

class SlotIdentificationConsumer : public clang::ASTConsumer {
public:
  explicit SlotIdentificationConsumer( ASTContext *Context) {}
  ~SlotIdentificationConsumer() {
    this->yamlfout.close();
  }
  virtual void HandleTranslationUnit(clang::ASTContext &Context) {

    // Match all
    //    pthread_mutex_t mutex;
    //    pthread_mutex_t *mutex_ptr;
    // and convert to
    //    BaseLock_t lock;
    //    BaseLock_t *lock;
    matcher.addMatcher(
      varDecl(eachOf(
        hasType(asString("pthread_mutex_t")), hasType(asString("pthread_mutex_t *"))
      )).bind("vars"),
      &handler_for_vars
    );

    // Match all
    //    typedef pthread_mutex_t MyLock
    //    typedef pthread_mutext_t *MyLockPtr
    // and convert them to
    //    typedef BaseLock MyLock
    //    typedef BaseLock *MyLock

    //! TODO Not implement yet
    // matcher.addMatcher(
    //   typedefNameDecl(eachOf(
    //     hasType(asString("pthread_mutex_t")),
    //     hasType(asString("pthread_mutex_t *"))
    //   )).bind("typedefs")
    //   &handler_for_typedef
    // );

    // Match all
    //    malloc(sizeof(pthread_mutex_t));
    // and convert them to
    //    __dylinx_ptr_init(malloc(sizeof(pthread_mutex_t)));
    // ! Note:
    // Dylinx refuses to implement support for declaring multiple
    // lock instance through malloc.
    //    [NOT SUPPORT] pthread_mutex_t *mutex = malloc(100 * sizeof(pthread_mutex_t));
    //    [NOT SUPPORT] pthread_mutex_t mutex[100];
    //
    //! TODO Not implement yet
    matcher.addMatcher(
      varDecl(
        hasType(asString("pthread_mutex_t *")),
        hasInitializer(hasDescendant(
          callExpr(
            callee(functionDecl(hasName("malloc"))),
            hasArgument(0, sizeOfExpr(hasArgumentOfType(qualType(asString("pthread_mutex_t"))))
          )).bind("malloc2decls")
        ))).bind("mutex_ptr_decl"),
      &handler_for_mem_alloca
    );

    matcher.addMatcher(
      binaryOperator(
        hasOperatorName("="),
        hasRHS(hasDescendant(
         callExpr(callee(
          functionDecl(hasName("malloc"))),
          hasArgument(0, sizeOfExpr(hasArgumentOfType(qualType(asString("pthread_mutex_t"))))
        )).bind("malloc2assign")))
      ).bind("assignment"),
      &handler_for_mem_alloca
    );


    matcher.addMatcher(
      callExpr(eachOf(
        callee(functionDecl(hasName("pthread_mutex_lock"))),
        callee(functionDecl(hasName("pthread_mutex_unlock"))),
        callee(functionDecl(hasName("pthread_mutex_init"))),
        callee(functionDecl(hasName("pthread_mutex_destroy")))
      )).bind("interfaces"),
      &handler_for_interface
    );

    YAML::Emitter out;
    YAML::Node node;
    SourceManager& sm = Context.getSourceManager();
    auto decls = Context.getTranslationUnitDecl()->decls();
    YAML::Node markers;
    for (auto &decl: decls) {
      if (sm.isInSystemHeader(decl->getLocation()) || !isa<FunctionDecl>(decl) && !isa<LinkageSpecDecl>(decl))
        continue;
      Dylinx::Instance().should_header_modify[sm.getFileID(decl->getLocation())] = false;
      if (auto *comments = Context.getRawCommentList().getCommentsInFile(sm.getFileID(decl->getLocation()))) {
        Dylinx::Instance().should_header_modify[sm.getFileID(decl->getLocation())] = true;
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
            Dylinx::Instance().commented_locks.push_back(std::make_pair(src_id, line));
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
    matcher.matchAST(Context);
    out << node;
    out << Dylinx::Instance().lock_decl;
    this->yamlfout << out.c_str();
  }
  void setYamlFout(std::string path) {
    this->yamlfout = std::ofstream(path.c_str());
  }
private:
  uint32_t mark_i = 0;
  std::ofstream yamlfout;
  MatchFinder matcher;
  FuncInterfaceMatchHandler handler_for_interface;
  VarsMatchHandler handler_for_vars;
  MemAllocaMatchHandler handler_for_mem_alloca;
};

class SlotIdentificationAction : public clang::ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile
  ) {
    Dylinx::Instance().rw.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
    std::unique_ptr<SlotIdentificationConsumer> consumer(
      new SlotIdentificationConsumer(&Compiler.getASTContext()
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
    SourceManager &sm = Dylinx::Instance().rw.getSourceMgr();
    FileManager &fm = sm.getFileManager();
    std::map<FileID, bool>::iterator dep_f;
    for (
      dep_f = Dylinx::Instance().should_header_modify.begin();
      dep_f != Dylinx::Instance().should_header_modify.end();
      dep_f++
    ) {
      // TODO
      // Use #ifdef to prevent double declaration of glue.h
      std::string filename = sm.getFileEntryForID(dep_f->first)->getName().str();
      printf("modifying %s\n", filename.c_str());
      size_t dir_pos = filename.find_last_of("/");
      filename = filename.substr(dir_pos + 1, filename.length());
      std::string copy_path = processing_dir + filename;
      printf("copy to %s \n", copy_path.c_str());
      if (dep_f->second) {
        Dylinx::Instance().rw.InsertText(sm.getLocForStartOfFile(dep_f->first), "#include \"glue.h\"\n");
        std::error_code err;
        raw_fd_ostream fstream(processing_dir + filename, err);
        Dylinx::Instance().rw.getEditBuffer(dep_f->first).write(fstream);
      } else {
        fs::copy(
          getAbsolutePath(sm.getFileEntryForID(dep_f->first)->getName()),
          processing_dir + filename
        );
      }
    }
    return;
    // TODO
    // overflow security issue;
    char compile_cmd[500];
    std::string main_file = sm.getFileEntryForID(Dylinx::Instance().rw.getSourceMgr().getMainFileID())->getName().str();
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
      // std::remove(filename.c_str());
    }
    // Print modified source code.
    Dylinx::Instance().rw.getEditBuffer(Dylinx::Instance().rw.getSourceMgr().getMainFileID())
              .write(llvm::outs());
  }
private:
  std::string yaml_path;
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
