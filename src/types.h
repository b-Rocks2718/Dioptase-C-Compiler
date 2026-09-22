#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stddef.h>

// Enumerate C storage classes tracked by declaration/type checking.
enum StorageClass {
  NONE,
  STATIC,
  EXTERN
};

// Classify primitive, pointer, aggregate, and function types.
enum TypeType {
  CHAR_TYPE,
  SCHAR_TYPE,
  UCHAR_TYPE,
  INT_TYPE,
  LONG_TYPE,
  SHORT_TYPE,
  UINT_TYPE,
  ULONG_TYPE,
  USHORT_TYPE,
  FUN_TYPE,
  POINTER_TYPE,
  ARRAY_TYPE,
  VOID_TYPE,
  STRUCT_TYPE,
  UNION_TYPE,
  ENUM_TYPE,
};

// Describe function parameter types and return type.
struct FunType {
  struct ParamTypeList* param_types;
  struct Type* return_type;
};

// Identify the type referenced by a pointer.
struct PointerType {
  struct Type* referenced_type;
};

// Describe an array's element type and element count.
struct ArrayType {
  struct Type* element_type;
  size_t size;
};

// Reference a named struct tag in the type system.
struct StructType {
  struct Slice* name;
};

// Reference a named union tag in the type system.
struct UnionType {
  struct Slice* name;
};

// Reference a named enum tag in the type system.
struct EnumType {
  struct Slice* name;
};

// Select the concrete payload associated with a TypeType value.
union TypeVariant {
  struct FunType fun_type;
  struct PointerType pointer_type;
  struct ArrayType array_type;
  struct StructType struct_type;
  struct UnionType union_type;
  struct EnumType enum_type;
  // no data for other types
};

// Store a type kind, its const qualifier, and its variant-specific payload.
// is_const qualifies this type node. A pointer's const is independent of the
// const on the type it points at, so `const int *` and `int * const` differ.
struct Type {
  enum TypeType type;
  bool is_const;
  union TypeVariant type_data;
};

#endif // TYPES_H
