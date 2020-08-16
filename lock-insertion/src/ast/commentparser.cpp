#include "clang/Lex/Lexer.h"
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
#include <set>
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
#define MEM_ALLOCA_TYPE "MALLOC"

using namespace clang;
using namespace llvm;
using namespace clang::tooling;
using namespace clang::ast_matchers;
namespace fs = std::filesystem;

// Consider FileID and line number combination as an unique ID.
typedef std::pair<FileID, uint32_t> LocID;
class Dylinx {
public:
  static Dylinx& Instance() {
    static Dylinx dylinx;
    return dylinx;
  }
  Rewriter *rw_ptr;
  std::set<FileID> altered_files;
  std::ofstream yaml_fout;
  YAML::Node lock_decl;
  uint32_t lock_i = 0;
  std::map<
    std::string,
    std::vector<uint32_t>
  > lock_member_ids;
  std::set<
    std::tuple<std::string, std::string>
  > inserted_member;
  fs::path temp_dir;
  std::pair<std::string, uint32_t> extra_init4cu{"", 0};
private:
  Dylinx() {};
  ~Dylinx() {};
};

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

void write_modified_file(fs::path file_path, FileID fid, SourceManager& sm) {
  std::error_code err;
  raw_fd_ostream fstream((Dylinx::Instance().temp_dir / file_path.filename()).string(), err);
  Dylinx::Instance().rw_ptr->getEditBuffer(fid).write(fstream);
}

YAML::Node parse_comment(std::string raw_text) {
  // exterior match
  std::smatch config_result;
  std::regex re("\\[LockSlot\\](.*)");
  std::regex_match(raw_text, config_result, re);
  YAML::Node cmb;
  if (config_result.size() == 2) { // meet exterior condition
    std::smatch comb_result;
    std::string combination = config_result[1];
    std::regex lock_pattern(getLockPattern());
    while(std::regex_search(combination, comb_result, lock_pattern)) {
      std::string t = comb_result[1];
      combination = comb_result.suffix().str();
      cmb.push_back(t);
    }
  }
  return cmb;
}

class FuncInterfaceMatchHandler: public MatchFinder::MatchCallback {
public:
  FuncInterfaceMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("interfaces")) {
      SourceManager& sm = result.Context->getSourceManager();
      FileID src_id = sm.getFileID(e->getBeginLoc());
      Dylinx::Instance().rw_ptr->ReplaceText(
        e->getCallee()->getSourceRange(),
        interface_LUT[e->getDirectCallee()->getNameInfo().getName().getAsString()]
      );
      Dylinx::Instance().altered_files.emplace(src_id);
    }
  }
private:
  std::map<std::string, std::string>interface_LUT = {
    {"pthread_mutex_init", "__dylinx_check_var_"},
    {"pthread_mutex_lock", "__dylinx_generic_enable_"},
    {"pthread_mutex_unlock", "__dylinx_generic_disable_"},
    {"pthread_mutex_destroy", "__dylinx_generic_destroy_"},
    {"pthread_cond_wait", "__dylinx_generic_condwait_"}
  };
};

class MemberInitMatchHandler: public MatchFinder::MatchCallback {
public:
  MemberInitMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("member_init_call")) {
      SourceManager& sm = result.Context->getSourceManager();
      FileID src_id = sm.getFileID(e->getBeginLoc());
      Dylinx::Instance().rw_ptr->ReplaceText(
        e->getCallee()->getSourceRange(),
        "__dylinx_member_init_"
      );
      Dylinx::Instance().altered_files.emplace(src_id);
    }
  }
};

class MallocMatchHandler: public MatchFinder::MatchCallback {
public:
  MallocMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    const CallExpr *e;
    SourceManager& sm = result.Context->getSourceManager();
    std::string ptr_name;
    if (const VarDecl *vd = result.Nodes.getNodeAs<VarDecl>("malloc_decl")) {
      ptr_name = vd->getNameAsString();
      e = result.Nodes.getNodeAs<CallExpr>("malloc_decl_callexpr");
      SourceLocation loc = vd->getBeginLoc();
      FileID src_id = sm.getFileID(loc);
      uint32_t line = sm.getSpellingLineNumber(loc);
    } else if (const BinaryOperator *binop = result.Nodes.getNodeAs<BinaryOperator>("malloc_assign")) {
      DeclRefExpr *drefexpr = dyn_cast<DeclRefExpr>(binop->getLHS());
      ptr_name = drefexpr->getNameInfo().getAsString();
      e = result.Nodes.getNodeAs<CallExpr>("malloc_assign_callexpr");
    } else {
      perror("Runtime error malloc matcher operation fault!\n");
    }
    if(!e)
      return;
    char arr_init[100];
    SourceLocation size_begin = e->getArg(0)->getBeginLoc();
    SourceLocation size_end = e->getRParenLoc();
    SourceRange range(size_begin, size_end);
    sprintf(
      arr_init, " FILL_ARRAY(%s, %s);",
      ptr_name.c_str(),
      Lexer::getSourceText(CharSourceRange(range, false), sm, result.Context->getLangOpts()).str().c_str()
    );
    Dylinx::Instance().rw_ptr->InsertTextAfter(
      e->getEndLoc().getLocWithOffset(2),
      arr_init
    );
    Dylinx::Instance().altered_files.emplace(sm.getFileID(e->getBeginLoc()));
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
        Dylinx::Instance().rw_ptr->ReplaceText(loc, 15, type_macro);
        alloca["extra_init"] = 1;
      } else
        Dylinx::Instance().rw_ptr->ReplaceText(d->getBeginLoc(), 15, type_macro);

      if (d->getStorageClass() != StorageClass::SC_Static || d->isStaticLocal()) {
        char arr_init[100];
        sprintf(
          arr_init,
          " FILL_ARRAY( %s, (%s) * sizeof(pthread_mutex_t));",
          d->getNameAsString().c_str(),
          array_size
        );
        Dylinx::Instance().rw_ptr->InsertTextAfter(
          d->getEndLoc().getLocWithOffset(2),
          arr_init
        );
      }
      Dylinx::Instance().lock_i++;
      Dylinx::Instance().lock_decl["LockEntity"].push_back(alloca);
      Dylinx::Instance().altered_files.emplace(src_id);
    }
  }
};

class TypedefMatchHandler: public MatchFinder::MatchCallback {
public:
  TypedefMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const TypedefDecl *d = result.Nodes.getNodeAs<TypedefDecl>("typedefs")) {
      SourceManager& sm = result.Context->getSourceManager();
      const Token *token = move2n_token(d->getBeginLoc(), 1, sm, result.Context->getLangOpts());
      char format[100];
      sprintf(format, "DYLINX_LOCK_MACRO_%d", Dylinx::Instance().lock_i);
      Dylinx::Instance().rw_ptr->ReplaceText(
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
      Dylinx::Instance().lock_i++;
      Dylinx::Instance().lock_decl["LockEntity"].push_back(lock_meta);
      Dylinx::Instance().altered_files.emplace(src_id);
    }
  }
};

class RecordAliasMatchHandler: public MatchFinder::MatchCallback {
public:
  RecordAliasMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const TypedefDecl *td = result.Nodes.getNodeAs<TypedefDecl>("record_alias")) {
      SourceManager& sm = result.Context->getSourceManager();
      std::string recr_name = td->getUnderlyingType().getAsString();
      std::string alias = td->getNameAsString();
      Dylinx::Instance().lock_member_ids[alias] = Dylinx::Instance().lock_member_ids[recr_name];
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
      std::string recr_name = fd->getParent()->getNameAsString();
      recr_name = "struct " + recr_name;
      std::string field_name = fd->getNameAsString();
      if (Dylinx::Instance().inserted_member.find(
            std::make_tuple(recr_name, field_name)
          ) != Dylinx::Instance().inserted_member.end())
        return;
      char format[50];
      sprintf(format, "DYLINX_LOCK_MACRO_%d", Dylinx::Instance().lock_i);
      Dylinx::Instance().rw_ptr->ReplaceText(loc, 15, format);
      Dylinx::Instance().inserted_member.insert(
        std::make_tuple(recr_name, field_name)
      );
      YAML::Node decl_loc;
      if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(fd))
        decl_loc["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));
      decl_loc["file_name"] = sm.getFileEntryForID(src_id)->getName().str();
      decl_loc["line"] = sm.getSpellingLineNumber(fd->getBeginLoc());
      decl_loc["id"] = Dylinx::Instance().lock_i;
      decl_loc["modification_type"] = STRUCT_MEM_ALLOCA_TYPE;
      if (Dylinx::Instance().lock_member_ids.find(recr_name) == Dylinx::Instance().lock_member_ids.end()) {
        Dylinx::Instance().lock_member_ids[recr_name] = std::vector<uint32_t>();
        Dylinx::Instance().lock_member_ids[recr_name].push_back(
          Dylinx::Instance().lock_i
        );
      } else {
        Dylinx::Instance().lock_member_ids[recr_name].push_back(
          Dylinx::Instance().lock_i
        );
      }
      Dylinx::Instance().lock_i++;
      Dylinx::Instance().lock_decl["LockEntity"].push_back(decl_loc);
      Dylinx::Instance().altered_files.emplace(src_id);
    }
  }
};

class InitlistMatchHandler: public MatchFinder::MatchCallback {
public:
  InitlistMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const VarDecl *vd = result.Nodes.getNodeAs<VarDecl>("struct_instance")) {
      SourceManager& sm = result.Context->getSourceManager();
      std::string cur_loc = vd->getBeginLoc().printToString(sm);
      if (cur_loc.compare(processed_loc) != 0) {
        processed_loc = cur_loc;
        std::string recr_name = vd->getType().getAsString();
        cursor = 0;
        ids = Dylinx::Instance().lock_member_ids[recr_name];
      }
    }
    if (const InitListExpr *init_expr = result.Nodes.getNodeAs<InitListExpr>("struct_member_init")) {
      SourceManager& sm = result.Context->getSourceManager();
      SourceLocation begin_loc = init_expr->getLBraceLoc();
      char format[100];
      sprintf(format, "DYLINX_STRUCT_MEMBER_INIT_%d", ids[cursor]);
      Dylinx::Instance().rw_ptr->ReplaceText(
        sm.getImmediateExpansionRange(begin_loc).getAsRange(),
        format
      );
      cursor++;
    }
  }
private:
  uint32_t cursor;
  std::vector<uint32_t> ids;
  std::string processed_loc;
};

class EntryMatchHandler: public MatchFinder::MatchCallback {
public:
  EntryMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const FunctionDecl *fd = result.Nodes.getNodeAs<FunctionDecl>("entry")) {
      SourceManager& sm = result.Context->getSourceManager();
      CompoundStmt *main_body = dyn_cast<CompoundStmt>(fd->getBody());
      SourceLocation scope_start = main_body->getLBracLoc();
      Dylinx::Instance().rw_ptr->InsertText(
        scope_start.getLocWithOffset(1),
        "\n\t__dylinx_global_mtx_init();\n"
      );
      FileID src_id = sm.getFileID(scope_start);
      Dylinx::Instance().altered_files.emplace(src_id);
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
          Dylinx::Instance().rw_ptr->InsertTextBefore(begin, "__dylinx_generic_cast_(");
          Dylinx::Instance().rw_ptr->InsertTextAfter(end, ")");
        }
      }
      Dylinx::Instance().altered_files.emplace(sm.getFileID(e->getBeginLoc()));
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
    if (const VarDecl *d = result.Nodes.getNodeAs<VarDecl>("vars")) {
      SourceManager& sm = result.Context->getSourceManager();
      // Dealing with Type information
      SourceLocation begin_loc = d->getBeginLoc();
      SourceLocation init_loc = Lexer::findNextToken(
        d->getEndLoc(),
        sm,
        result.Context->getLangOpts()
      )->getLocation();
      if (sm.isInSystemHeader(begin_loc))
        return;
      FileID src_id = sm.getFileID(begin_loc);
      uint32_t line = sm.getSpellingLineNumber(begin_loc);
      LocID key = std::make_pair(src_id, line);
      Dylinx::Instance().altered_files.emplace(src_id);
      YAML::Node decl_loc;
      // Take care when user declares their mutex in the same line.
      // Ex:
      //    pthread_mutex_t mtx1, mtx2;
      if (processing_type_loc != key) {
        char format[50];
        sprintf(format, "DYLINX_LOCK_TYPE_%d", Dylinx::Instance().lock_i);
        if (d->getStorageClass() == StorageClass::SC_Static) {
          SourceLocation type_loc = move2n_token(begin_loc, 1, sm, result.Context->getLangOpts())->getLocation();
          Dylinx::Instance().rw_ptr->ReplaceText(type_loc, 15, format);
        }
        else
          Dylinx::Instance().rw_ptr->ReplaceText(begin_loc, 15, format);
        if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(d))
          decl_loc["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));
        decl_loc["file_name"] = sm.getFileEntryForID(src_id)->getName().str();
        decl_loc["line"] = sm.getSpellingLineNumber(d->getBeginLoc());
        decl_loc["id"] = Dylinx::Instance().lock_i;
        decl_loc["modification_type"] = VAR_ALLOCA_TYPE;
        lock_cnt = Dylinx::Instance().lock_i;
        processing_type_loc = key;
        Dylinx::Instance().lock_i++;
      }

      // Dealing with initialization
      printf(
        "%s hasInit=%d, isStaticLocal=%d, hasGlobalStorage=%d\n",
        d->getNameAsString().c_str(), d->hasInit(), d->isStaticLocal(), d->hasGlobalStorage()
      );
      if (d->hasInit() && !d->isStaticLocal() && d->hasGlobalStorage()) {
        uint32_t skip = d->getStorageClass() == StorageClass::SC_Static? 2: 1;
        const Token *var_name = move2n_token(d->getBeginLoc(), skip, sm, result.Context->getLangOpts());
        Dylinx::Instance().rw_ptr->RemoveText(
          SourceRange(
            var_name->getLocation().getLocWithOffset(d->getNameAsString().length()),
            sm.getImmediateExpansionRange(d->getEndLoc()).getAsRange().getEnd()
          )
        );
        if (sm.getFileEntryForID(sm.getMainFileID())->getName().str().compare(Dylinx::Instance().extra_init4cu.first)) {
          Dylinx::Instance().extra_init4cu.second++;
          Dylinx::Instance().extra_init4cu.first = sm.getFileEntryForID(sm.getMainFileID())->getName().str();
        }
        decl_loc["extra_init"] = Dylinx::Instance().extra_init4cu.second;
        decl_loc["name"] = d->getNameAsString();
        Dylinx::Instance().lock_decl["LockEntity"].push_back(decl_loc);
        return;
      } else if (const InitListExpr *init_expr = result.Nodes.getNodeAs<InitListExpr>("init_macro")) {
        SourceManager& sm = result.Context->getSourceManager();
        SourceLocation begin_loc = init_expr->getLBraceLoc();
        char format[100];
        sprintf(format, "DYLINX_LOCK_INIT_%d", Dylinx::Instance().lock_i);
        Dylinx::Instance().rw_ptr->ReplaceText(
            sm.getImmediateExpansionRange(begin_loc).getAsRange(),
            format
            );
        Dylinx::Instance().lock_decl["LockEntity"].push_back(decl_loc);
        return;
      }
      char format[50];
      sprintf(format, " = DYLINX_LOCK_INIT_%d", lock_cnt);
      Dylinx::Instance().rw_ptr->InsertText(init_loc, format);
      Dylinx::Instance().lock_decl["LockEntity"].push_back(decl_loc);
    }
  }
private:
  LocID processing_type_loc;
  uint32_t lock_cnt;
};

class SlotIdentificationConsumer : public clang::ASTConsumer {
public:
  Rewriter rw;
  const LangOptions& opts;
  explicit SlotIdentificationConsumer(ASTContext *Context, const LangOptions& opts): opts{opts} {}
  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    SourceManager& sm = Context.getSourceManager();
    Dylinx::Instance().altered_files.clear();
    Dylinx::Instance().rw_ptr = new Rewriter;
    Dylinx::Instance().rw_ptr->setSourceMgr(sm, opts);
    // Match all
    //    pthread_mutex_t mutex;
    //    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    // and convert to
    //    DYLINX_LOCK_TYPE_1 lock = DYLINX_LOCK_INIT_1;
    matcher.addMatcher(
      varDecl(
        hasType(asString("pthread_mutex_t")),
        optionally(has(
          initListExpr(hasSyntacticForm(hasType(asString("pthread_mutex_t")))).bind("init_macro")
        ))).bind("vars"),
      &handler_for_vars
    );
    // Match all
    //    pthread_mutex_t locks[NUM_LOCK];
    // and convert them to
    //    pthread_mutex_t locks[NUM_LOCK]; __dylinx_array_init_(locks, NUM_LOCK, DYLINX_LOCK_MACRO);
    // matcher.addMatcher(
    //   varDecl(hasType(arrayType(hasElementType(qualType(asString("pthread_mutex_t")))))).bind("array_decls"),
    //   &handler_for_array
    // );

    // Match all
    //    typedef pthread_mutex_t MyLock
    //    typedef pthread_mutext_t *MyLockPtr
    // and convert them to
    //    typedef BaseLock MyLock
    //    typedef BaseLock *MyLock
    matcher.addMatcher(
      typedefDecl(eachOf(
        hasType(asString("pthread_mutex_t")),
        hasType(asString("pthread_mutex_t *"))
      )).bind("typedefs"),
      &handler_for_typedef
    );

    // Match all
    //    pthread_mutex_t *lock = malloc(sizeof(pthread_mutex_t));
    //    pthread_mutex_t *lock;
    //    lock = malloc(sizeof(pthread_mutex_t));
    // and convert them to
    //    __dylinx_ptr_init(malloc(sizeof(pthread_mutex_t)));
    matcher.addMatcher(
      varDecl(
        hasType(asString("pthread_mutex_t *")),
        hasInitializer(hasDescendant(
          callExpr(
            callee(functionDecl(hasName("malloc"))),
            hasArgument(0, sizeOfExpr(hasArgumentOfType(qualType(asString("pthread_mutex_t"))))
          )).bind("malloc_decl_callexpr")
        ))).bind("malloc_decl"),
      &handler_for_malloc
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
      &handler_for_malloc
    );

    matcher.addMatcher(
      fieldDecl(eachOf(
        hasType(asString("pthread_mutex_t")),
        hasType(asString("pthread_mutex_t *"))
      )).bind("struct_members"),
      &handler_for_struct
    );

    matcher.addMatcher(
      varDecl(
        anyOf(
          hasType(recordDecl(has(fieldDecl(hasType(asString("pthread_mutex_t")))))),
          hasType(typedefDecl(hasType(qualType(hasDeclaration(
            recordDecl(has(fieldDecl(hasType(asString("pthread_mutex_t")))))
          )))))
        ),
        forEachDescendant(
          initListExpr(hasSyntacticForm(
            hasType(asString("pthread_mutex_t"))
          )).bind("struct_member_init")
        ))
      .bind("struct_instance"),
      &handler_for_initlist
    );

    matcher.addMatcher(
      typedefDecl(hasType(qualType(hasDeclaration(
        recordDecl(has(fieldDecl(hasType(asString("pthread_mutex_t")))))
        )))).bind("record_alias"),
      &handler_for_record_alias
    );

    matcher.addMatcher(
      callExpr(eachOf(
        callee(functionDecl(hasName("pthread_mutex_lock"))),
        callee(functionDecl(hasName("pthread_mutex_unlock"))),
        allOf(
          callee(functionDecl(hasName("pthread_mutex_init"))),
          hasArgument(0, hasDescendant(declRefExpr(
            hasType(qualType(
              anyOf(
                asString("pthread_mutex_t"),
                hasDeclaration(typedefDecl(hasType(asString("pthread_mutex_t"))))
              )
            ))
          )))
        ),
        callee(functionDecl(hasName("pthread_mutex_destroy"))),
        callee(functionDecl(hasName("pthread_cond_wait")))
      )).bind("interfaces"),
      &handler_for_interface
    );

    matcher.addMatcher(
      callExpr(
        callee(functionDecl(hasName("pthread_mutex_init"))),
        hasArgument(0, hasDescendant(declRefExpr(
          hasType(qualType(
            hasDeclaration(anyOf(
              recordDecl(has(fieldDecl(hasType(asString("pthread_mutex_t"))))),
              typedefDecl(hasType(qualType(hasDeclaration(
                recordDecl(has(fieldDecl(hasType(asString("pthread_mutex_t")))))
              ))))
            ))
          ))
        )))
      ).bind("member_init_call"),
      &handler_for_member_init
    );

    matcher.addMatcher(
      functionDecl(hasName("main")).bind("entry"),
      &handler_for_entry
    );

    matcher.addMatcher(
      callExpr(
        hasAnyArgument(hasType(asString("pthread_mutex_t *")))
      ).bind("ptr_ref"),
      &handler_for_ref
    );

    matcher.matchAST(Context);
    std::set<FileID>::iterator file;
    for (
      file = Dylinx::Instance().altered_files.begin();
      file != Dylinx::Instance().altered_files.end();
      file++
    )
      Dylinx::Instance().lock_decl["AlteredFiles"].push_back(
        sm.getFileEntryForID(*file)->getName().str()
      );
  }
private:
  MatchFinder matcher;
  FuncInterfaceMatchHandler handler_for_interface;
  VarsMatchHandler handler_for_vars;
  MallocMatchHandler handler_for_malloc;
  ArrayMatchHandler handler_for_array;
  TypedefMatchHandler handler_for_typedef;
  PtrRefMatchHandler handler_for_ref;
  StructFieldMatchHandler handler_for_struct;
  InitlistMatchHandler handler_for_initlist;
  RecordAliasMatchHandler handler_for_record_alias;
  MemberInitMatchHandler handler_for_member_init;
  EntryMatchHandler handler_for_entry;
};

class SlotIdentificationAction : public clang::ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile
  ) {
    // Dylinx::Instance().rw.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
    std::unique_ptr<SlotIdentificationConsumer> consumer(
      new SlotIdentificationConsumer(
        &Compiler.getASTContext(),
        Compiler.getLangOpts()
      )
    );
    return consumer;
  }
  void setCompileDB(std::shared_ptr<CompilationDatabase> compiler_db) {
    this->compiler_db = compiler_db;
  }

  void EndSourceFileAction() override {
    // Manually add the definition of BaseLock, slot_lock and
    // slot_unlock here.
    SourceManager &sm = Dylinx::Instance().rw_ptr->getSourceMgr();
    if (!sm.getFileEntryForID(sm.getMainFileID())->getName().str().compare(Dylinx::Instance().extra_init4cu.first)) {
      SourceLocation end = sm.getLocForEndOfFile(sm.getMainFileID());
      char prototype[100];
      sprintf(
        prototype,
        "void __dylinx_cu_init_%d_() {\n",
        Dylinx::Instance().extra_init4cu.second
      );
      std::string global_initializer(prototype);
      YAML::Node entity = Dylinx::Instance().lock_decl["LockEntity"];
      for (YAML::const_iterator it = entity.begin(); it != entity.end(); it++) {
        const YAML::Node& entity = *it;
        if (entity["extra_init"] && entity["extra_init"].as<uint32_t>() == Dylinx::Instance().extra_init4cu.second) {
          char init_mtx[50];
          sprintf(init_mtx, "\t__dylinx_member_init_(&%s, NULL);\n", entity["name"].as<std::string>().c_str());
          printf("%s %u\n", init_mtx, Dylinx::Instance().extra_init4cu.second);
          global_initializer.append(init_mtx);
        }
      }
      global_initializer.append("}");
      Dylinx::Instance().rw_ptr->InsertText(
        end,
        global_initializer
      );
      Dylinx::Instance().altered_files.emplace(sm.getMainFileID());
    }
    std::set<FileID>::iterator iter;
    for (iter = Dylinx::Instance().altered_files.begin(); iter != Dylinx::Instance().altered_files.end(); iter++) {
      std::string filename = sm.getFileEntryForID(*iter)->getName().str();
      Dylinx::Instance().rw_ptr->InsertText(sm.getLocForStartOfFile(*iter), "#include \"dylinx-glue.h\"\n");
      write_modified_file(filename, *iter, sm);
    }
    delete Dylinx::Instance().rw_ptr;
    return;
  }
private:
  std::shared_ptr<CompilationDatabase> compiler_db;
};


std::unique_ptr<FrontendActionFactory> newSlotIdentificationActionFactory(
    std::shared_ptr<CompilationDatabase> compiler_db
    ) {
  class SlotIdentificationActionFactory: public FrontendActionFactory {
  public:
    std::unique_ptr<FrontendAction> create() override {
      std::unique_ptr<SlotIdentificationAction> action(new SlotIdentificationAction);
      action->setCompileDB(this->compiler_db);
      return action;
    };
    void setCompileDB(std::shared_ptr<CompilationDatabase> compiler_db) {
      this->compiler_db = compiler_db;
    }
  private:
    std::shared_ptr<CompilationDatabase> compiler_db;
  };
  std::unique_ptr<SlotIdentificationActionFactory> factory(new SlotIdentificationActionFactory);
  factory->setCompileDB(compiler_db);
  return factory;
}

int main(int argc, const char **argv) {
  std::string err;
  const char *compiler_db_path = argv[1];
  fs::path revert = fs::path(std::string(argv[1])).parent_path() / ".dylinx";
  Dylinx::Instance().yaml_fout = std::ofstream(argv[2]);
  const char *glue_env = std::getenv("DYLINX_GLUE_PATH");
  if (!glue_env) {
    fprintf(stderr, "[ERROR] It is required to set DYLINX_GLUE_PATH\n");
    return -1;
  }
  Dylinx::Instance().temp_dir = fs::temp_directory_path() / ".dylinx-modified";
  // Clean temporary directory
  if (!fs::exists(Dylinx::Instance().temp_dir))
    fs::create_directory(Dylinx::Instance().temp_dir);
  else {
    fs::remove_all(Dylinx::Instance().temp_dir);
    fs::create_directory(Dylinx::Instance().temp_dir);
  }

  // Store original file for revert in the future
  if (!fs::exists(revert))
    fs::create_directory(revert);
  else {
    fs::remove_all(revert);
    fs::create_directory(revert);
  }

  std::shared_ptr<CompilationDatabase> compiler_db = CompilationDatabase::autoDetectFromSource(compiler_db_path, err);
  ClangTool tool(*compiler_db, compiler_db->getAllFiles());
  tool.run(newSlotIdentificationActionFactory(compiler_db).get());
  YAML::Node updated_file = Dylinx::Instance().lock_decl["AlteredFiles"];
  for (YAML::const_iterator it = updated_file.begin(); it != updated_file.end(); it++) {
    fs::path u =  (*it).as<std::string>();
    fs::copy(u, revert / u.filename());
    fs::remove(u);
    fs::copy(Dylinx::Instance().temp_dir / u.filename(), u);
  }
  YAML::Emitter out;
  out << Dylinx::Instance().lock_decl;
  Dylinx::Instance().yaml_fout << out.c_str();
  Dylinx::Instance().yaml_fout.close();
}
