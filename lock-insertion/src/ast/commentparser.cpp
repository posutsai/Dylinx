#include "clang/Lex/Lexer.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Comment.h"
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
#include <cstring>
#include <regex>
#include <utility>
#include "yaml-cpp/yaml.h"
#include "util.h"

#define ARR_ALLOCA_TYPE "ARRAY"
#define VAR_ALLOCA_TYPE "VARIABLE"
#define STRUCT_MEM_ALLOCA_TYPE "STRUCT_MEMBER"
#define TYPEDEF_ALLOCA_TYPE "TYPEDEF"

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

const Token *move2n_token(SourceLocation loc, uint32_t n, SourceManager& sm, const LangOptions& opts) {
  SourceLocation arrive = loc;
  Token *token;
  for (int i = 0; i < n; i++) {
    token = Lexer::findNextToken(
      arrive, sm, opts
    ).getPointer();
    arrive = token->getLocation();
  }
  return token;
}

YAML::Node parse_comment(std::string raw_text) {
  // exterior match
  std::smatch config_result;
  std::regex re("\\[LockSlot\\](.*)");
  std::regex_match(raw_text, config_result, re);
  printf("raw_text is %s and match size is %lu\n",raw_text.c_str(), config_result.size());
  YAML::Node cmb;
  if (config_result.size() == 2) { // meet exterior condition
    std::smatch comb_result;
    std::string combination = config_result[1];
    std::regex lock_pattern(getLockPattern());
    while(std::regex_search(combination, comb_result, lock_pattern)) {
      std::string t = comb_result[1];
      printf("matched %s\n", t.c_str());
      combination = comb_result.suffix().str();
      cmb.push_back(t);
    }
  }
  return cmb;
}

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
    {"pthread_mutex_init", "__dylinx_generic_init_"},
    {"pthread_mutex_lock", "__dylinx_generic_enable_"},
    {"pthread_mutex_unlock", "__dylinx_generic_disable_"},
    {"pthread_mutex_destroy", "__dylinx_generic_destroy_"}
  };
};

class MemAllocaMatchHandler: public MatchFinder::MatchCallback {
public:
  MemAllocaMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    const CallExpr *e;
    std::string ptr_name;
    bool new_decl;
    if (const VarDecl *vd = result.Nodes.getNodeAs<VarDecl>("malloc_decl")) {
      ptr_name = vd->getNameAsString();
      e = result.Nodes.getNodeAs<CallExpr>("malloc_decl_callexpr");
      new_decl = true;
    } else if (const BinaryOperator *binop = result.Nodes.getNodeAs<BinaryOperator>("malloc_assign")) {
      DeclRefExpr *drefexpr = dyn_cast<DeclRefExpr>(binop->getLHS());
      ptr_name = drefexpr->getNameInfo().getAsString();
      e = result.Nodes.getNodeAs<CallExpr>("malloc_assign_callexpr");
      new_decl = false;
    } else {
      perror("Runtime error malloc matcher operation fault!\n");
    }
    if(!e)
      return;
    SourceManager& sm = result.Context->getSourceManager();
    SourceLocation loc = e->getBeginLoc();
    FileID src_id = sm.getFileID(loc);
    uint32_t line = sm.getSpellingLineNumber(loc);
    char arr_init[100];
    SourceLocation size_begin = e->getArg(0)->getBeginLoc();
    SourceLocation size_end = e->getRParenLoc();
    SourceRange range(size_begin, size_end);
    sprintf(
      arr_init, " FILL_ARRAY(%s, %s);",
      ptr_name.c_str(),
      Lexer::getSourceText(CharSourceRange(range, false), sm, result.Context->getLangOpts()).str().c_str()
    );
    Dylinx::Instance().rw.InsertTextAfter(
      e->getEndLoc().getLocWithOffset(2),
      arr_init
    );
    if (!new_decl)
      return;
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
    alloca["is_commented"] = iter == Dylinx::Instance().commented_locks.end()? 0: 1;
    Dylinx::Instance().lock_decl["LockEntity"].push_back(alloca);
  }
};

class ArrayMatchHandler: public MatchFinder::MatchCallback {
public:
  ArrayMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const VarDecl *d = result.Nodes.getNodeAs<VarDecl>("array_decls")) {
      SourceManager& sm = result.Context->getSourceManager();
      SourceLocation loc = d->getBeginLoc();
      FileID src_id = sm.getFileID(loc);
      uint32_t line = sm.getSpellingLineNumber(loc);
      YAML::Node alloca;
      if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(d))
        alloca["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));
      alloca["file_name"] = sm.getFileEntryForID(src_id)->getName().str();
      alloca["line"] = line;
      alloca["id"] = Dylinx::Instance().lock_i;
      alloca["modification_type"] = ARR_ALLOCA_TYPE;
      SourceLocation size_begin, size_end;
      char array_size[50];
      if (const ConstantArrayType *arr_type = result.Context->getAsConstantArrayType(d->getType())) {
        sprintf(array_size, "%lu", arr_type->getSize().getZExtValue());
      } else if (const VariableArrayType *arr_type = result.Context->getAsVariableArrayType(d->getType())) {
        size_begin = arr_type->getLBracketLoc().getLocWithOffset(1);
        size_end = arr_type->getRBracketLoc();
        SourceRange size_range(size_begin, size_end);
        sprintf(array_size, "%s", Lexer::getSourceText(CharSourceRange(size_range, false), sm, result.Context->getLangOpts()).str().c_str());
      } else { perror("Array type is neither constant nor variable\n"); };

      char type_macro[50];
      sprintf(type_macro, "DYLINX_LOCK_MACRO_%d", Dylinx::Instance().lock_i);
      if (d->getStorageClass() == StorageClass::SC_Static) {
        loc = Lexer::findNextToken(loc, sm, result.Context->getLangOpts())->getLocation();
        Dylinx::Instance().rw.ReplaceText(loc, 15, type_macro);
        alloca["extra_init"] = 1;
      } else
        Dylinx::Instance().rw.ReplaceText(d->getBeginLoc(), 15, type_macro);

      if (d->getStorageClass() != StorageClass::SC_Static || d->isStaticLocal()) {
        char arr_init[100];
        sprintf(
          arr_init,
          " FILL_ARRAY( %s, (%s) * sizeof(pthread_mutex_t));",
          d->getNameAsString().c_str(),
          array_size
        );
        Dylinx::Instance().rw.InsertTextAfter(
          d->getEndLoc().getLocWithOffset(2),
          arr_init
        );
      }
      Dylinx::Instance().lock_i++;
      Dylinx::Instance().lock_decl["LockEntity"].push_back(alloca);
      Dylinx::Instance().should_header_modify[src_id] = true;
    }
  }
};

class TypedefMatchHandler: public MatchFinder::MatchCallback {
public:
  TypedefMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const TypedefDecl *d = result.Nodes.getNodeAs<TypedefDecl>("typedefs")) {
      SourceManager& sm = result.Context->getSourceManager();
      const Token *token = move2n_token(d->getBeginLoc(), 2, sm, result.Context->getLangOpts());
      char format[100];
      sprintf(format, "DYLINX_LOCK_MACRO_%d", Dylinx::Instance().lock_i);
      Dylinx::Instance().rw.ReplaceText(
        token->getLocation(),
        token->getLength(),
        format
      );
      SourceLocation loc = d->getBeginLoc();
      FileID src_id = sm.getFileID(loc);
      uint32_t line = sm.getSpellingLineNumber(loc);
      YAML::Node lock_meta;
      if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(d))
        lock_meta["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));
      lock_meta["file_name"] = sm.getFileEntryForID(src_id)->getName().str();
      lock_meta["line"] = line;
      lock_meta["id"] = Dylinx::Instance().lock_i;
      lock_meta["modification_type"] = TYPEDEF_ALLOCA_TYPE;
      LocID com = std::make_pair(src_id, line);
      std::vector<LocID>::iterator iter = std::find(
        Dylinx::Instance().commented_locks.begin(),
        Dylinx::Instance().commented_locks.end(),
        com
      );
      lock_meta["is_commented"] = iter == Dylinx::Instance().commented_locks.end()? 0: 1;
      Dylinx::Instance().lock_i++;
      Dylinx::Instance().lock_decl["LockEntity"].push_back(lock_meta);
      Dylinx::Instance().should_header_modify[src_id] = true;
    }
  }
};

class StructFieldMatchHandler: public MatchFinder::MatchCallback {
public:
  StructFieldMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const FieldDecl *fd = result.Nodes.getNodeAs<FieldDecl>("struct_members")) {
      SourceManager& sm = result.Context->getSourceManager();
      SourceLocation loc = fd->getBeginLoc();
      FileID src_id = sm.getFileID(loc);
      uint32_t line = sm.getSpellingLineNumber(loc);
      LocID key = std::make_pair(src_id, line);
      std::vector<LocID>::iterator iter = std::find(
        Dylinx::Instance().commented_locks.begin(),
        Dylinx::Instance().commented_locks.end(),
        key
      );
      char format[50];
      sprintf(format, "DYLINX_LOCK_MACRO_%d", Dylinx::Instance().lock_i);
      Dylinx::Instance().rw.ReplaceText(loc, 15, format);
      YAML::Node decl_loc;
      if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(fd))
        decl_loc["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));
      decl_loc["file_name"] = sm.getFileEntryForID(src_id)->getName().str();
      decl_loc["line"] = sm.getSpellingLineNumber(fd->getBeginLoc());
      decl_loc["id"] = Dylinx::Instance().lock_i;
      decl_loc["modification_type"] = STRUCT_MEM_ALLOCA_TYPE;
      Dylinx::Instance().lock_i++;
      Dylinx::Instance().lock_decl["LockEntity"].push_back(decl_loc);
      Dylinx::Instance().should_header_modify[src_id] = true;
    }
  }
};

class PtrRefMatchHandler: public MatchFinder::MatchCallback {
public:
  PtrRefMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("ptr_ref")) {
      //! config from config.yml
      SourceManager& sm = result.Context->getSourceManager();
      std::string func_name = e->getDirectCallee()->getNameInfo().getName().getAsString();
      std::vector<std::string>::iterator iter = std::find(
          interfaces.begin(), interfaces.end(), func_name
      );
      if (iter != interfaces.end())
        return;
      for (int i = 0; i < e->getNumArgs(); i++) {
        printf("i == %d\n",i);
        const Expr *arg = e->getArg(i);
        SourceLocation begin = arg->getBeginLoc();
        SourceLocation end;
        if (i == e->getNumArgs() -1)
          end = e->getRParenLoc();
        else {
          end = e->getArg(i+1)->getBeginLoc();
          CharSourceRange src_rng;
          src_rng.setBegin(begin);
          src_rng.setEnd(end);
          std::string with_comma = Lexer::getSourceText(src_rng, sm, result.Context->getLangOpts()).str();
          size_t found = with_comma.find_last_of(",");
          std::string without_comma = with_comma.substr(0, found).c_str();
          end = begin.getLocWithOffset(without_comma.length());
        }
        if (!strcmp(arg->getType().getAsString().c_str(), "pthread_mutex_t *")) {
          Dylinx::Instance().rw.InsertTextBefore(begin, "__dylinx_generic_cast_(");
          Dylinx::Instance().rw.InsertTextAfter(end, ")");
        }
      }
    }
  }
private:
  std::vector<std::string> interfaces = {
    "pthread_mutex_init",
    "pthread_mutex_lock",
    "pthread_mutex_unlock",
    "pthread_mutex_destroy"
  };
};

class VarsMatchHandler: public MatchFinder::MatchCallback {
public:
  VarsMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    // Take care to the varDecl in the function argument
    if (const VarDecl *d = result.Nodes.getNodeAs<VarDecl>("vars")) {
      SourceManager& sm = result.Context->getSourceManager();
      SourceLocation loc = d->getBeginLoc();
      if (sm.isInSystemHeader(loc))
        return;
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
      if (pvd) {
        Dylinx::Instance().rw.ReplaceText(pvd->getTypeSourceInfo()->getTypeLoc().getSourceRange(), "generic_interface_t *");
      } else if (Dylinx::Instance().last_altered_loc != key) {
        char format[50];
        sprintf(format, "DYLINX_LOCK_MACRO_%d", Dylinx::Instance().lock_i);
        if (d->getStorageClass() == StorageClass::SC_Static) {
          SourceLocation loc = Lexer::findNextToken(
            d->getBeginLoc(),
            sm,
            result.Context->getLangOpts()
          )->getLocation();
          Dylinx::Instance().rw.ReplaceText(loc, 15, format);
        }
        else
          Dylinx::Instance().rw.ReplaceText(d->getBeginLoc(), 15, format);
        YAML::Node decl_loc;
        if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(d))
          decl_loc["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));
        decl_loc["file_name"] = sm.getFileEntryForID(src_id)->getName().str();
        decl_loc["line"] = sm.getSpellingLineNumber(d->getBeginLoc());
        decl_loc["id"] = Dylinx::Instance().lock_i;
        decl_loc["modification_type"] = VAR_ALLOCA_TYPE;
        Dylinx::Instance().lock_i++;
        Dylinx::Instance().lock_decl["LockEntity"].push_back(decl_loc);
        Dylinx::Instance().should_header_modify[src_id] = true;
        Dylinx::Instance().last_altered_loc = key;
      } else
        return;
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
        hasType(asString("pthread_mutex_t")),
        hasType(asString("pthread_mutex_t *")),
        hasType(arrayType(hasElementType(qualType(asString("pthread_mutex_t *")))))
      )).bind("vars"),
      &handler_for_vars
    );

    // Match all
    //    pthread_mutex_t locks[NUM_LOCK];
    // and convert them to
    //    pthread_mutex_t locks[NUM_LOCK]; __dylinx_array_init_(locks, NUM_LOCK, DYLINX_LOCK_MACRO);
    matcher.addMatcher(
      varDecl(hasType(arrayType(hasElementType(qualType(asString("pthread_mutex_t")))))).bind("array_decls"),
      &handler_for_array
    );

    // Match all
    //    typedef pthread_mutex_t MyLock
    //    typedef pthread_mutext_t *MyLockPtr
    // and convert them to
    //    typedef BaseLock MyLock
    //    typedef BaseLock *MyLock

    //! TODO Not implement yet
    matcher.addMatcher(
      typedefDecl(eachOf(
        hasType(asString("pthread_mutex_t")),
        hasType(asString("pthread_mutex_t *"))
      )).bind("typedefs"),
      &handler_for_typedef
    );

    // Match all
    //    pthread_mutex_t *lock malloc(sizeof(pthread_mutex_t));
    //    pthread_mutex_t *lock;
    //    lock = malloc(sizeof(pthread_mutex_t));
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
          )).bind("malloc_decl_callexpr")
        ))).bind("malloc_decl"),
      &handler_for_mem_alloca
    );

    matcher.addMatcher(
      binaryOperator(
        hasOperatorName("="),
        hasRHS(hasDescendant(
         callExpr(callee(
          functionDecl(hasName("malloc"))),
          hasArgument(0, sizeOfExpr(hasArgumentOfType(qualType(asString("pthread_mutex_t"))))
        )).bind("malloc_assign_callexpr")))
      ).bind("malloc_assign"),
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

    matcher.addMatcher(
      callExpr(
        hasAnyArgument(hasType(asString("pthread_mutex_t *")))
      ).bind("ptr_ref"),
      &handler_for_ref
    );

    matcher.addMatcher(
      fieldDecl(eachOf(
        hasType(asString("pthread_mutex_t")),
        hasType(asString("pthread_mutex_t *"))
      )).bind("struct_members"),
      &handler_for_struct
    );

    YAML::Emitter out;
    matcher.matchAST(Context);
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
  ArrayMatchHandler handler_for_array;
  TypedefMatchHandler handler_for_typedef;
  PtrRefMatchHandler handler_for_ref;
  StructFieldMatchHandler handler_for_struct;
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
