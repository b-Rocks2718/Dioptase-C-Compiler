#ifndef AST_H
#define AST_H

#include <stddef.h>
#include <stdbool.h>

#include "slice.h"
#include "identifier_map.h"
#include "types.h"

/* AST data structures */

struct Program {
  struct DeclarationList* dclrs;
};

// The param type list stores type, next.
struct ParamTypeList {
  struct Type* type;
  struct ParamTypeList* next;
};

// Identify the possible dclr type values.
enum DclrType {
  VAR_DCLR,
  FUN_DCLR,
  STRUCT_DCLR,
  UNION_DCLR,
  ENUM_DCLR,
  MEMBER_DCLR,
};

// Identify the possible initializer type values.
enum InitializerType {
  SINGLE_INIT,
  COMPOUND_INIT,
};

// The initializer list stores init, next.
struct InitializerList {
  struct Initializer* init;
  struct InitializerList* next;
};

// The initializer variant stores single_init, compound_init.
union InitializerVariant {
  struct Expr* single_init;
  struct InitializerList* compound_init;
};

// The initializer stores init_type, init, type, loc.
struct Initializer {
  enum InitializerType init_type;
  union InitializerVariant init;
  struct Type* type; // target type for this initializer
  const char* loc; // location in source for error reporting
};

// The var attributes stores cleanup_func.
struct VarAttributes {
  struct Slice* cleanup_func;
};

// The variable dclr stores name, init, type, storage, and other fields.
struct VariableDclr {
  struct Slice* name;
  struct Initializer* init;
  struct Type* type;
  enum StorageClass storage;
  struct VarAttributes attributes;
};

// The function dclr stores name, storage, params, type, and other fields.
struct FunctionDclr {
  struct Slice* name;
  enum StorageClass storage;
  struct ParamList* params;
  struct Type* type;
  struct Block* body;
};

// The member dclr stores name, type, next.
struct MemberDclr {
  struct Slice* name;
  struct Type* type;
  struct MemberDclr* next;
};

// The enum member dclr stores name, value, next.
struct EnumMemberDclr {
  struct Slice* name;
  int value;
  struct EnumMemberDclr* next;
};

// The struct dclr stores name, members.
struct StructDclr {
  struct Slice* name;
  struct MemberDclr* members;
};

// The union dclr stores name, members.
struct UnionDclr {
  struct Slice* name;
  struct MemberDclr* members;
};

// The enum dclr stores name, members.
struct EnumDclr {
  struct Slice* name;
  struct EnumMemberDclr* members;
};

// The declare variant stores var_dclr, fun_dclr, struct_dclr, union_dclr, and other fields.
union DeclareVariant {
  struct VariableDclr var_dclr;
  struct FunctionDclr fun_dclr;
  struct StructDclr struct_dclr;
  struct UnionDclr union_dclr;
  struct EnumDclr enum_dclr;
  struct MemberDclr member_dclr;
};

// The param list stores param, next.
struct ParamList {
  struct VariableDclr param;
  struct ParamList* next;
};

// The declaration stores dclr, type.
struct Declaration {
  union DeclareVariant dclr;
  enum DclrType type;
};

// The declaration list stores dclr, next.
struct DeclarationList {
  struct Declaration dclr;
  struct DeclarationList* next;
};

// Identify the possible expr type values.
enum ExprType {
  BINARY,
  ASSIGN,
  POST_ASSIGN,
  CONDITIONAL,
  LIT,
  UNARY,
  VAR,
  FUNCTION_CALL,
  CAST,
  ADDR_OF,
  DEREFERENCE,
  SUBSCRIPT,
  STRING,
  SIZEOF_EXPR,
  SIZEOF_T_EXPR,
  STMT_EXPR,
  DOT_EXPR,
  ARROW_EXPR,
};

// Identify the possible bin op values.
enum BinOp {
  ADD_OP = 1,
  SUB_OP,
  MUL_OP,
  DIV_OP,
  MOD_OP,
  BIT_AND,
  BIT_OR,
  BIT_XOR,
  BIT_SHR,
  BIT_SHL,
  BOOL_AND,
  BOOL_OR,
  BOOL_EQ,
  BOOL_NEQ,
  BOOL_LE,
  BOOL_GE,
  BOOL_LEQ,
  BOOL_GEQ,
  ASSIGN_OP,
  PLUS_EQ_OP,
  MINUS_EQ_OP,
  MUL_EQ_OP,
  DIV_EQ_OP,
  MOD_EQ_OP,
  AND_EQ_OP,
  OR_EQ_OP,
  XOR_EQ_OP,
  SHL_EQ_OP,
  SHR_EQ_OP,
  TERNARY_OP,
  COMMA_OP,
};

// Identify the possible un op values.
enum UnOp {
  COMPLEMENT = 1,
  NEGATE,
  BOOL_NOT,
  UNARY_PLUS,
};

// The binary expr stores op, left, right.
struct BinaryExpr {
  enum BinOp op;
  struct Expr* left;
  struct Expr* right;
};

// The assign expr stores left, right.
struct AssignExpr {
  struct Expr* left;
  struct Expr* right;
};

// Identify the possible post op values.
enum PostOp {
  POST_INC,
  POST_DEC
};

// The post assign expr stores op, expr.
struct PostAssignExpr {
  enum PostOp op;
  struct Expr* expr;
};

// The conditional expr stores condition, left, right.
struct ConditionalExpr {
  struct Expr* condition;
  struct Expr* left;
  struct Expr* right;
};

// Identify the possible const type values.
enum ConstType {
  INT_CONST,
  UINT_CONST,
  LONG_CONST,
  ULONG_CONST
};

// The const variant stores char_val, short_val, ushort_val, int_val, and other fields.
union ConstVariant {
  char char_val;
  short short_val;
  unsigned short ushort_val;
  int int_val;
  unsigned uint_val;
  long long_val;
  unsigned long ulong_val;
};

// The lit expr stores type, value.
struct LitExpr {
  enum ConstType type;
  union ConstVariant value;
};

// The unary expr stores op, expr.
struct UnaryExpr {
  enum UnOp op;
  struct Expr* expr;
};

// The var expr stores name.
struct VarExpr {
  struct Slice* name;
};

// The function call expr stores func, args.
struct FunctionCallExpr {
  struct Expr* func; // name or pointer
  struct ArgList* args;
};

// The cast expr stores target, expr.
struct CastExpr {
  struct Type* target;
  struct Expr* expr;
};

// The addr of expr stores expr.
struct AddrOfExpr {
  struct Expr* expr;
};

// The dereference expr stores expr.
struct DereferenceExpr {
  struct Expr* expr;
};

// The subscript expr stores array, index.
struct SubscriptExpr {
  struct Expr* array;
  struct Expr* index;
};

// The string expr stores string.
struct StringExpr {
  struct Slice* string;
};

// The size of expr stores expr.
struct SizeOfExpr {
  struct Expr* expr;
};

// The size of texpr stores type.
struct SizeOfTExpr {
  struct Type* type;
};

// The stmt expr stores block.
struct StmtExpr {
  struct Block* block;
};

// The dot expr stores struct_expr, member.
struct DotExpr {
  struct Expr* struct_expr;
  struct Slice* member;
};

// The arrow expr stores pointer_expr, member.
struct ArrowExpr {
  struct Expr* pointer_expr;
  struct Slice* member;
};

// The expr variant stores bin_expr, assign_expr, post_assign_expr, conditional_expr, and other fields.
union ExprVariant {
  struct BinaryExpr bin_expr;
  struct AssignExpr assign_expr;
  struct PostAssignExpr post_assign_expr;
  struct ConditionalExpr conditional_expr;
  struct LitExpr lit_expr;
  struct UnaryExpr un_expr;
  struct VarExpr var_expr;
  struct FunctionCallExpr fun_call_expr;
  struct CastExpr cast_expr;
  struct AddrOfExpr addr_of_expr;
  struct DereferenceExpr deref_expr;
  struct SubscriptExpr subscript_expr;
  struct StringExpr string_expr;
  struct SizeOfExpr sizeof_expr;
  struct SizeOfTExpr sizeof_t_expr;
  struct StmtExpr stmt_expr;
  struct DotExpr dot_expr;
  struct ArrowExpr arrow_expr;
};

// The expr stores loc, value_type, type, expr.
struct Expr {
  const char* loc; // start of this expression in the preprocessed source
  struct Type* value_type;
  enum ExprType type;
  union ExprVariant expr;
};

// The arg list stores arg, next.
struct ArgList {
  struct Expr* arg;
  struct ArgList* next;
};

// Identify the possible statement type values.
enum StatementType {
  RETURN_STMT,
  EXPR_STMT,
  IF_STMT,
  GOTO_STMT,
  LABELED_STMT,
  COMPOUND_STMT,
  BREAK_STMT,
  CONTINUE_STMT,
  WHILE_STMT,
  DO_WHILE_STMT,
  FOR_STMT,
  SWITCH_STMT,
  CASE_STMT,
  DEFAULT_STMT,
  NULL_STMT
};

// The return stmt stores expr, func.
struct ReturnStmt {
  struct Expr* expr;
  struct Slice* func;
};

// The expr stmt stores expr.
struct ExprStmt {
  struct Expr* expr;
};

// The if stmt stores condition, if_stmt, else_stmt.
struct IfStmt {
  struct Expr* condition;
  struct Statement* if_stmt;
  struct Statement* else_stmt;
};

// The goto stmt stores label.
struct GotoStmt {
  struct Slice* label;
};

// The labeled stmt stores label, stmt.
struct LabeledStmt {
  struct Slice* label;
  struct Statement* stmt;
};

// The compound stmt stores block.
struct CompoundStmt {
  struct Block* block;
};

// The break stmt stores label.
struct BreakStmt {
  struct Slice* label;
};

// The continue stmt stores label.
struct ContinueStmt {
  struct Slice* label;
};

// The while stmt stores condition, statement, label.
struct WhileStmt {
  struct Expr* condition;
  struct Statement* statement;
  struct Slice* label;
};

// The do while stmt stores statement, condition, label.
struct DoWhileStmt {
  struct Statement* statement;
  struct Expr* condition;
  struct Slice* label;
};

// The for stmt stores init, condition, end, statement, and other fields.
struct ForStmt {
  struct ForInit* init;
  struct Expr* condition;
  struct Expr* end;
  struct Statement* statement;
  struct Slice* label;
  struct IdentMap* init_idents;
};

// Identify the possible for init type values.
enum ForInitType {
  DCLR_INIT,
  EXPR_INIT,
};  

// The for init variant stores dclr_init, expr_init.
union ForInitVariant {
  struct VariableDclr* dclr_init;
  struct Expr* expr_init;
};

// The for init stores type, init.
struct ForInit {
  enum ForInitType type;
  union ForInitVariant init;
};

// The switch stmt stores condition, statement, label, cases.
struct SwitchStmt {
  struct Expr* condition;
  struct Statement* statement;
  struct Slice* label;
  struct CaseList* cases;
};

// The case stmt stores expr, statement, label.
struct CaseStmt {
  struct Expr* expr;
  struct Statement* statement;
  struct Slice* label;
};

// The default stmt stores statement, label.
struct DefaultStmt {
  struct Statement* statement;
  struct Slice* label;
};

// The null stmt stores ret_stmt, expr_stmt, if_stmt, goto_stmt, and other fields.
struct NullStmt {};

// The statement variant stores ret_stmt, expr_stmt, if_stmt, goto_stmt, and other fields.
union StatementVariant {
  struct ReturnStmt ret_stmt;
  struct ExprStmt expr_stmt;
  struct IfStmt if_stmt;
  struct GotoStmt goto_stmt;
  struct LabeledStmt labeled_stmt;
  struct CompoundStmt compound_stmt;
  struct BreakStmt break_stmt;
  struct ContinueStmt continue_stmt;
  struct WhileStmt while_stmt;
  struct DoWhileStmt do_while_stmt;
  struct ForStmt for_stmt;
  struct SwitchStmt switch_stmt;
  struct CaseStmt case_stmt;
  struct DefaultStmt default_stmt;
  struct NullStmt null_stmt;
};

// The statement stores loc, statement, type.
struct Statement {
  const char* loc; // start of this statement in the preprocessed source
  union StatementVariant statement;
  enum StatementType type;
};

// Identify the possible block item type values.
enum BlockItemType {
  DCLR_ITEM,
  STMT_ITEM
};

// The block item variant stores stmt, dclr.
union BlockItemVariant {
  struct Statement* stmt;
  struct Declaration* dclr;
};

// The block item stores item, type.
struct BlockItem {
  union BlockItemVariant item;
  enum BlockItemType type;
};

// The block stores item, idents, next.
struct Block {
  struct BlockItem* item;
  struct IdentMap* idents;
  struct Block* next;
};

// Identify the possible case label type values.
enum CaseLabelType {
  INT_CASE,
  DEFAULT_CASE
};

// The case label stores type, data.
struct CaseLabel {
  enum CaseLabelType type;
  int data;
};

// The case list stores case_label, next.
struct CaseList {
  struct CaseLabel case_label;
  struct CaseList* next;
};

// Identify the possible declarator type values.
enum DeclaratorType {
  IDENT_DEC,
  POINTER_DEC,
  FUN_DEC,
  ARRAY_DEC,
};

// The ident dec stores name.
struct IdentDec {
  struct Slice* name;
};

// The pointer dec stores decl.
struct PointerDec {
  struct Declarator* decl;
};

// The fun dec stores params, decl.
struct FunDec {
  struct ParamInfoList* params;
  struct Declarator* decl;
};

// The array dec stores decl, size.
struct ArrayDec {
  struct Declarator* decl;
  size_t size;
};

// The declarator variant stores ident_dec, pointer_dec, fun_dec, array_dec.
union DeclaratorVariant {
  struct IdentDec ident_dec;
  struct PointerDec pointer_dec;
  struct FunDec fun_dec;
  struct ArrayDec array_dec;
};

// The declarator stores type, declarator.
struct Declarator {
  enum DeclaratorType type;
  union DeclaratorVariant declarator;
};

// The param info stores type, decl.
struct ParamInfo {
  struct Type* type;
  struct Declarator decl;
};

// The param info list stores info, next.
struct ParamInfoList {
  struct ParamInfo info;
  struct ParamInfoList* next;
};

// Identify the possible abstract declarator type values.
enum AbstractDeclaratorType {
  ABSTRACT_POINTER,
  ABSTRACT_ARRAY,
  ABSTRACT_FUNCTION,
  ABSTRACT_BASE,
};

// The abstract pointer stores next.
struct AbstractPointer {
  struct AbstractDeclarator* next;
};

// The abstract array stores next, size.
struct AbstractArray {
  struct AbstractDeclarator* next;
  size_t size;
};

// The abstract function stores next, params.
struct AbstractFunction {
  struct AbstractDeclarator* next;
  struct ParamTypeList* params;
};

// The abstract declarator variant stores pointer_type, array_type, function_type.
union AbstractDeclaratorVariant {
  struct AbstractPointer* pointer_type;
  struct AbstractArray* array_type;
  struct AbstractFunction* function_type;
  // no data for AbstractBase
};

// The abstract declarator stores type, data.
struct AbstractDeclarator {
  enum AbstractDeclaratorType type;
  union AbstractDeclaratorVariant data;
};

// Identify the possible type specifier type values.
enum TypeSpecifierType {
  INT_SPEC = 1,
  UNSIGNED_SPEC,
  SIGNED_SPEC,
  LONG_SPEC,
  SHORT_SPEC,
  CHAR_SPEC,
  VOID_SPEC,
  STRUCT_SPEC,
  UNION_SPEC,
  ENUM_SPEC,
};

// The type specifier stores type, name.
struct TypeSpecifier {
  enum TypeSpecifierType type;
  struct Slice* name;
};

// The type spec list stores spec, next.
struct TypeSpecList {
  struct TypeSpecifier spec;
  struct TypeSpecList* next;
};

// The storage class list stores spec, next.
struct StorageClassList {
  enum StorageClass spec;
  struct StorageClassList* next;
};

// Identify the possible dclr prefix type values.
enum DclrPrefixType {
  STORAGE_PREFIX,
  TYPE_PREFIX
};

// The dclr prefix variant stores type_spec, storage_class.
union DclrPrefixVariant {
  struct TypeSpecifier type_spec;
  enum StorageClass storage_class;
};

// The dclr prefix stores type, prefix.
struct DclrPrefix {
  enum DclrPrefixType type;
  union DclrPrefixVariant prefix;
};

/*-------------------------------------------------------------------------------------------------------*/

/* show instances */

void print_expr(struct Expr* expr, int tabs);

void print_bin_expr(struct BinaryExpr* expr, int tabs);

void print_un_expr(struct UnaryExpr* expr, int tabs);

void print_assign_expr(struct AssignExpr* expr, int tabs);

void print_post_assign_expr(struct PostAssignExpr* expr, int tabs);

void print_conditional_expr(struct ConditionalExpr* expr, int tabs);

void print_lit_expr(struct LitExpr* expr);

void print_var_expr(struct VarExpr* expr);

void print_fun_call_expr(struct FunctionCallExpr* expr, int tabs);

void print_cast_expr(struct CastExpr* expr, int tabs);

void print_addr_of_expr(struct AddrOfExpr* expr, int tabs);

void print_dereference_expr(struct DereferenceExpr* expr, int tabs);

void print_type(struct Type* type);

void print_stmt(struct Statement* stmt, int tabs);

void print_declaration(struct Declaration* declaration, int tabs);

void print_var_dclr(struct VariableDclr* var_dclr, int tabs);

void print_block(struct Block* block, int tabs);

void print_prog(struct Program* prog);

void print_initializer(struct Initializer* init, int tabs);

void print_subscript_expr(struct SubscriptExpr* expr, int tabs);

bool compare_types(struct Type* a, struct Type* b);

#endif // AST_H
