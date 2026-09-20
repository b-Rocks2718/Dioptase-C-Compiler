#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>

// Identify the possible storage class values.
enum StorageClass {
  NONE,
  STATIC,
  EXTERN
};

// Identify the possible type type values.
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

// The fun type stores param_types, return_type.
struct FunType {
  struct ParamTypeList* param_types;
  struct Type* return_type;
};

// The pointer type stores referenced_type.
struct PointerType {
  struct Type* referenced_type;
};

// The array type stores element_type, size.
struct ArrayType {
  struct Type* element_type;
  size_t size;
};

// The struct type stores name.
struct StructType {
  struct Slice* name;
};

// The union type stores name.
struct UnionType {
  struct Slice* name;
};

// The enum type stores name.
struct EnumType {
  struct Slice* name;
};

// The type variant stores fun_type, pointer_type, array_type, struct_type, and other fields.
union TypeVariant {
  struct FunType fun_type;
  struct PointerType pointer_type;
  struct ArrayType array_type;
  struct StructType struct_type;
  struct UnionType union_type;
  struct EnumType enum_type;
  // no data for other types
};

// The type stores type, type_data.
struct Type {
  enum TypeType type;
  union TypeVariant type_data;
};

#endif // TYPES_H
