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
#include <cctype>
#include <utility>
#include "yaml-cpp/yaml.h"
#include "util.h"

#define ARR_ALLOCA_TYPE "ARRAY"
#define VAR_ALLOCA_TYPE "VARIABLE"
#define EXTERN_VAR_SYMBOL "EXTERN_VAR_SYMBOL"
#define EXTERN_ARR_SYMBOL "EXTERN_ARR_SYMBOL"
#define VAR_FIELD_INIT "VAR_FIELD_INIT"
#define FIELD_INSERT "FIELD_INSERT"
#define TYPEDEF_ALLOCA_TYPE "TYPEDEF"
#define MUTEX_MEM_ALLOCATION "MUTEX_MEM_ALLOCATION"
#define STRUCT_MEM_ALLOCATION "STRUCT_MEM_ALLOCATION"
#define DEBUG_LOG(pattern, ins, sm)  \
  do {                      \
    SourceLocation matched_loc = ins->getBeginLoc();  \
    FileID src_id = sm.getFileID(matched_loc); \
    printf( \
      "[ LOG FIND ] %20s L%5d:%4d, %s\n", \
      #pattern, sm.getSpellingLineNumber(matched_loc),\
      sm.getSpellingColumnNumber(matched_loc), \
      sm.getFileEntryForID(src_id)->getName().str().c_str() \
    ); \
  } while(0)

using namespace clang;
using namespace llvm;
using namespace clang::tooling;
using namespace clang::ast_matchers;
namespace fs = std::filesystem;

// Use tuple with four elements as unique ID
// 1. file path
// 2. line number
// 3. column number
typedef std::tuple<fs::path, uint32_t, uint32_t> EntityID;
class Dylinx {
public:
  static Dylinx& Instance() {
    static Dylinx dylinx;
    return dylinx;
  }
  Rewriter *rw_ptr;
  std::set<EntityID> metas;
  std::set<FileID> cu_deps;
  std::map<std::string, std::string> cu_arrs;
  std::map<std::string, std::vector<std::string>> cu_recr;
  std::set<fs::path> altered_files;
  std::ofstream yaml_fout;
  YAML::Node lock_decl;
  uint32_t lock_i = 0;
  fs::path temp_dir;
  bool require_init;
private:
  Dylinx() {};
  ~Dylinx() {};
};

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

void traverse_init_fields(const RecordDecl *recr, std::vector<std::string>& init_str, std::vector<std::string>& field_seq) {
  for (auto iter = recr->field_begin(); iter != recr->field_end(); iter++) {
    const clang::Type *t = iter->getType().getTypePtr();
    std::string type_name = t->getCanonicalTypeInternal().getAsString();
    if (t->isStructureType() && type_name != "pthread_mutex_t") {
      field_seq.push_back(iter->getNameAsString());
      traverse_init_fields(t->getAsStructureType()->getDecl(), init_str, field_seq);
    }
    else if (type_name == "pthread_mutex_t") {
      std::string prefix = "";
      for (auto el = field_seq.begin(); el != field_seq.end(); el++)
        prefix = prefix + std::string(".") + *el;
      init_str.push_back(prefix + std::string(".") + iter->getNameAsString());
    } else {
      continue;
    }
  }
  if (field_seq.size())
    field_seq.pop_back();
}

void write_modified_file(fs::path file_path, FileID fid, SourceManager& sm) {
  std::error_code err;
  fs::path temp_file = Dylinx::Instance().temp_dir / file_path.filename();
  if (!fs::exists(temp_file)) {
    raw_fd_ostream fstream(temp_file.string(), err);
    Dylinx::Instance().rw_ptr->getEditBuffer(fid).write(fstream);
  }
}

// Chances are saving is failed since the uid may already exist.
bool save2metas(EntityID uid, YAML::Node meta) {
  if (!Dylinx::Instance().metas.insert(uid).second)
    return false;
  meta["file_name"] = std::get<0>(uid).string();
  meta["line"] = std::get<1>(uid);
  meta["id"] = Dylinx::Instance().lock_i;
  Dylinx::Instance().lock_i++;
  Dylinx::Instance().lock_decl["LockEntity"].push_back(meta);
  return true;
}

bool save2altered_list(FileID src_id, SourceManager& sm) {
  return Dylinx::Instance().cu_deps.insert(src_id).second;
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


class MemberInitMatchHandler: public MatchFinder::MatchCallback {
public:
  MemberInitMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CallExpr *e = result.Nodes.getNodeAs<CallExpr>("member_init_call")) {
      SourceManager& sm = result.Context->getSourceManager();
#ifdef __DYLINX_DEBUG__
      DEBUG_LOG(MemberInit, e, sm);
#endif
      FileID src_id = sm.getFileID(e->getBeginLoc());
      Dylinx::Instance().rw_ptr->ReplaceText(
        e->getCallee()->getSourceRange(),
        "__dylinx_member_init_"
      );
      save2altered_list(src_id, sm);
    }
  }
};

// TODO type assignment
class MallocMatchHandler: public MatchFinder::MatchCallback {
public:
  MallocMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    SourceManager& sm = result.Context->getSourceManager();
    const VarDecl *vd = result.Nodes.getNodeAs<VarDecl>("res_decl");
    const BinaryOperator *bin_op = result.Nodes.getNodeAs<BinaryOperator>("res_assign");
    QualType arg_type;
    std::string ref_name;
    SourceLocation begin_loc;
    YAML::Node meta;
    if (vd) {
      arg_type = vd->getType().getTypePtr()->getPointeeType();
      ref_name = vd->getNameAsString();
      begin_loc = vd->getBeginLoc();
    } else if (bin_op) {
      const Expr *lhs = bin_op->getLHS();
      begin_loc = lhs->getBeginLoc();
      arg_type = lhs->getType().getTypePtr()->getPointeeType();
      ref_name = Lexer::getSourceText(
        CharSourceRange(SourceRange(
          lhs->getBeginLoc(),
          bin_op->getOperatorLoc()
        ), false),
        sm,
        result.Context->getLangOpts()
      ).str();
    }
    meta["name"] = ref_name;
    meta["pointee_type"] = arg_type.getAsString();
    FileID src_id = sm.getFileID(sm.getSpellingLoc(begin_loc));
    if (sm.isInSystemHeader(begin_loc) || src_id.isInvalid())
      return;
    fs::path src_path = sm.getFileEntryForID(src_id)->getName().str();
    EntityID uid = std::make_tuple(
      src_path,
      sm.getSpellingLineNumber(begin_loc),
      sm.getSpellingColumnNumber(begin_loc)
    );

    // Decouple type replacement
    if (vd && vd->getType().getAsString() == "pthread_mutex_t *") {
      SourceLocation type_start = vd->getTypeSpecStartLoc();
      SourceLocation type_end = vd->getTypeSpecEndLoc();
      if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(vd))
        meta["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));
      if (sm.isAtStartOfImmediateMacroExpansion(type_start)) {
        Dylinx::Instance().rw_ptr->ReplaceText(
          sm.getImmediateExpansionRange(type_start).getAsRange(),
          "generic_interface_t *"
        );
      } else {
        Dylinx::Instance().rw_ptr->ReplaceText(
          SourceRange(type_start, type_end),
          "generic_interface_t *"
        );
      }
    }

    if (const UnaryExprOrTypeTraitExpr *sizeof_expr = result.Nodes.getNodeAs<UnaryExprOrTypeTraitExpr>("sizeofExpr")) {
      std::vector<std::string> init_str;
      std::vector<std::string> field_seq;
      if (arg_type.getAsString() == "pthread_mutex_t") {
        Dylinx::Instance().rw_ptr->ReplaceText(
          SourceRange(
            sizeof_expr->getRParenLoc().getLocWithOffset(
              -1 * arg_type.getAsString().length()
            ),
            sizeof_expr->getRParenLoc().getLocWithOffset(-1)
          ),
          "generic_interface_t"
        );
        meta["modification_type"] = MUTEX_MEM_ALLOCATION;
      } else {
        traverse_init_fields(
          arg_type->getUnqualifiedDesugaredType()->getAsRecordDecl(),
          init_str, field_seq
        );
        if (!init_str.size())
          return;
        meta["modification_type"] = STRUCT_MEM_ALLOCATION;
      }
      char size_expr[300];
      const CallExpr *call_expr = result.Nodes.getNodeAs<CallExpr>("alloc_call");
      if (call_expr->getDirectCallee()->getNameInfo().getAsString() == std::string("malloc")) {
        const Expr *arg_expr = call_expr->getArg(0);
        SourceLocation arg_begin = arg_expr->getBeginLoc();
        SourceLocation arg_end = arg_expr->getEndLoc().getLocWithOffset(1);
        SourceRange range(arg_begin, arg_end);
        sprintf(
          size_expr, "(%s) / sizeof(%s)",
          Lexer::getSourceText(CharSourceRange(range, false), sm, result.Context->getLangOpts()).str().c_str(),
          arg_type.getAsString().c_str()
        );
      } else if (call_expr->getDirectCallee()->getNameInfo().getAsString() == std::string("calloc")) {
        const Expr *arg0_expr = call_expr->getArg(0);
        const Expr *arg1_expr = call_expr->getArg(1);
        size_t arg0_len = Lexer::getSourceText(
          CharSourceRange(
            SourceRange(arg0_expr->getBeginLoc(), arg1_expr->getBeginLoc()),
            false
          ),
          sm, result.Context->getLangOpts()
        ).str().find_last_of(",");
        SourceRange cnt_range(
          arg0_expr->getBeginLoc(),
          arg0_expr->getBeginLoc().getLocWithOffset(arg0_len)
        );
        SourceRange unit_range(
          arg1_expr->getBeginLoc(),
          arg1_expr->getEndLoc().getLocWithOffset(1)
        );
        sprintf(
          size_expr, "((%s) * (%s)) / sizeof(%s)",
          Lexer::getSourceText(CharSourceRange(cnt_range, false), sm, result.Context->getLangOpts()).str().c_str(),
          Lexer::getSourceText(CharSourceRange(unit_range, false), sm, result.Context->getLangOpts()).str().c_str(),
          arg_type.getAsString().c_str()
        );
      }
      char init_expr[300];
      if (arg_type.getAsString() == "pthread_mutex_t") {
        sprintf(
          init_expr,
          "\n__dylinx_ptr_unit_init_((%s), %s, DYLINX_LOCK_TYPE_%d);",
          ref_name.c_str(),
          size_expr,
          Dylinx::Instance().lock_i
        );
        Dylinx::Instance().rw_ptr->InsertText(
          call_expr->getEndLoc().getLocWithOffset(2),
          init_expr
        );
      } else {
        char loop_head[300];
        sprintf(
          loop_head,
          "\nfor (int i = 0; i < %s; i++) {",
          size_expr
        );
        Dylinx::Instance().rw_ptr->InsertText(
          call_expr->getEndLoc().getLocWithOffset(2),
          loop_head
        );
        for (auto it = init_str.begin(); it != init_str.end(); it++) {
          sprintf(
            init_expr,
            "\n\t__dylinx_member_init_(&(%s)[i]%s, NULL);",
            ref_name.c_str(),
            it->c_str()
          );
          Dylinx::Instance().rw_ptr->InsertText(
            call_expr->getEndLoc().getLocWithOffset(2),
            init_expr
          );
        }
        Dylinx::Instance().rw_ptr->InsertText(
          call_expr->getEndLoc().getLocWithOffset(2),
          "\n}"
        );
      }
      save2metas(uid, meta);
      save2altered_list(src_id, sm);
    }
  }
};

//! TODO
// buggy for global array handling.
// 1. Assume there is no consecutive declaration as following.
//    pthread_mutex_t m[2], n[5];
//    However, according to current code structure it would be
//    not too hard to ipmlement.
class ArrayMatchHandler: public MatchFinder::MatchCallback {
public:
  ArrayMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const VarDecl *d = result.Nodes.getNodeAs<VarDecl>("array_decls")) {
      SourceManager& sm = result.Context->getSourceManager();
#ifdef __DYLINX_DEBUG__
      DEBUG_LOG(ArrayDecl, d, sm);
#endif
      if (sm.isInSystemHeader(d->getBeginLoc()))
        return;
      TypeSourceInfo *type_info = d->getTypeSourceInfo();
      TypeLoc type_loc = type_info->getTypeLoc();
      FileID src_id = sm.getFileID(type_loc.getBeginLoc());
      fs::path src_path = sm.getFileEntryForID(src_id)->getName().str();
      if (Dylinx::Instance().altered_files.find(src_path) != Dylinx::Instance().altered_files.end())
        return;

      EntityID uid = std::make_tuple(
        src_path,
        sm.getSpellingLineNumber(type_loc.getBeginLoc()),
        sm.getSpellingColumnNumber(type_loc.getBeginLoc())
      );
      YAML::Node alloca;
      if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(d))
        alloca["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));

      // Dealing with array size
      SourceLocation size_begin, size_end;
      char array_size[50];
      SourceRange size_range;
      if (const ConstantArrayTypeLoc arr_type = type_loc.getAs<ConstantArrayTypeLoc>())
        size_range = arr_type.getSizeExpr()->getSourceRange();
      else if (const VariableArrayTypeLoc arr_type = type_loc.getAs<VariableArrayTypeLoc>())
        size_range = arr_type.getSizeExpr()->getSourceRange();
      else {
        exit(-1);
        perror("Array type is neither constant nor variable\n");
      }

      // Replace type
      char type_macro[50];
      sprintf(type_macro, "DYLINX_LOCK_TYPE_%d", Dylinx::Instance().lock_i);
      SourceLocation type_start = d->getTypeSpecStartLoc();
      Dylinx::Instance().rw_ptr->ReplaceText(type_start, 15, type_macro);
      alloca["name"] = d->getNameAsString();

      if (d->getStorageClass() == StorageClass::SC_Extern) {
        alloca["modification_type"] = EXTERN_ARR_SYMBOL;
        save2metas(uid, alloca);
        save2altered_list(src_id, sm);
        return;
      }

      alloca["modification_type"] = ARR_ALLOCA_TYPE;
      Dylinx::Instance().require_init = true;

      CharSourceRange size_char_rng = Lexer::getAsCharRange(size_range, sm, result.Context->getLangOpts());
      size_char_rng.setEnd(size_char_rng.getEnd());
      std::string size_src = Lexer::getSourceText(size_char_rng, sm, result.Context->getLangOpts()).str();

      if (!d->isStaticLocal() && d->hasGlobalStorage()) {
        alloca["extra_init"] = sm.getFileEntryForID(sm.getMainFileID())->getUID();
        Dylinx::Instance().cu_arrs[d->getNameAsString()] = size_src;
      } else {
        char init_mtx[200];
        sprintf(
          init_mtx,
          "\t__dylinx_array_init_(&%s, %s);\n",
          d->getNameAsString().c_str(),
          size_src.c_str()
        );
        Dylinx::Instance().rw_ptr->InsertTextAfter(
          d->getEndLoc().getLocWithOffset(2),
          init_mtx
        );
      }
      save2metas(uid, alloca);
      save2altered_list(src_id, sm);
    }
  }
};

class TypedefMatchHandler: public MatchFinder::MatchCallback {
public:
  TypedefMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const TypedefDecl *d = result.Nodes.getNodeAs<TypedefDecl>("typedefs")) {
      SourceManager& sm = result.Context->getSourceManager();
#ifdef __DYLINX_DEBUG__
      DEBUG_LOG(TypedeDecl, d, sm);
#endif
      SourceLocation begin_loc = d->getBeginLoc();
      const Token *token = move2n_token(begin_loc, 1, sm, result.Context->getLangOpts());
      FileID src_id = sm.getFileID(begin_loc);
      char format[100];
      Dylinx::Instance().rw_ptr->ReplaceText(
        token->getLocation(),
        token->getLength(),
        "generic_interface_t *"
      );
      save2altered_list(src_id, sm);
    }
  }
};

class RecordAliasMatchHandler: public MatchFinder::MatchCallback {
public:
  RecordAliasMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const TypedefDecl *td = result.Nodes.getNodeAs<TypedefDecl>("record_alias")) {
      SourceManager& sm = result.Context->getSourceManager();
#ifdef __DYLINX_DEBUG__
      DEBUG_LOG(RecordAlias, td, sm);
#endif
      std::string recr_name = td->getUnderlyingType().getAsString();
      std::string alias = td->getNameAsString();
      // Dylinx::Instance().lock_member_ids[alias] = Dylinx::Instance().lock_member_ids[recr_name];
    }
  }
};

class StructFieldMatchHandler: public MatchFinder::MatchCallback {
public:
  StructFieldMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const FieldDecl *fd = result.Nodes.getNodeAs<FieldDecl>("struct_members")) {
      SourceManager& sm = result.Context->getSourceManager();
#ifdef __DYLINX_DEBUG__
      DEBUG_LOG(StructField, fd, sm);
#endif
      if (sm.isInSystemHeader(fd->getBeginLoc()))
        return;
      SourceLocation begin_loc = fd->getBeginLoc();
      FileID src_id = sm.getFileID(begin_loc);
      fs::path src_path = sm.getFileEntryForID( sm.getFileID(begin_loc))->getName().str();
      EntityID uid = std::make_tuple(
        src_path,
        sm.getSpellingLineNumber(begin_loc),
        sm.getSpellingColumnNumber(begin_loc)
      );
      std::string recr_name = fd->getParent()->getNameAsString();
      YAML::Node decl_loc;
      if (recr_name.length() == 0)
        decl_loc["record_name"] = "0_anonymous_0";
      else
        decl_loc["record_name"] = recr_name;
      std::string field_name = fd->getNameAsString();
      decl_loc["field_name"] = field_name;
      char format[50];
      sprintf(format, "DYLINX_LOCK_TYPE_%d", Dylinx::Instance().lock_i);
      Dylinx::Instance().rw_ptr->ReplaceText(
        SourceRange(fd->getTypeSpecStartLoc(), fd->getTypeSpecEndLoc()),
        format
      );
      if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(fd))
        decl_loc["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));
      decl_loc["modification_type"] = FIELD_INSERT;
      save2metas(uid, decl_loc);
      save2altered_list(src_id, sm);
    }
  }
};


//! TODO
//  Refine struct variable to init immediately.
class InitlistMatchHandler: public MatchFinder::MatchCallback {
public:
  InitlistMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const VarDecl *vd = result.Nodes.getNodeAs<VarDecl>("struct_instance")) {
      SourceManager& sm = result.Context->getSourceManager();
      recr_name = vd->getType().getTypePtr()->getUnqualifiedDesugaredType()->getCanonicalTypeInternal().getAsString();
      if (recr_name == "pthread_mutex_t")
        return;
      if (sm.isInSystemHeader(vd->getBeginLoc()))
        return;
      SourceLocation begin_loc = vd->getBeginLoc();
      FileID src_id = sm.getFileID(begin_loc);
      fs::path src_path = sm.getFileEntryForID(src_id)->getName().str();
      EntityID uid = std::make_tuple(
        src_path,
        sm.getSpellingLineNumber(begin_loc),
        sm.getSpellingColumnNumber(begin_loc)
      );
      std::vector<std::string> init_str;
      std::vector<std::string> field_seq;
      traverse_init_fields(vd->getType().getTypePtr()->getAsRecordDecl(), init_str, field_seq);
      if (!init_str.size())
        return;
#ifdef __DYLINX_DEBUG__
      DEBUG_LOG(StructDecl, vd, sm);
#endif
      YAML::Node meta;
      meta["name"] = vd->getNameAsString();
      meta["modification_type"] = VAR_FIELD_INIT;

      if (!vd->isStaticLocal() && vd->hasGlobalStorage()) {
        Dylinx::Instance().require_init = true;
        meta["extra_init"] = sm.getFileEntryForID(sm.getMainFileID())->getUID();
        Dylinx::Instance().cu_recr[vd->getNameAsString()] = init_str;
      } else {
        std::string var_name = vd->getNameAsString();
        std::string concat_init = "";
        for (auto it = init_str.begin(); it != init_str.end(); it++) {
          char init_field[200];
          std::string field_name = var_name + *it;
          sprintf(
            init_field,
            "\t__dylinx_member_init_(&%s, NULL);\n",
            field_name.c_str()
          );
          concat_init = concat_init + init_field;
        }
        Dylinx::Instance().rw_ptr->InsertTextAfter(
          vd->getEndLoc().getLocWithOffset(var_name.size() + 2),
          concat_init
        );
      }
      save2metas(uid, meta);
      save2altered_list(src_id, sm);
    }
    return;
    if (const InitListExpr *init_expr = result.Nodes.getNodeAs<InitListExpr>("struct_member_init")) {
      SourceManager& sm = result.Context->getSourceManager();
      SourceLocation begin_loc = init_expr->getLBraceLoc();
      char format[100];
      sprintf(format, "DYLINXR_DUMMY_INIT");
      Dylinx::Instance().rw_ptr->ReplaceText(
        sm.getImmediateExpansionRange(begin_loc).getAsRange(),
        format
      );
    }
  }
private:
  uint32_t counter;
  std::string recr_name;
};

class EntryMatchHandler: public MatchFinder::MatchCallback {
public:
  EntryMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const FunctionDecl *fd = result.Nodes.getNodeAs<FunctionDecl>("entry")) {
      SourceManager& sm = result.Context->getSourceManager();
#ifdef __DYLINX_DEBUG__
      DEBUG_LOG(EntryMatch, fd, sm);
#endif
      CompoundStmt *main_body = dyn_cast<CompoundStmt>(fd->getBody());
      SourceLocation scope_start = main_body->getLBracLoc();
      Dylinx::Instance().rw_ptr->InsertText(
        scope_start.getLocWithOffset(1),
        "\n\t__dylinx_global_mtx_init_();\n"
      );
      FileID src_id = sm.getFileID(scope_start);
      save2altered_list(src_id, sm);
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
#ifdef __DYLINX_DEBUG__
      DEBUG_LOG(PtrRef, e, sm);
#endif
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
      save2altered_list(sm.getFileID(e->getBeginLoc()), sm);
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
// #ifdef __DYLINX_DEBUG__
//       DEBUG_LOG(VarDecl, d, sm);
// #endif
      if (sm.isInSystemHeader(d->getBeginLoc()))
        return;

      // Dealing with Type information
      SourceLocation type_loc = d->getTypeSpecStartLoc();
      FileID src_id = sm.getFileID(sm.getSpellingLoc(type_loc));
      fs::path src_path = sm.getFileEntryForID(src_id)->getName().str();

      // If the file is already modified just simply skip
      if (Dylinx::Instance().altered_files.find(src_path) != Dylinx::Instance().altered_files.end())
        return;

      EntityID cur_type = std::make_tuple(
        src_path,
        sm.getSpellingLineNumber(type_loc),
        sm.getSpellingColumnNumber(type_loc)
      );
      YAML::Node meta;

      // The behavior is as following.
      // if cur_type != pre_type
      //    increment lock_i and insert meta
      // else
      //    insert meta only.
      if (cur_type != pre_type) {
        // Encoutering a new modifiable lock type.
        char format[50];
        sprintf(format, "DYLINX_LOCK_TYPE_%d", Dylinx::Instance().lock_i);
        if (sm.isAtStartOfImmediateMacroExpansion(type_loc)) {
          Dylinx::Instance().rw_ptr->ReplaceText(
            sm.getImmediateExpansionRange(d->getTypeSpecStartLoc()).getAsRange(),
            format
          );
        } else {
          Dylinx::Instance().rw_ptr->ReplaceText(
            SourceRange(
              d->getTypeSpecStartLoc(),
              d->getTypeSpecEndLoc()
            ),
            format
          );
        }
        stash_id = Dylinx::Instance().lock_i;
      }
      std::string var_name = d->getNameAsString();
      if (!d->hasDefinition()) {
        meta["modification_type"] = EXTERN_VAR_SYMBOL;
        meta["name"] = var_name;
        save2metas(cur_type, meta);
        save2altered_list(src_id, sm);
        return;
      }
      Dylinx::Instance().require_init = true;
      if (RawComment *comment = result.Context->getRawCommentForDeclNoCache(d))
        meta["lock_combination"] = parse_comment(comment->getBriefText(*result.Context));
      meta["modification_type"] = VAR_ALLOCA_TYPE;

      // Dealing with initialization
      if (!d->isStaticLocal() && d->hasGlobalStorage()) {
        // Global variable requires extra init.
        const InitListExpr *init_expr = result.Nodes.getNodeAs<InitListExpr>("init_macro");
        const Token *var_token = move2n_token(d->getTypeSpecStartLoc(), 2, sm, result.Context->getLangOpts());
        if (init_expr) {
          Dylinx::Instance().rw_ptr->RemoveText(
            SourceRange(
              var_token->getLocation(),
              sm.getImmediateExpansionRange(init_expr->getBeginLoc()).getEnd()
            )
          );
        }
        if (cur_type != pre_type)
          meta["extra_init"] = sm.getFileEntryForID(sm.getMainFileID())->getUID();

      } else if (const InitListExpr *init_expr = result.Nodes.getNodeAs<InitListExpr>("init_macro")) {

        char format[100];
        sprintf(format, "DYLINX_LOCK_INIT_%d", stash_id);
        Dylinx::Instance().rw_ptr->ReplaceText(
          sm.getImmediateExpansionRange(init_expr->getBeginLoc()).getAsRange(),
          format
        );
        meta["define_init"] = true;

      } else {
        // User doesn't specify initlist.
        char format[50];
        sprintf(format, " = DYLINX_LOCK_INIT_%d", stash_id);
        Dylinx::Instance().rw_ptr->InsertTextAfter(
          d->getEndLoc().getLocWithOffset(var_name.length()),
          format
        );
        meta["define_init"] = true;
      }

      meta["name"] = var_name;
      save2metas(cur_type, meta);
      save2altered_list(src_id, sm);
      pre_type = cur_type;
    }
  }
private:
  EntityID pre_type = {"/", -1, -1};
  uint32_t stash_id;
};

class CastMatchHandler: public MatchFinder::MatchCallback {
public:
  CastMatchHandler() {}
  virtual void run(const MatchFinder::MatchResult &result) {
    if (const CStyleCastExpr *cast_expr = result.Nodes.getNodeAs<CStyleCastExpr>("casting")) {
      SourceManager& sm = result.Context->getSourceManager();
// #ifdef __DYLINX_DEBUG__
//       DEBUG_LOG(CastPtr, cast_expr, sm);
// #endif
      SourceLocation begin_loc = cast_expr->getLParenLoc();
      SourceLocation end_loc = cast_expr->getRParenLoc();
      if (begin_loc.isMacroID()) {
        Dylinx::Instance().rw_ptr->ReplaceText(
          SourceRange(
            sm.getSpellingLoc(begin_loc).getLocWithOffset(1),
            sm.getSpellingLoc(end_loc).getLocWithOffset(-1)
          ),
          "generic_interface_t *"
        );
        begin_loc = sm.getSpellingLoc(begin_loc);
      }
      else {
        Dylinx::Instance().rw_ptr->ReplaceText(
          SourceRange(
            cast_expr->getLParenLoc().getLocWithOffset(1),
            cast_expr->getRParenLoc().getLocWithOffset(-1)
          ),
          "generic_interface_t *"
        );
      }
      FileID src_id = sm.getFileID(begin_loc);
      save2altered_list(src_id, sm);
    }
  }
};

class SlotIdentificationConsumer : public clang::ASTConsumer {
public:
  Rewriter rw;
  const LangOptions& opts;
  explicit SlotIdentificationConsumer(ASTContext *Context, const LangOptions& opts): opts{opts} {}
  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    SourceManager& sm = Context.getSourceManager();
    Dylinx::Instance().require_init = false;
    Dylinx::Instance().cu_deps.clear();
    Dylinx::Instance().cu_arrs.clear();
    Dylinx::Instance().rw_ptr = new Rewriter;
    Dylinx::Instance().rw_ptr->setSourceMgr(sm, opts);

    for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); it++) {
      const FileEntry *fentry = it->first;
      fs::path pth = fentry->getName().str();
      const FileID file_id = sm.translateFile(fentry);
      if (pth.filename() == "pthread.h") {
        SourceLocation header = sm.getIncludeLoc(file_id);
        uint32_t line = sm.getSpellingLineNumber(header);
        int32_t col = sm.getSpellingColumnNumber(header);
        Dylinx::Instance().rw_ptr->InsertTextAfter(
          header.getLocWithOffset(-1 * col),
          "\n#ifndef __DYLINX_REPLACE_PTHREAD_NATIVE__\n"
          "#define __DYLINX_REPLACE_PTHREAD_NATIVE__\n"
          "#define pthread_mutex_init pthread_mutex_init_original\n"
          "#define pthread_mutex_lock pthread_mutex_lock_original\n"
          "#define pthread_mutex_unlock pthread_mutex_unlock_original\n"
          "#define pthread_mutex_destroy pthread_mutex_destroy_original\n"
          "#define pthread_mutex_trylock pthread_mutex_destroy_original\n"
          "#define pthread_cond_wait pthread_cond_wait_original\n"
          "#define pthread_cond_timedwait pthread_mutex_destroy_original"
        );
        Dylinx::Instance().rw_ptr->InsertText(
          header.getLocWithOffset(std::string("<pthread.h>").length() + 1),
          "#undef pthread_mutex_init\n"
          "#undef pthread_mutex_lock\n"
          "#undef pthread_mutex_unlock\n"
          "#undef pthread_mutex_destroy\n"
          "#undef pthread_mutex_trylock\n"
          "#undef pthread_cond_wait\n"
          "#undef pthread_cond_timedwait"
          "#include \"dylinx-glue.h\"\n"
          "#endif //__DYLINX_REPLACE_PTHREAD_NATIVE__\n"
        );
      }
    }

    // Match all
    //
    //    a. pthread_mutex_t mutex;
    //    b. pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    //
    // and handle all the matched pattern into two ways. If the
    // instance locates in local scope(able to conduct expression
    // ), initialize the lock immediately as following.
    //
    //    DYLINX_LOCK_TYPE_1 lock = DYLINX_LOCK_INIT_1;
    //
    // On the other hand, if the lock is declared in the global
    // scope, whole global locks no matter static or not will be
    // initialized when entering main function.
#ifndef __linux
#error Current implementation is only for linux. VarDecl matcher rely on POSIX implementation.
#endif
    matcher.addMatcher(
      varDecl(
        hasType(hasUnqualifiedDesugaredType(recordType(
          hasDeclaration(recordDecl(has(fieldDecl(hasType(asString("struct __pthread_mutex_s"))))))
        ))),
        optionally(has(
          initListExpr(hasSyntacticForm(hasType(asString("pthread_mutex_t")))).bind("init_macro")
        ))).bind("vars"),
      &handler_for_vars
    );

    // Match all
    //    pthread_mutex_t locks[NUM_LOCK];
    // and convert them to
    //    DYLINX_LOCK_TYPE_1 locks[NUM_LOCK]; __dylinx_array_init_(locks, NUM_LOCK);
    matcher.addMatcher(
      varDecl(hasType(arrayType(hasElementType(qualType(asString("pthread_mutex_t")))))).bind("array_decls"),
      &handler_for_array
    );

    // Match all
    //    typedef pthread_mutex_t MyLock
    //    typedef pthread_mutext_t *MyLockPtr
    // and convert them to
    //    typedef generic_interface_t MyLock
    //    typedef generic_interface_t *MyLock
    matcher.addMatcher(
      typedefDecl(
        hasType(asString("pthread_mutex_t *"))
      ).bind("typedefs"),
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
        anyOf(
          hasType(asString("pthread_mutex_t *")),
          hasType(pointerType(pointee(hasUnqualifiedDesugaredType(recordType()))))
        ),
        optionally(anyOf(
          hasInitializer(hasDescendant(
            callExpr(
              callee(functionDecl(hasName("malloc"))),
              anyOf(
                hasDescendant(unaryExprOrTypeTraitExpr(hasArgumentOfType(qualType(asString("pthread_mutex_t")))).bind("sizeofExpr")),
                hasDescendant(unaryExprOrTypeTraitExpr(hasArgumentOfType(
                  hasUnqualifiedDesugaredType(recordType(hasDeclaration(recordDecl(has(fieldDecl())))))
                )).bind("sizeofExpr"))
              )
            ).bind("alloc_call")
          )),
          hasInitializer(hasDescendant(
            callExpr(
              callee(functionDecl(hasName("calloc"))),
              anyOf(
                hasDescendant(unaryExprOrTypeTraitExpr(hasArgumentOfType(qualType(asString("pthread_mutex_t")))).bind("sizeofExpr")),
                hasDescendant(unaryExprOrTypeTraitExpr(hasArgumentOfType(
                  hasUnqualifiedDesugaredType(recordType(hasDeclaration(recordDecl(has(fieldDecl())))))
                )).bind("sizeofExpr"))
              )
            ).bind("alloc_call")
          ))
        ))
      ).bind("res_decl"),
      &handler_for_malloc
    );

    matcher.addMatcher(
      binaryOperator(
        hasOperatorName("="),
        anyOf(
          hasRHS(hasDescendant(
           callExpr(
            callee(functionDecl(hasName("malloc"))),
            anyOf(
              hasDescendant(unaryExprOrTypeTraitExpr(hasArgumentOfType(qualType(asString("pthread_mutex_t")))).bind("sizeofExpr")),
              hasDescendant(unaryExprOrTypeTraitExpr(hasArgumentOfType(
                hasUnqualifiedDesugaredType(recordType(hasDeclaration(recordDecl(has(fieldDecl())))))
              )).bind("sizeofExpr"))
            )
          ).bind("alloc_call"))),
          hasRHS(hasDescendant(
            callExpr(callee( functionDecl(hasName("calloc"))),
            anyOf(
              hasDescendant(unaryExprOrTypeTraitExpr(hasArgumentOfType(qualType(asString("pthread_mutex_t")))).bind("sizeofExpr")),
              hasDescendant(unaryExprOrTypeTraitExpr(hasArgumentOfType(
                hasUnqualifiedDesugaredType(recordType(hasDeclaration(recordDecl(has(fieldDecl())))))
              )).bind("sizeofExpr"))
            )
          ).bind("alloc_call")))
        )
      ).bind("res_assign"),
      &handler_for_malloc
    );

    // Match all struct with pthred_mutex_t or pthread_mutex_t *
    // member
    //
    //    struct MyStruct {
    //      ....
    //      pthread_mutex_t my_mtx;
    //    };
    //
    // and convert the struct into following form.
    //
    //    struct MyStruct {
    //       ....
    //      DYLINX_LOCK_TYPE_1 my_mtx;
    //    };
    matcher.addMatcher(
      fieldDecl(eachOf(
        hasType(asString("pthread_mutex_t")),
        hasType(asString("pthread_mutex_t *"))
      )).bind("struct_members"),
      &handler_for_struct
    );

    // Although pthread_mutex_t member and regular lock instance
    // seems similar, instead of forcing these instance to initialize
    // right after declaration, we make them initialized with specific
    // function just like pthread_mutex_init().
    //
    //    struct MyStruct { pthread_mutex_t mtx; int a; };
    //    typedef struct MyStruct alias_t;
    //    struct MyStruct ins1 = {PTHREAD_MUTEX_INITIALIZER, 0};
    //    alias_t ins2 = {PTHREAD_MUTEX_INITIALIZER, 0};
    //
    // Convert the above pattern as following.
    //
    //    struct MyStruct { DYLINX_LOCK_TYPE_1 mtx; int a; };
    //    typedef struct MyStruct alias_t;
    //    struct MyStruct instance = {DYLINX_MyStruct_MEMBER_INIT_1, 0}
    //    alias_t ins2 = {DYLINX_MyStruct_MEMBER_INIT_1, 0};
    //
    // Note:
    // Current implementation is able to deal recursive "typedef". Since
    // user may define nested structure with pthread_mutex_t member and
    // the macro is named after the most exterior struct name, it is
    // possible that the macro is undefined.
    matcher.addMatcher(
      varDecl(
        hasType(hasUnqualifiedDesugaredType(recordType(
          hasDeclaration(recordDecl(has(fieldDecl())))
        ))),
        optionally(
        forEachDescendant(
          initListExpr(hasSyntacticForm(
            hasType(asString("pthread_mutex_t"))
          )).bind("struct_member_init")
        ))
      ).bind("struct_instance"),
      &handler_for_initlist
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
      cStyleCastExpr(hasDestinationType(asString("pthread_mutex_t *")))
      .bind("casting"),
      &handler_for_casting
    );
    // There's no casting need in memcached
    // matcher.addMatcher(
    //   callExpr(
    //     hasAnyArgument(hasType(asString("pthread_mutex_t *")))
    //   ).bind("ptr_ref"),
    //   &handler_for_ref
    // );

    matcher.matchAST(Context);
  }
private:
  MatchFinder matcher;
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
  CastMatchHandler handler_for_casting;
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
#ifdef __DYLINX_DEBUG__
    printf("[ End UNIT ]\n");
#endif

    if (Dylinx::Instance().require_init) {
      SourceLocation end = sm.getLocForEndOfFile(sm.getMainFileID());
      uint32_t main_id = sm.getFileEntryForID(sm.getMainFileID())->getUID();
      char prototype[100];
      sprintf(
        prototype,
        "void __dylinx_cu_init_%d_() {\n",
        main_id
      );
      std::string global_initializer(prototype);
      YAML::Node entity = Dylinx::Instance().lock_decl["LockEntity"];
      for (YAML::const_iterator it = entity.begin(); it != entity.end(); it++) {
        const YAML::Node& entity = *it;
        if (entity["extra_init"] && entity["extra_init"].as<uint32_t>() == main_id) {
          char init_mtx[200];
          std::string var_name = entity["name"].as<std::string>();
          if (entity["modification_type"].as<std::string>() == std::string(ARR_ALLOCA_TYPE)) {
            sprintf(
              init_mtx,
              "\t__dylinx_array_init_(&%s, %s);\n",
              var_name.c_str(),
              Dylinx::Instance().cu_arrs[var_name].c_str()
            );
          } else if (entity["modification_type"].as<std::string>() == std::string(VAR_FIELD_INIT)) {
            std::vector<std::string> init_str = Dylinx::Instance().cu_recr[var_name];
            std::string concat_init = "";
            for (auto it = init_str.begin(); it != init_str.end(); it++) {
              std::string field_name = var_name + *it;
              sprintf(
                init_mtx,
                "\t__dylinx_member_init_(&%s, NULL);\n",
                field_name.c_str()
              );
              concat_init = concat_init + init_mtx;
            }
            global_initializer.append(concat_init);
            continue;
          }
          else
            sprintf(init_mtx, "\t__dylinx_member_init_(&%s, NULL);\n", var_name.c_str());
          global_initializer.append(init_mtx);
        }
      }
      global_initializer.append("}");
      Dylinx::Instance().rw_ptr->InsertText(
        end,
        global_initializer
      );
      save2altered_list(sm.getMainFileID(), sm);
    }
    std::set<FileID>::iterator iter;
    for (iter = Dylinx::Instance().cu_deps.begin(); iter != Dylinx::Instance().cu_deps.end(); iter++) {
      std::string filename = sm.getFileEntryForID(*iter)->getName().str();
      write_modified_file(filename, *iter, sm);
      Dylinx::Instance().altered_files.insert(filename);
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

// TODO
// Not yet implement array of struct and struct of array

int main(int argc, const char **argv) {
  std::string err;
  const char *compiler_db_path = argv[1];
  fs::path revert = fs::path(std::string(argv[1])).parent_path() / ".dylinx";
  Dylinx::Instance().yaml_fout = std::ofstream(argv[2]);
  const char *glue_env = std::getenv("DYLINX_HOME");
  if (!glue_env) {
    fprintf(stderr, "[ERROR] It is required to set DYLINX_HOME\n");
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

  std::set<fs::path>::iterator it;
  for (it = Dylinx::Instance().altered_files.begin(); it != Dylinx::Instance().altered_files.end(); it++)
    Dylinx::Instance().lock_decl["AlteredFiles"].push_back(it->string());

  YAML::Node updated_file = Dylinx::Instance().lock_decl["AlteredFiles"];
  for (YAML::const_iterator it = updated_file.begin(); it != updated_file.end(); it++) {
    fs::path u =  (*it).as<std::string>();
    fs::copy_file(u, revert / u.filename(), fs::copy_options::skip_existing);
    fs::remove(u);
    fs::copy(Dylinx::Instance().temp_dir / u.filename(), u);
  }
  YAML::Emitter out;
  out << Dylinx::Instance().lock_decl;
  Dylinx::Instance().yaml_fout << out.c_str();
  Dylinx::Instance().yaml_fout.close();
}
