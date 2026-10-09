#include "asm_gen.h"
#include "exit_codes.h"
#include "arena.h"
#include "typechecking.h"
#include "unique_name.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

struct PseudoMap* pseudo_map = NULL;

// True while lowering a function that takes the address of one of its own
// frame-allocated variables. Set per function by top_level_to_asm.
static bool frame_address_taken = false;

static struct Slice text_directive_slice = {"text", 4};
static struct Slice data_directive_slice = {"data", 4};

// Use caller-saved registers that are not argument registers for scratch work.
const enum Reg kScratchRegA = R9;
const enum Reg kScratchRegB = R10;

static const size_t REG_ARG_LIMIT = 8;
static const size_t kStackSlotBytes = 4;

static const char kTempMarker[] = ".tmp.";
static const size_t kTempMarkerLen = sizeof(kTempMarker) - 1;

struct AsmSymbolTable* asm_symbol_table = NULL;

static struct AsmType kByteType = { .type = BYTE };
static struct AsmType kDoubleType = { .type = DOUBLE };
static struct AsmType kWordType = { .type = WORD };
static struct AsmType kLongWordType = { .type = LONG_WORD };

const struct Operand kStackMem = {
  .type = OPERAND_MEMORY,
  .asm_type = &kWordType,
  .op = {
    .memory = {
      .base = SP,
      .offset = 0,
    },
  },
};

// Return the identifier stored by a pseudo, pseudo-mem, or data operand.
static struct Slice* operand_symbol_name(const struct Operand* opr) {
  if (opr == NULL) {
    return NULL;
  }
  switch (opr->type) {
    case OPERAND_PSEUDO:
      return opr->op.pseudo.name;
    case OPERAND_PSEUDO_MEM:
      return opr->op.pseudo_mem.name;
    case OPERAND_DATA:
      return opr->op.data.label;
    default:
      return NULL;
  }
}

// Identify aggregate types that require byte-wise copies.
// Returns true for arrays, structs, and unions.
static bool is_aggregate_type(const struct Type* type) {
  if (type == NULL) {
    return false;
  }
  return type->type == ARRAY_TYPE ||
         type->type == STRUCT_TYPE ||
         type->type == UNION_TYPE;
}

// Determine the base alignment for an operand's backing storage.
// Returns the alignment in bytes.
static size_t operand_base_alignment(const struct Operand* opr) {
  if (opr == NULL || opr->asm_type == NULL) {
    return 1;
  }

  size_t alignment = asm_type_alignment(opr->asm_type);
  struct Slice* name = operand_symbol_name(opr);
  if ((opr->type == OPERAND_PSEUDO ||
       opr->type == OPERAND_PSEUDO_MEM ||
       opr->type == OPERAND_DATA) &&
      name != NULL &&
      asm_symbol_table != NULL) {
    struct AsmSymbolEntry* sym_entry = asm_symbol_table_get(asm_symbol_table, name);
    if (sym_entry != NULL && sym_entry->type != NULL) {
      alignment = asm_type_alignment(sym_entry->type);
    }
  }

  return alignment;
}

// Map a checked C type to its assembly representation.
struct AsmType* type_to_asm_type(struct Type* type){
  switch (type->type){
    case CHAR_TYPE:
    case SCHAR_TYPE:
    case UCHAR_TYPE:
      return &kByteType;
    case SHORT_TYPE:
    case USHORT_TYPE:
      return &kDoubleType;
    case INT_TYPE:
    case UINT_TYPE:
    case ENUM_TYPE:
    case POINTER_TYPE:
      return &kWordType;
    case LONG_TYPE:
    case ULONG_TYPE:
      return &kLongWordType;
    case ARRAY_TYPE: 
    case STRUCT_TYPE:
    case UNION_TYPE: {
      struct AsmType* asm_type = arena_alloc(sizeof(struct AsmType));
      asm_type->type = BYTE_ARRAY;
      asm_type->byte_array.size = get_type_size(type);
      asm_type->byte_array.alignment = type_alignment(type, NULL);
      return asm_type;
    }
    case FUN_TYPE:
      return NULL; // dont error, functions just have no asm type
    default:
      // unknown type
      asm_gen_error("symbol table", NULL, "invalid type %d for ASM symbol conversion", type->type);
      return NULL;
  }
}

// Create a fresh pseudo temp for byte-copy ops and register it in the ASM symbol table.
// Returns an OPERAND_PSEUDO operand with a unique temp name.
static struct Operand* make_asm_temp(struct Slice* func_name, struct AsmType* asm_type) {
  if (asm_symbol_table == NULL) {
    asm_gen_error("copy-bytes", func_name, "asm symbol table not initialized before temp creation");
  }

  struct Operand* temp = arena_alloc(sizeof(struct Operand));
  temp->type = OPERAND_PSEUDO;
  temp->op.pseudo.name = make_unique_label(func_name, "tmp.asm");
  temp->asm_type = asm_type;

  asm_symbol_table_insert(asm_symbol_table, temp->op.pseudo.name, asm_type, false, false, false);
  return temp;
}

// An ASM instruction list under construction, with O(1) append.
struct AsmList {
  struct AsmInstr* head;
  struct AsmInstr* tail;
};

// Append one instruction, or a chain linked through `next`, to a list. NULL is
// ignored so helpers that may emit nothing (a zero-byte copy) compose freely.
static void asm_emit(struct AsmList* list, struct AsmInstr* instrs) {
  if (instrs == NULL) {
    return;
  }
  if (list->head == NULL) {
    list->head = instrs;
  } else {
    list->tail->next = instrs;
  }
  while (instrs->next != NULL) {
    instrs = instrs->next;
  }
  list->tail = instrs;
}

// Return the length of an operand list.
// list is the operand list head (may be NULL).
static size_t operand_list_length(const struct OperandList* list) {
  size_t count = 0;
  for (const struct OperandList* cur = list; cur != NULL; cur = cur->next) {
    count++;
  }
  return count;
}

// Fetch an operand from a list by index.
// Returns the operand pointer at index or errors if out of range.
static struct Operand* operand_list_get(const struct OperandList* list, size_t index) {
  const struct OperandList* cur = list;
  for (size_t i = 0; i < index; i++) {
    if (cur == NULL) {
      asm_gen_error("operand-list", NULL, "operand list index %zu out of range", index);
    }
    cur = cur->next;
  }
  if (cur == NULL || cur->opr == NULL) {
    asm_gen_error("operand-list", NULL, "operand list index %zu out of range", index);
  }
  return cur->opr;
}

// Build a direct memory operand.
// Returns a new OPERAND_MEMORY operand.
static struct Operand* make_asm_mem(enum Reg base, int offset, struct AsmType* asm_type) {
  if (asm_type == NULL) {
    asm_gen_error("operand", NULL, "NULL asm type for memory operand");
  }
  struct Operand* opr = arena_alloc(sizeof(struct Operand));
  opr->type = OPERAND_MEMORY;
  opr->op.memory.base = base;
  opr->op.memory.offset = offset;
  opr->asm_type = asm_type;
  return opr;
}

// Create an operand representing base+offset for byte/word accesses.
// Returns a new operand with adjusted offset.
static struct Operand* add_offset_typed(struct Operand* base, int offset, struct AsmType* asm_type) {
  if (base == NULL) {
    asm_gen_error("operand", NULL, "NULL base operand for offset");
  }
  switch (base->type) {
    case OPERAND_PSEUDO_MEM:
      return make_pseudo_mem(base->op.pseudo_mem.name, asm_type, base->op.pseudo_mem.offset + offset);
    case OPERAND_PSEUDO:
      return make_pseudo_mem(base->op.pseudo.name, asm_type, offset);
    case OPERAND_MEMORY:
      return make_asm_mem(base->op.memory.base, base->op.memory.offset + offset, asm_type);
    case OPERAND_DATA:
      {
        struct Operand* opr = arena_alloc(sizeof(struct Operand));
        opr->type = OPERAND_DATA;
        opr->op.data.label = base->op.data.label;
        opr->asm_type = asm_type;
        opr->op.data.offset = base->op.data.offset + offset;
        return opr;
      }
    case OPERAND_REG:
      if (offset != 0) {
        asm_gen_error("operand", NULL, "register operand cannot take offset");
      }
      {
        struct Operand* opr = arena_alloc(sizeof(struct Operand));
        *opr = *base;
        opr->asm_type = asm_type;
        return opr;
      }
    default:
      asm_gen_error("operand", NULL, "unsupported operand type %d for offset", (int)base->type);
      return NULL;
  }
}

// ---------------------------------------------------------------------------
// ASM IR constructors. The arena does not zero memory, so every node built here
// is fully initialized; operands may be shared between instructions because
// passes replace operand fields, never the operands themselves.
// ---------------------------------------------------------------------------

static struct Operand* reg_operand(enum Reg reg, struct AsmType* asm_type) {
  struct Operand* opr = arena_alloc(sizeof(struct Operand));
  memset(opr, 0, sizeof(*opr));
  opr->type = OPERAND_REG;
  opr->op.reg.reg = reg;
  opr->asm_type = asm_type;
  return opr;
}

static struct Operand* lit_operand(int value, struct AsmType* asm_type) {
  struct Operand* opr = arena_alloc(sizeof(struct Operand));
  memset(opr, 0, sizeof(*opr));
  opr->type = OPERAND_LIT;
  opr->op.lit.value = value;
  opr->asm_type = asm_type;
  return opr;
}

// Allocate a zeroed, unlinked instruction of the given kind.
static struct AsmInstr* new_asm_instr(enum AsmInstrType type) {
  struct AsmInstr* instr = arena_alloc(sizeof(struct AsmInstr));
  memset(instr, 0, sizeof(*instr));
  instr->type = type;
  return instr;
}

static struct AsmInstr* asm_mov(struct Operand* dst, struct Operand* src) {
  struct AsmInstr* instr = new_asm_instr(ASM_MOV);
  instr->instr.asm_mov.dst = dst;
  instr->instr.asm_mov.src = src;
  return instr;
}

static struct AsmInstr* asm_binary(enum ALUOp op, struct Operand* dst,
                                   struct Operand* src1, struct Operand* src2) {
  struct AsmInstr* instr = new_asm_instr(ASM_BINARY);
  instr->instr.asm_binary.alu_op = op;
  instr->instr.asm_binary.dst = dst;
  instr->instr.asm_binary.src1 = src1;
  instr->instr.asm_binary.src2 = src2;
  return instr;
}

// `SP = SP <op> bytes`, used to allocate (ALU_SUB) or release (ALU_ADD) stack.
static struct AsmInstr* asm_adjust_sp(enum ALUOp op, int bytes) {
  return asm_binary(op, reg_operand(SP, &kWordType), reg_operand(SP, &kWordType),
                    lit_operand(bytes, &kWordType));
}

static struct AsmInstr* asm_get_address(struct Operand* dst, struct Operand* src) {
  struct AsmInstr* instr = new_asm_instr(ASM_GET_ADDRESS);
  instr->instr.asm_get_address.dst = dst;
  instr->instr.asm_get_address.src = src;
  return instr;
}

// Load through a pointer; is_volatile selects VolatileLoad so later passes
// keep the access.
static struct AsmInstr* asm_load(bool is_volatile, struct Operand* dst, struct Operand* ptr, int offset) {
  struct AsmInstr* instr = new_asm_instr(is_volatile ? ASM_VOLATILE_LOAD : ASM_LOAD);
  if (is_volatile) {
    instr->instr.asm_volatile_load.dst = dst;
    instr->instr.asm_volatile_load.src = ptr;
    instr->instr.asm_volatile_load.offset = offset;
  } else {
    instr->instr.asm_load.dst = dst;
    instr->instr.asm_load.src = ptr;
    instr->instr.asm_load.offset = offset;
  }
  return instr;
}

// Store through a pointer; is_volatile selects VolatileStore.
static struct AsmInstr* asm_store(bool is_volatile, struct Operand* ptr, struct Operand* src, int offset) {
  struct AsmInstr* instr = new_asm_instr(is_volatile ? ASM_VOLATILE_STORE : ASM_STORE);
  if (is_volatile) {
    instr->instr.asm_volatile_store.dst = ptr;
    instr->instr.asm_volatile_store.src = src;
    instr->instr.asm_volatile_store.offset = offset;
  } else {
    instr->instr.asm_store.dst = ptr;
    instr->instr.asm_store.src = src;
    instr->instr.asm_store.offset = offset;
  }
  return instr;
}

// A copy that must stay a separate access: VolatileRead when the source is
// volatile, VolatileWrite when the destination is.
static struct AsmInstr* asm_volatile_copy(bool is_write, struct Operand* dst, struct Operand* src) {
  struct AsmInstr* instr = new_asm_instr(is_write ? ASM_VOLATILE_WRITE : ASM_VOLATILE_READ);
  if (is_write) {
    instr->instr.asm_volatile_write.dst = dst;
    instr->instr.asm_volatile_write.src = src;
  } else {
    instr->instr.asm_volatile_read.dst = dst;
    instr->instr.asm_volatile_read.src = src;
  }
  return instr;
}

static struct AsmInstr* asm_label_instr(struct Slice* label) {
  struct AsmInstr* instr = new_asm_instr(ASM_LABEL);
  instr->instr.asm_label.label = label;
  return instr;
}

// Direct (label) or indirect (operand) call, optionally as a tail call.
static struct AsmInstr* asm_call(struct Slice* label, struct Operand* target, bool is_tail) {
  struct AsmInstr* instr;
  if (label != NULL) {
    instr = new_asm_instr(is_tail ? ASM_TAIL_CALL : ASM_CALL);
    if (is_tail) {
      instr->instr.asm_tail_call.label = label;
    } else {
      instr->instr.asm_call.label = label;
    }
  } else {
    instr = new_asm_instr(is_tail ? ASM_TAIL_CALL_INDIRECT : ASM_INDIRECT_CALL);
    if (is_tail) {
      instr->instr.asm_tail_call_indirect.src = target;
    } else {
      instr->instr.asm_indirect_call.src = target;
    }
  }
  return instr;
}

// Append an operand to an OperandList under construction.
static void operand_list_append(struct OperandList** head, struct OperandList** tail,
                                struct Operand* opr) {
  struct OperandList* entry = arena_alloc(sizeof(struct OperandList));
  entry->opr = opr;
  entry->next = NULL;
  if (*head == NULL) {
    *head = entry;
  } else {
    (*tail)->next = entry;
  }
  *tail = entry;
}

// Forward declaration for aggregate classification helpers.
static struct VarClassList* classify_struct(struct StructEntry* struct_entry);
static struct AsmType* get_fourbyte_type(size_t offset, size_t struct_size);

// Type of the widest move (word, then double, then byte) that fits in the
// `remaining` bytes starting `offset` bytes into a copy, given both sides'
// base alignments.
static struct AsmType* copy_chunk_type(size_t offset, size_t remaining,
                                       size_t src_alignment, size_t dst_alignment) {
  const size_t kWordBytes = 4;
  const size_t kDoubleBytes = 2;
  if (remaining >= kWordBytes && offset % kWordBytes == 0 &&
      src_alignment >= kWordBytes && dst_alignment >= kWordBytes) {
    return &kWordType;
  }
  if (remaining >= kDoubleBytes && offset % kDoubleBytes == 0 &&
      src_alignment >= kDoubleBytes && dst_alignment >= kDoubleBytes) {
    return &kDoubleType;
  }
  return &kByteType;
}

// Copy `size` bytes from src to dst with the widest moves both sides' alignment
// allows (word, then double, then byte).
struct AsmInstr* copy_bytes(struct Slice* func_name, struct Operand* src, struct Operand* dst, size_t size){
  if (src == NULL || dst == NULL) {
    asm_gen_error("copy-bytes", func_name, "NULL operand for byte copy");
  }
  size_t src_alignment = operand_base_alignment(src);
  size_t dst_alignment = operand_base_alignment(dst);

  struct AsmList out = { NULL, NULL };
  for (size_t offset = 0; offset < size; ){
    struct AsmType* chunk_type = copy_chunk_type(offset, size - offset, src_alignment, dst_alignment);
    asm_emit(&out, asm_mov(add_offset_typed(dst, (int)offset, chunk_type),
                           add_offset_typed(src, (int)offset, chunk_type)));
    offset += asm_type_size(chunk_type);
  }
  return out.head;
}

// Type of a single register move of 4, 2, or 1 bytes.
static struct AsmType* reg_chunk_type(struct Slice* func_name, size_t size) {
  switch (size) {
    case 4: return &kWordType;
    case 2: return &kDoubleType;
    case 1: return &kByteType;
    default:
      asm_gen_error("copy-bytes", func_name,
                    "unsupported register copy size %zu; expected 1, 2, or 4", size);
      return NULL;
  }
}

// Move `size` bytes (1..4) of memory into register dst_reg.
// An under-aligned source is assembled byte-wise in a zeroed word temp. A
// 3-byte value with 2-byte alignment is packed from a 16-bit and an 8-bit load;
// both are masked because narrow loads sign-extend. Clobbers kScratchRegA.
struct AsmInstr* copy_bytes_to_reg(struct Slice* func_name, struct Operand* src, enum Reg dst_reg, size_t size){
  if (size == 0) {
    return NULL;
  }
  size_t src_alignment = operand_base_alignment(src);
  bool allow_word = src_alignment >= 4;
  bool allow_double = src_alignment >= 2;
  struct AsmList out = { NULL, NULL };

  if ((size == 4 && !allow_word) || (size == 2 && !allow_double) || (size == 3 && !allow_double)) {
    struct Operand* temp = make_asm_temp(func_name, &kWordType);
    asm_emit(&out, asm_mov(temp, lit_operand(0, &kWordType)));
    asm_emit(&out, copy_bytes(func_name, src, temp, size));
    asm_emit(&out, asm_mov(reg_operand(dst_reg, &kWordType), temp));
    return out.head;
  }

  if (size == 3) {
    struct Operand* dst = reg_operand(dst_reg, &kWordType);
    struct Operand* tmp = reg_operand(kScratchRegA, &kWordType);
    asm_emit(&out, asm_mov(dst, add_offset_typed(src, 0, &kDoubleType)));
    asm_emit(&out, asm_binary(ALU_AND, dst, dst, lit_operand(0xFFFF, &kWordType)));
    asm_emit(&out, asm_mov(tmp, add_offset_typed(src, 2, &kByteType)));
    asm_emit(&out, asm_binary(ALU_AND, tmp, tmp, lit_operand(0xFF, &kWordType)));
    asm_emit(&out, asm_binary(ALU_LSL, tmp, tmp, lit_operand(16, &kWordType)));
    asm_emit(&out, asm_binary(ALU_OR, dst, dst, tmp));
    return out.head;
  }

  struct AsmType* chunk_type = reg_chunk_type(func_name, size);
  return asm_mov(reg_operand(dst_reg, chunk_type), add_offset_typed(src, 0, chunk_type));
}

// Move the low `size` bytes (1..4) of register src_reg into memory; the
// mirror of copy_bytes_to_reg. Clobbers kScratchRegA for 3-byte values.
struct AsmInstr* copy_bytes_from_reg(struct Slice* func_name, enum Reg src_reg, struct Operand* dst, size_t size){
  if (size == 0) {
    return NULL;
  }
  size_t dst_alignment = operand_base_alignment(dst);
  bool allow_word = dst_alignment >= 4;
  bool allow_double = dst_alignment >= 2;
  struct AsmList out = { NULL, NULL };

  if ((size == 4 && !allow_word) || (size == 2 && !allow_double) || (size == 3 && !allow_double)) {
    struct Operand* temp = make_asm_temp(func_name, &kWordType);
    asm_emit(&out, asm_mov(temp, reg_operand(src_reg, &kWordType)));
    asm_emit(&out, copy_bytes(func_name, temp, dst, size));
    return out.head;
  }

  if (size == 3) {
    struct Operand* src = reg_operand(src_reg, &kWordType);
    struct Operand* tmp = reg_operand(kScratchRegA, &kWordType);
    asm_emit(&out, asm_mov(add_offset_typed(dst, 0, &kDoubleType), src));
    asm_emit(&out, asm_mov(tmp, src));
    // Clear the upper byte first so the logical shift leaves only byte 2.
    asm_emit(&out, asm_binary(ALU_AND, tmp, tmp, lit_operand(0x00FFFFFF, &kWordType)));
    asm_emit(&out, asm_binary(ALU_LSR, tmp, tmp, lit_operand(16, &kWordType)));
    asm_emit(&out, asm_mov(add_offset_typed(dst, 2, &kByteType), tmp));
    return out.head;
  }

  struct AsmType* chunk_type = reg_chunk_type(func_name, size);
  return asm_mov(add_offset_typed(dst, 0, chunk_type), reg_operand(src_reg, chunk_type));
}

// Convert a high-level symbol table to an assembly-level symbol table.
struct AsmSymbolTable* convert_symbol_table(struct SymbolTable* symbols){
  struct AsmSymbolTable* asm_table = create_asm_symbol_table(symbols->map.bucket_count);

  struct SliceMapIter entries = slice_map_iter(&symbols->map);
  for (struct SymbolEntry* cur = slice_map_next_value(&entries); cur != NULL;
       cur = slice_map_next_value(&entries)) {
    struct AsmType* asm_type = type_to_asm_type(cur->type);
    bool is_static = cur->attrs != NULL &&
                     (cur->attrs->attr_type == STATIC_ATTR ||
                      cur->attrs->attr_type == CONST_ATTR);
    bool is_defined = cur->attrs != NULL && cur->attrs->is_defined;
    bool return_on_stack = false;
    if (cur->type != NULL && cur->type->type == FUN_TYPE) {
      struct Type* ret_type = cur->type->type_data.fun_type.return_type;
      if (ret_type != NULL &&
          (ret_type->type == STRUCT_TYPE || ret_type->type == UNION_TYPE)) {
        struct TypeEntry* entry = type_table_get(global_type_table,
          ret_type->type == STRUCT_TYPE ? ret_type->type_data.struct_type.name
                                        : ret_type->type_data.union_type.name);
        if (entry == NULL) {
          asm_gen_error("symbol table", cur->key,
                        "missing type entry for aggregate return");
        }
        struct StructEntry* agg_entry =
            (entry->type == STRUCT_ENTRY) ? entry->data.struct_entry
                                          : entry->data.union_entry;
        struct VarClassList* classes = classify_struct(agg_entry);
        return_on_stack = (classes->var_class == MEMORY_CLASS);
      }
    }
    
    asm_symbol_table_insert(asm_table, cur->key, asm_type, is_static, is_defined, return_on_stack);
  }
  
  return asm_table;
}

// Check whether a slice contains the compiler temp marker.
// Returns true if the marker appears in the slice.
// name->start may not be NUL-terminated.
static bool slice_contains_temp_marker(const struct Slice* name) {
  if (name == NULL || name->start == NULL || name->len < kTempMarkerLen) {
    return false;
  }
  for (size_t i = 0; i + kTempMarkerLen <= name->len; i++) {
    bool match = true;
    for (size_t j = 0; j < kTempMarkerLen; j++) {
      if (name->start[i + j] != kTempMarker[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

// Append a debug local entry into a list sorted by stack offset.
static void insert_debug_local_sorted(struct DebugLocal** head, struct DebugLocal* entry) {
  if (head == NULL || entry == NULL) {
    return;
  }
  if (*head == NULL || entry->offset < (*head)->offset) {
    entry->next = *head;
    *head = entry;
    return;
  }
  struct DebugLocal* cur = *head;
  while (cur->next != NULL && cur->next->offset <= entry->offset) {
    cur = cur->next;
  }
  entry->next = cur->next;
  cur->next = entry;
}

// Collect stack-local debug metadata from a pseudo map.
// Returns a sorted list of locals and sets out_count.
static struct DebugLocal* collect_debug_locals(const struct PseudoMap* map, size_t* out_count) {
  if (out_count != NULL) {
    *out_count = 0;
  }
  if (map == NULL) {
    return NULL;
  }
  struct DebugLocal* head = NULL;
  struct SliceMapIter entries = slice_map_iter(&map->map);
  for (struct PseudoEntry* entry = slice_map_next_value(&entries); entry != NULL;
       entry = slice_map_next_value(&entries)) {
    if (entry->pseudo == NULL || entry->mapped == NULL) {
      continue;
    }
    struct Slice* name = operand_symbol_name(entry->pseudo);
    if (name == NULL || slice_contains_temp_marker(name)) {
      continue;
    }
    if (entry->mapped->type != OPERAND_MEMORY || entry->mapped->op.memory.base != BP) {
      continue;
    }
    struct DebugLocal* local = arena_alloc(sizeof(struct DebugLocal));
    local->name = name;
    local->offset = entry->mapped->op.memory.offset;
    local->size = asm_type_size(entry->mapped->asm_type); // store size in bytes
    local->next = NULL;
    insert_debug_local_sorted(&head, local);
    if (out_count != NULL) {
      (*out_count)++;
    }
  }
  return head;
}

// Detect whether a function body contains debug markers.
// Returns true if at least one debug boundary is present.
// instrs may be NULL for empty bodies.
static bool asm_has_debug_markers(const struct AsmInstr* instrs) {
  for (const struct AsmInstr* cur = instrs; cur != NULL; cur = cur->next) {
    if (cur->type == ASM_BOUNDARY) {
      return true;
    }
  }
  return false;
}

// Print a slice to stderr for error reporting.
// slice may be NULL; otherwise points to a valid slice.
// slice->start may be non-null-terminated.
static void asm_gen_fprint_slice(const struct Slice* slice) {
  if (slice == NULL || slice->start == NULL) {
    fputs("<null>", stderr);
    return;
  }
  fprintf(stderr, "%.*s", (int)slice->len, slice->start);
}

// Emit a formatted asm_gen error with optional context and exit.
// operation labels the failing step; func_name may be NULL.
// Writes an actionable error message to stderr and exits.
void asm_gen_error(const char* operation,
                        const struct Slice* func_name,
                        const char* fmt,
                        ...) {
  fprintf(stderr, "ASM generation error");
  if (operation != NULL) {
    fprintf(stderr, " (%s)", operation);
  }
  if (func_name != NULL) {
    fprintf(stderr, " in ");
    asm_gen_fprint_slice(func_name);
  }
  fprintf(stderr, ": ");
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc('\n', stderr);
  exit(BCC_EXIT_INTERNAL);
}

// Name a TAC instruction type for diagnostics.
// Returns a string literal describing the TAC opcode.
static const char* tac_instr_name(enum TACInstrType type) {
  switch (type) {
    case TACRETURN:
      return "TACRETURN";
    case TACUNARY:
      return "TACUNARY";
    case TACBINARY:
      return "TACBINARY";
    case TACCOND_JUMP:
      return "TACCOND_JUMP";
    case TACJUMP:
      return "TACJUMP";
    case TACLABEL:
      return "TACLABEL";
    case TACCOPY:
      return "TACCOPY";
    case TACVOLATILE_READ:
      return "TACVOLATILE_READ";
    case TACVOLATILE_WRITE:
      return "TACVOLATILE_WRITE";
    case TACVOLATILE_LOAD:
      return "TACVOLATILE_LOAD";
    case TACVOLATILE_STORE:
      return "TACVOLATILE_STORE";
    case TACVOLATILE_COPY_TO_OFFSET:
      return "TACVOLATILE_COPY_TO_OFFSET";
    case TACVOLATILE_COPY_FROM_OFFSET:
      return "TACVOLATILE_COPY_FROM_OFFSET";
    case TACCALL:
      return "TACCALL";
    case TACCALL_INDIRECT:
      return "TACCALL_INDIRECT";
    case TACTAIL_CALL:
      return "TACTAIL_CALL";
    case TACTAIL_CALL_INDIRECT:
      return "TACTAIL_CALL_INDIRECT";
    case TACGET_ADDRESS:
      return "TACGET_ADDRESS";
    case TACLOAD:
      return "TACLOAD";
    case TACSTORE:
      return "TACSTORE";
    case TACCOPY_TO_OFFSET:
      return "TACCOPY_TO_OFFSET";
    default:
      return "TAC<unknown>";
  }
}

// Append a top-level ASM node to the program list.
static void append_asm_top_level(struct AsmProg* prog, struct AsmTopLevel* node) {
  if (prog == NULL || node == NULL) {
    asm_gen_error("top-level", NULL, "append requested with NULL program or node");
  }

  if (prog->head == NULL) {
    prog->head = node;
    prog->tail = node;
    return;
  }

  prog->tail->next = node;
  prog->tail = node;
}

// A `.section` directive when sections are emitted, otherwise a word alignment
// directive so each group still starts aligned.
static struct AsmTopLevel* section_or_align(bool emit_sections, struct Slice* section) {
  struct AsmTopLevel* top = arena_alloc(sizeof(struct AsmTopLevel));
  memset(top, 0, sizeof(*top));
  if (emit_sections) {
    top->type = ASM_SECTION;
    top->top.asm_section.name = section;
  } else {
    top->type = ASM_ALIGN;
    top->top.asm_align.alignment = 4;
  }
  return top;
}

// One 4-byte piece of an aggregate passed or returned in registers: a
// pseudo-mem at `offset` typed by how many bytes of the aggregate it covers.
static struct Operand* fourbyte_piece(struct Slice* name, size_t offset, size_t aggregate_size) {
  return make_pseudo_mem(name, get_fourbyte_type(offset, aggregate_size), (int)offset);
}

// Lower a TAC program into the ASM IR representation.
// tac_prog is the TAC program to lower (must be non-NULL);
//         emit_sections controls whether .data/.text directives are emitted.
// Returns a newly allocated ASM program rooted in arena storage.
struct AsmProg* prog_to_asm(struct TACProg* tac_prog, bool emit_sections) {
  if (tac_prog == NULL) {
    asm_gen_error("program", NULL, "input TAC program is NULL");
  }

  asm_symbol_table = convert_symbol_table(global_symbol_table);

  struct AsmProg* asm_prog = arena_alloc(sizeof(struct AsmProg));
  asm_prog->head = NULL;
  asm_prog->tail = NULL;

  append_asm_top_level(asm_prog, section_or_align(emit_sections, &data_directive_slice));

  for (struct TopLevel* tac_top = tac_prog->statics; tac_top != NULL; tac_top = tac_top->next) {
    struct AsmTopLevel* asm_top = top_level_to_asm(tac_top);
    if (asm_top == NULL) {
      asm_gen_error("top-level", NULL, "failed to lower TAC static");
    }
    append_asm_top_level(asm_prog, asm_top);
  }

  append_asm_top_level(asm_prog, section_or_align(emit_sections, &text_directive_slice));

  for (struct TopLevel* tac_top = tac_prog->head; tac_top != NULL; tac_top = tac_top->next) {
      
    struct AsmTopLevel* asm_top = top_level_to_asm(tac_top);
    if (asm_top == NULL) {
      asm_gen_error("top-level", NULL, "failed to lower TAC top-level");
    }
    append_asm_top_level(asm_prog, asm_top);
  }

  return asm_prog;
}

// Report whether func_name returns its value through a caller-provided buffer
// whose address arrives in R1 and is spilled to BP-4 by the prologue.
static bool func_returns_in_memory(struct Slice* func_name) {
  struct SymbolEntry* sym_entry = symbol_table_get(global_symbol_table, func_name);
  if (sym_entry == NULL || sym_entry->type == NULL || sym_entry->type->type != FUN_TYPE) {
    asm_gen_error("top-level", func_name,
                  "missing function type information for %.*s",
                  (int)func_name->len, func_name->start);
  }
  bool return_in_memory = false;
  struct Type* ret_type = sym_entry->type->type_data.fun_type.return_type;
  if (ret_type != NULL && ret_type->type != VOID_TYPE) {
    struct Val ret_val;
    ret_val.val_type = VARIABLE;
    ret_val.val.var_name = func_name;
    ret_val.type = ret_type;
    struct OperandList* ignored = NULL;
    classify_return_val(&ret_val, &ignored, &return_in_memory);
  }
  return return_in_memory;
}

// Return true if codegen lowers op to a call to a runtime builtin (smul, sdiv,
// umod, ...) because Dioptase has no single instruction for it. Must
// agree with emit_binary_op in codegen.c.
static bool alu_op_needs_builtin_call(enum ALUOp op) {
  switch (op) {
    case ALU_SMUL:
    case ALU_SDIV:
    case ALU_SMOD:
    case ALU_UMUL:
    case ALU_UDIV:
    case ALU_UMOD:
      return true;
    case ALU_ADD:
    case ALU_SUB:
    case ALU_AND:
    case ALU_OR:
    case ALU_XOR:
    case ALU_MOV:
    case ALU_LSL:
    case ALU_LSR:
    case ALU_ASL:
    case ALU_ASR:
      return false;
  }
  // Unknown ops are reported by codegen; assume a call so callers stay conservative.
  return true;
}

// Return true if the asm body contains any instruction that codegen lowers to a
// `call`: explicit calls, tail calls (which may be demoted to calls), and ALU
// ops implemented by builtins (multiply, divide, modulo).
static bool asm_body_makes_calls(const struct AsmInstr* body) {
  for (const struct AsmInstr* instr = body; instr != NULL; instr = instr->next) {
    switch (instr->type) {
      case ASM_CALL:
      case ASM_INDIRECT_CALL:
      case ASM_TAIL_CALL:
      case ASM_TAIL_CALL_INDIRECT:
        return true;
      case ASM_BINARY:
        if (alu_op_needs_builtin_call(instr->instr.asm_binary.alu_op)) {
          return true;
        }
        break;
      default:
        break;
    }
  }
  return false;
}

// Return true if body takes the address of any frame-allocated variable
// (local, parameter, or temporary).
static bool body_takes_frame_address(struct TACInstr* body) {
  for (struct TACInstr* instr = body; instr != NULL; instr = instr->next) {
    if (instr->type != TACGET_ADDRESS) {
      continue;
    }
    struct Val* src = instr->instr.tac_get_address.src;
    if (src == NULL || src->val_type != VARIABLE) {
      continue;
    }
    struct SymbolEntry* entry = symbol_table_get(global_symbol_table, src->val.var_name);
    if (entry != NULL && entry->attrs != NULL && entry->attrs->attr_type == LOCAL_ATTR) {
      return true;
    }
  }
  return false;
}

// Lower a direct or indirect call, or a tail call, made from func_name.
// Exactly one of callee_label (direct) or callee_ptr (indirect) must be non-NULL.
//
// ASM:
// Mov R1, &dst (or R1, [BP-4] for tail calls) if the result is returned in memory
// Mov reg args into R1..R8
// Push stack args (reverse order)
// Call func
// Binary Add SP, SP, stack_bytes
// Mov dst, R1 (if dst != NULL)
//
// Tail calls have no dst: the callee's result is left in R1/R2 (or written
// through our forwarded return buffer) and returned unchanged. A tail call is
// lowered as
// Mov R1, [BP-4] (if the result is returned in memory)
// Mov reg args into R1..R8
// TailCall func
// and codegen is responsible for tearing down this frame before jumping.
// It falls back to Call followed by Ret when any argument goes on the stack
// (that would require overwriting our own incoming args, which is not
// supported yet) or when the function takes the address of a frame variable.
static struct AsmInstr* call_to_asm(struct Slice* func_name,
                                    struct Slice* callee_label,
                                    struct Val* callee_ptr,
                                    struct Val* dst,
                                    struct Val* args,
                                    size_t num_args,
                                    bool is_tail_call) {
  if ((callee_label == NULL) == (callee_ptr == NULL)) {
    asm_gen_error("call", func_name,
                  "expected exactly one of a callee label or callee pointer, got %s",
                  callee_label == NULL ? "neither" : "both");
  }

  struct AsmList out = { NULL, NULL };

  bool return_in_memory = false;
  struct OperandList* dests = NULL;
  size_t reg_index = 0;

  if (dst != NULL) {
    classify_return_val(dst, &dests, &return_in_memory);
  } else if (is_tail_call) {
    // The typechecker only leaves a bare call in a return statement when
    // its type matches ours, so the callee shares our return convention.
    return_in_memory = func_returns_in_memory(func_name);
  }

  if (return_in_memory && is_tail_call) {
    // Forward our own return buffer pointer (spilled at BP-4) to the callee.
    asm_emit(&out, asm_mov(reg_operand(R1, &kWordType), make_asm_mem(BP, -4, &kWordType)));
    reg_index = 1; // R1 holds the return buffer address
  } else if (return_in_memory) {
    asm_emit(&out, asm_get_address(reg_operand(R1, &kWordType), tac_val_to_asm(dst)));
    reg_index = 1; // R1 holds the return buffer address
  }

  struct OperandList* reg_args = NULL;
  struct OperandList* stack_args = NULL;
  classify_params(args, num_args, return_in_memory, &reg_args, &stack_args);

  // A real tail call frees our frame before the callee runs, so it is unsound
  // if the callee might still dereference a pointer into that frame
  // (e.g. `return g(&local)`, or a pointer stashed in a global earlier).
  bool emit_tail_call = is_tail_call && stack_args == NULL && !frame_address_taken;

  // Register arguments go to R1..R8 in order.
  for (struct OperandList* reg_arg_iter = reg_args; reg_arg_iter != NULL; reg_arg_iter = reg_arg_iter->next) {
    struct Operand* arg = reg_arg_iter->opr;
    enum Reg arg_reg = (enum Reg)(R1 + reg_index);
    size_t arg_size = asm_type_size(arg->asm_type);
    bool needs_byte_copy = arg->asm_type->type == BYTE_ARRAY ||
                           operand_base_alignment(arg) < arg_size;
    if (needs_byte_copy) {
      asm_emit(&out, copy_bytes_to_reg(func_name, arg, arg_reg, arg_size));
    } else {
      asm_emit(&out, asm_mov(reg_operand(arg_reg, arg->asm_type), arg));
    }
    reg_index++;
  }

  // Stack arguments are pushed last-to-first; each occupies a full 4-byte slot
  // per the ABI so SP stays aligned.
  size_t stack_bytes = 0;
  size_t stack_arg_count = operand_list_length(stack_args);
  for (size_t idx = stack_arg_count; idx > 0; ) {
    idx--;
    struct Operand* arg = operand_list_get(stack_args, idx);
    size_t copy_size = asm_type_size(arg->asm_type);
    bool needs_byte_copy = arg->asm_type->type == BYTE_ARRAY ||
                           copy_size < kStackSlotBytes ||
                           operand_base_alignment(arg) < copy_size;
    if (needs_byte_copy) {
      if (copy_size > kStackSlotBytes) {
        asm_gen_error("call", func_name,
                      "stack arg chunk exceeds %zu-byte slot (size=%zu)",
                      kStackSlotBytes, copy_size);
      }
      asm_emit(&out, asm_adjust_sp(ALU_SUB, (int)kStackSlotBytes));
      asm_emit(&out, copy_bytes(func_name, arg, (struct Operand*)&kStackMem, copy_size));
    } else {
      struct AsmInstr* push = new_asm_instr(ASM_PUSH);
      push->instr.asm_push.src = arg;
      asm_emit(&out, push);
    }
    stack_bytes += kStackSlotBytes;
  }

  struct Operand* target = callee_ptr != NULL ? tac_val_to_asm(callee_ptr) : NULL;
  if (emit_tail_call) {
    // Codegen tears down this frame and jumps; nothing follows in this function.
    asm_emit(&out, asm_call(callee_label, target, true));
    return out.head;
  }

  asm_emit(&out, asm_call(callee_label, target, false));
  if (stack_bytes > 0) {
    asm_emit(&out, asm_adjust_sp(ALU_ADD, (int)stack_bytes));
  }

  // Retrieve a register-returned value from R1 (and R2).
  if (dst != NULL && !return_in_memory) {
    size_t ret_reg_index = 0;
    for (struct OperandList* dest_iter = dests; dest_iter != NULL; dest_iter = dest_iter->next) {
      struct Operand* dest = dest_iter->opr;
      enum Reg ret_reg = (enum Reg)(R1 + ret_reg_index);
      size_t dest_size = asm_type_size(dest->asm_type);
      bool needs_byte_copy = dest->asm_type->type == BYTE_ARRAY ||
                             operand_base_alignment(dest) < dest_size;
      if (needs_byte_copy) {
        asm_emit(&out, copy_bytes_from_reg(func_name, ret_reg, dest, dest_size));
      } else {
        asm_emit(&out, asm_mov(dest, reg_operand(ret_reg, dest->asm_type)));
      }
      ret_reg_index++;
    }
  }

  if (is_tail_call) {
    asm_emit(&out, new_asm_instr(ASM_RET));
  }
  return out.head;
}

// Convert a TAC top-level structure to an assembly-level top-level structure.
struct AsmTopLevel* top_level_to_asm(struct TopLevel* tac_top) {
  if (tac_top == NULL) {
    asm_gen_error("top-level", NULL, "NULL TAC top-level encountered");
  }

  struct AsmTopLevel* asm_top = arena_alloc(sizeof(struct AsmTopLevel));
  asm_top->next = NULL;
  
  if (tac_top->type == FUNC) {
    struct TACFunc* func = &tac_top->top.tac_func;
    asm_top->type = ASM_FUNC;
    asm_top->top.asm_func.name = func->name;
    asm_top->top.asm_func.global = func->global;
    asm_top->top.asm_func.body = NULL;
    asm_top->top.asm_func.locals = NULL;
    asm_top->top.asm_func.num_locals = 0;
    asm_top->top.asm_func.reserved_stack_bytes = 0;
    asm_top->top.asm_func.frame_bytes = 0;
    asm_top->top.asm_func.uses_bp = false;

    struct AsmSymbolEntry* func_entry = asm_symbol_table_get(asm_symbol_table, func->name);
    if (func_entry == NULL) {
      asm_gen_error("top-level", func->name,
                    "function symbol not found in ASM symbol table");
    }
    bool return_in_memory = func_returns_in_memory(func->name);

    struct AsmList body = { NULL, NULL };
    // Empty when there are no params and no return buffer to spill.
    asm_emit(&body, set_up_params(func->name, func->params, func->num_params, return_in_memory));

    frame_address_taken = body_takes_frame_address(func->body.head);
    for (struct TACInstr* tac_instr = func->body.head; tac_instr != NULL; tac_instr = tac_instr->next) {
      asm_emit(&body, instr_to_asm(func->name, tac_instr));
    }

    // Pseudos stay in the body; assign_stack_slots places them after
    // register allocation. The return-buffer pointer occupies BP-4.
    asm_top->top.asm_func.body = body.head;
    asm_top->top.asm_func.makes_calls = asm_body_makes_calls(body.head);
    asm_top->top.asm_func.reserved_stack_bytes = return_in_memory ? kStackSlotBytes : 0;
    return asm_top;
  } else if (tac_top->type == STATIC_VAR) {
    struct TACStaticVar* static_var = &tac_top->top.tac_static_var;
    asm_top->type = ASM_STATIC_VAR;
    asm_top->top.asm_static_var.name = static_var->name;
    asm_top->top.asm_static_var.global = static_var->global;

    asm_top->top.asm_static_var.alignment = type_alignment(static_var->var_type, static_var->name);
    asm_top->top.asm_static_var.init_values = static_var->init_values;

    return asm_top;
  } else if (tac_top->type == STATIC_CONST) {
    struct TACStaticConst* static_const = &tac_top->top.tac_static_const;
    asm_top->type = ASM_STATIC_CONST;
    asm_top->top.asm_static_const.name = static_const->name;
    asm_top->top.asm_static_const.global = static_const->global;

    asm_top->top.asm_static_const.alignment = type_alignment(static_const->var_type, static_const->name);
    asm_top->top.asm_static_const.init_values = static_const->init_values;

    return asm_top;
  } else {
    asm_gen_error("top-level", NULL,
                  "unknown top-level type %d", (int)tac_top->type);
    return NULL;
  }
}

// Byte-wise copy of an aggregate between two operands of the given C type.
static struct AsmInstr* copy_aggregate(struct Slice* func_name, struct Operand* src,
                                       struct Operand* dst, struct Type* type) {
  return copy_bytes(func_name, src, dst, asm_type_size(type_to_asm_type(type)));
}

// Copy an aggregate of `type` between `other` and the object at ptr + offset
// (into other when is_load, out of it otherwise), one Load or Store per chunk.
// The pointer stays an ordinary operand that each chunk re-reads, instead of
// being pinned in a scratch register for the whole copy: with the pointer
// pinned, a chunk whose other side sits at a far frame offset needs the
// pointer, the value, and an address temporary live at once, more than
// codegen's two scratch registers.
static struct AsmInstr* copy_aggregate_through_pointer(struct Slice* func_name, struct Operand* ptr,
                                                       int offset, struct Operand* other,
                                                       struct Type* type, bool is_load) {
  struct AsmType* asm_type = type_to_asm_type(type);
  size_t size = asm_type_size(asm_type);
  // The object at ptr + offset has this type, so it is aligned for it.
  size_t ptr_alignment = asm_type_alignment(asm_type);
  size_t other_alignment = operand_base_alignment(other);

  struct AsmList out = { NULL, NULL };
  for (size_t chunk_offset = 0; chunk_offset < size; ) {
    struct AsmType* chunk_type =
        copy_chunk_type(chunk_offset, size - chunk_offset, ptr_alignment, other_alignment);
    struct Operand* other_chunk = add_offset_typed(other, (int)chunk_offset, chunk_type);
    int ptr_offset = offset + (int)chunk_offset;
    asm_emit(&out, is_load ? asm_load(false, other_chunk, ptr, ptr_offset)
                           : asm_store(false, ptr, other_chunk, ptr_offset));
    chunk_offset += asm_type_size(chunk_type);
  }
  return out.head;
}

// Lower a struct/union Load or Store through a pointer.
static struct AsmInstr* aggregate_through_pointer(struct Slice* func_name, struct Val* ptr, int offset,
                                                  struct Val* value, struct Type* type,
                                                  bool is_load) {
  return copy_aggregate_through_pointer(func_name, tac_val_to_asm(ptr), offset,
                                        tac_val_to_asm(value), type, is_load);
}

// Lower one TAC instruction to an ASM instruction chain.
struct AsmInstr* instr_to_asm(struct Slice* func_name, struct TACInstr* tac_instr) {
  if (tac_instr == NULL) {
    asm_gen_error("instruction", func_name, "NULL TAC instruction encountered");
  }

  switch (tac_instr->type) {
    case TACRETURN: {
      // ASM: <move the value to R1/R2, or copy it to the caller's buffer>; Ret
      struct TACReturn* ret_instr = &tac_instr->instr.tac_return;
      struct AsmList out = { NULL, NULL };
      if (ret_instr->src != NULL) {
        bool return_in_memory = false;
        struct OperandList* ret_vars = NULL;
        classify_return_val(ret_instr->src, &ret_vars, &return_in_memory);

        if (return_in_memory) {
          // The caller's buffer address was spilled to BP-4 by the prologue.
          asm_emit(&out, copy_aggregate_through_pointer(func_name, make_asm_mem(BP, -4, &kWordType), 0,
                                                        tac_val_to_asm(ret_instr->src),
                                                        ret_instr->src->type, false));
        } else {
          size_t reg_index = 0;
          for (struct OperandList* ret_iter = ret_vars; ret_iter != NULL; ret_iter = ret_iter->next) {
            struct Operand* ret_opr = ret_iter->opr;
            enum Reg ret_reg = (enum Reg)(R1 + reg_index);
            size_t ret_size = asm_type_size(ret_opr->asm_type);
            bool needs_byte_copy = ret_opr->asm_type->type == BYTE_ARRAY ||
                                   operand_base_alignment(ret_opr) < ret_size;
            if (needs_byte_copy) {
              asm_emit(&out, copy_bytes_to_reg(func_name, ret_opr, ret_reg, ret_size));
            } else {
              asm_emit(&out, asm_mov(reg_operand(ret_reg, ret_opr->asm_type), ret_opr));
            }
            reg_index++;
          }
        }
      }
      asm_emit(&out, new_asm_instr(ASM_RET));
      return out.head;
    }
    case TACCOPY:
    case TACVOLATILE_READ:
    case TACVOLATILE_WRITE: {
      // ASM: Mov dst, src (VolatileRead/VolatileWrite keep their distinct
      // opcodes); aggregates become a byte copy.
      struct TACCopy* copy_instr = &tac_instr->instr.tac_copy;
      struct Type* copy_type = copy_instr->dst->type != NULL ? copy_instr->dst->type
                                                             : copy_instr->src->type;
      if (is_aggregate_type(copy_type)) {
        return copy_aggregate(func_name, tac_val_to_asm(copy_instr->src),
                              tac_val_to_asm(copy_instr->dst), copy_type);
      }
      struct Operand* dst = tac_val_to_asm(copy_instr->dst);
      struct Operand* src = tac_val_to_asm(copy_instr->src);
      if (tac_instr->type == TACCOPY) {
        return asm_mov(dst, src);
      }
      return asm_volatile_copy(tac_instr->type == TACVOLATILE_WRITE, dst, src);
    }
    case TACUNARY: {
      struct TACUnary* unary_instr = &tac_instr->instr.tac_unary;
      struct AsmInstr* instr = new_asm_instr(ASM_UNARY);
      instr->instr.asm_unary.op = unary_instr->op;
      instr->instr.asm_unary.dst = tac_val_to_asm(unary_instr->dst);
      instr->instr.asm_unary.src = tac_val_to_asm(unary_instr->src);
      return instr;
    }
    case TACBINARY: {
      struct TACBinary* binary_instr = &tac_instr->instr.tac_binary;
      return asm_binary(binary_instr->alu_op, tac_val_to_asm(binary_instr->dst),
                        tac_val_to_asm(binary_instr->src1), tac_val_to_asm(binary_instr->src2));
    }
    case TACCOND_JUMP: {
      // ASM: Cmp src1, src2; CondJump cond, label
      struct TACCondJump* cond_jump_instr = &tac_instr->instr.tac_cond_jump;
      struct AsmInstr* cmp = new_asm_instr(ASM_CMP);
      cmp->instr.asm_cmp.src1 = tac_val_to_asm(cond_jump_instr->src1);
      cmp->instr.asm_cmp.src2 = tac_val_to_asm(cond_jump_instr->src2);
      struct AsmInstr* jump = new_asm_instr(ASM_COND_JUMP);
      jump->instr.asm_cond_jump.cond = cond_jump_instr->condition;
      jump->instr.asm_cond_jump.label = cond_jump_instr->label;
      cmp->next = jump;
      return cmp;
    }
    case TACJUMP: {
      struct AsmInstr* instr = new_asm_instr(ASM_JUMP);
      instr->instr.asm_jump.label = tac_instr->instr.tac_jump.label;
      return instr;
    }
    case TACLABEL:
      return asm_label_instr(tac_instr->instr.tac_label.label);
    case TACCALL:
    case TACTAIL_CALL: {
      // TACTailCall has the same layout as TACCall; TAC lowering fills both via tac_call.
      struct TACCall* call_instr = &tac_instr->instr.tac_call;
      return call_to_asm(func_name, call_instr->func_name, NULL, call_instr->dst,
                         call_instr->args, call_instr->num_args,
                         tac_instr->type == TACTAIL_CALL);
    }
    case TACCALL_INDIRECT:
    case TACTAIL_CALL_INDIRECT: {
      // TACTailCallIndirect has the same layout as TACCallIndirect; TAC
      // lowering fills both via tac_call_indirect.
      struct TACCallIndirect* call_instr = &tac_instr->instr.tac_call_indirect;
      return call_to_asm(func_name, NULL, call_instr->func, call_instr->dst,
                         call_instr->args, call_instr->num_args,
                         tac_instr->type == TACTAIL_CALL_INDIRECT);
    }
    case TACGET_ADDRESS: {
      struct TACGetAddress* get_addr_instr = &tac_instr->instr.tac_get_address;
      return asm_get_address(tac_val_to_asm(get_addr_instr->dst),
                             tac_val_to_asm(get_addr_instr->src));
    }
    case TACVOLATILE_LOAD:
    case TACLOAD: {
      struct TACLoad* load_instr = &tac_instr->instr.tac_load;
      struct Type* type = load_instr->dst->type;
      if (type->type == STRUCT_TYPE || type->type == UNION_TYPE) {
        return aggregate_through_pointer(func_name, load_instr->src_ptr, load_instr->offset, load_instr->dst,
                                         type, true);
      }
      return asm_load(tac_instr->type == TACVOLATILE_LOAD, tac_val_to_asm(load_instr->dst),
                      tac_val_to_asm(load_instr->src_ptr), load_instr->offset);
    }
    case TACVOLATILE_STORE:
    case TACSTORE: {
      struct TACStore* store_instr = &tac_instr->instr.tac_store;
      struct Type* type = store_instr->src->type;
      if (type->type == STRUCT_TYPE || type->type == UNION_TYPE) {
        return aggregate_through_pointer(func_name, store_instr->dst_ptr, store_instr->offset, store_instr->src,
                                         type, false);
      }
      return asm_store(tac_instr->type == TACVOLATILE_STORE, tac_val_to_asm(store_instr->dst_ptr),
                       tac_val_to_asm(store_instr->src), store_instr->offset);
    }
    case TACVOLATILE_COPY_TO_OFFSET:
    case TACCOPY_TO_OFFSET: {
      struct TACCopyToOffset* copy = &tac_instr->instr.tac_copy_to_offset;
      bool is_volatile = tac_instr->type == TACVOLATILE_COPY_TO_OFFSET;
      struct Type* store_type = copy->dst_type != NULL ? copy->dst_type : copy->src->type;
      if (store_type == NULL) {
        asm_gen_error("instruction", func_name,
                      "copy-to-offset missing type information for store");
      }
      struct AsmType* member_type = type_to_asm_type(store_type);
      if (store_type->type == STRUCT_TYPE || store_type->type == UNION_TYPE) {
        return copy_aggregate(func_name, tac_val_to_asm(copy->src),
                              make_pseudo_mem(copy->dst, member_type, copy->offset), store_type);
      }
      struct Operand* dst = make_pseudo_mem(copy->dst, member_type, copy->offset);
      struct Operand* src = tac_val_to_asm(copy->src);
      return is_volatile ? asm_volatile_copy(true, dst, src) : asm_mov(dst, src);
    }
    case TACVOLATILE_COPY_FROM_OFFSET:
    case TACCOPY_FROM_OFFSET: {
      struct TACCopyFromOffset* copy = &tac_instr->instr.tac_copy_from_offset;
      bool is_volatile = tac_instr->type == TACVOLATILE_COPY_FROM_OFFSET;
      struct Type* load_type = copy->dst->type;
      if (load_type == NULL) {
        asm_gen_error("instruction", func_name,
                      "copy-from-offset missing type information for load");
      }
      struct AsmType* member_type = type_to_asm_type(load_type);
      if (load_type->type == STRUCT_TYPE || load_type->type == UNION_TYPE) {
        return copy_aggregate(func_name, make_pseudo_mem(copy->src, member_type, copy->offset),
                              tac_val_to_asm(copy->dst), load_type);
      }
      struct Operand* dst = tac_val_to_asm(copy->dst);
      dst->asm_type = member_type;
      struct Operand* src = make_pseudo_mem(copy->src, member_type, copy->offset);
      return is_volatile ? asm_volatile_copy(false, dst, src) : asm_mov(dst, src);
    }
    case TACBOUNDARY: {
      struct AsmInstr* instr = new_asm_instr(ASM_BOUNDARY);
      instr->instr.asm_boundary.loc = tac_instr->instr.tac_boundary.loc;
      return instr;
    }
    case TACTRUNC: {
      struct AsmInstr* instr = new_asm_instr(ASM_TRUNC);
      instr->instr.asm_trunc.dst = tac_val_to_asm(tac_instr->instr.tac_trunc.dst);
      instr->instr.asm_trunc.src = tac_val_to_asm(tac_instr->instr.tac_trunc.src);
      instr->instr.asm_trunc.size = tac_instr->instr.tac_trunc.target_size;
      return instr;
    }
    case TACEXTEND: {
      struct AsmInstr* instr = new_asm_instr(ASM_EXTEND);
      instr->instr.asm_extend.dst = tac_val_to_asm(tac_instr->instr.tac_extend.dst);
      instr->instr.asm_extend.src = tac_val_to_asm(tac_instr->instr.tac_extend.src);
      instr->instr.asm_extend.size = tac_instr->instr.tac_extend.src_size;
      return instr;
    }
    default:
      asm_gen_error("instruction", func_name, "unknown TAC instruction type %d (%s)",
                    (int)tac_instr->type, tac_instr_name(tac_instr->type));
      return NULL;
  }
}

// Return true if any operand in body is a BP-relative memory operand. Must run
// after replace_pseudo so stack slots appear as Mem(BP, offset).
static bool body_uses_bp(struct AsmInstr* body) {
  for (struct AsmInstr* instr = body; instr != NULL; instr = instr->next) {
    struct OperandSlots slots = asm_operand_slots(instr);
    for (size_t i = 0; i < slots.count; i++) {
      const struct Operand* opr = *slots.slot[i].field;
      if (opr != NULL && opr->type == OPERAND_MEMORY && opr->op.memory.base == BP) {
        return true;
      }
    }
  }
  return false;
}

// Give every pseudo left in each function a home, then finish the frame:
// static symbols become Data operands and everything else gets a BP-relative
// stack slot (create_maps), debug locals are recorded when the body has line
// markers, the frame size is recorded for codegen's prologue, and pseudos are replaced
// in place. Runs after register allocation, so only unallocated pseudos take
// stack space.
void assign_stack_slots(struct AsmProg* prog) {
  for (struct AsmTopLevel* top = prog->head; top != NULL; top = top->next) {
    if (top->type != ASM_FUNC) {
      continue;
    }
    struct AsmFunc* func = &top->top.asm_func;
    size_t stack_size = create_maps(func->body, func->reserved_stack_bytes);
    if (asm_has_debug_markers(func->body)) {
      func->locals = collect_debug_locals(pseudo_map, &func->num_locals);
    }
    func->frame_bytes = stack_size;
    replace_pseudo(func->body);
    func->uses_bp = stack_size > 0 || body_uses_bp(func->body);

    // Pseudo maps are per-function.
    destroy_pseudo_map(pseudo_map);
    pseudo_map = NULL;
  }
}

// Build the register maps used to lower variables and temporaries.
size_t create_maps(struct AsmInstr* asm_instr, size_t reserved_bytes) {
  if (pseudo_map != NULL) {
    asm_gen_error("stack-map", NULL, "pseudo map already initialized");
  }

  pseudo_map = create_pseudo_map(128);
  size_t stack_bytes = reserved_bytes;

  for (struct AsmInstr* instr = asm_instr; instr != NULL; instr = instr->next) {
    struct OperandSlots slots = asm_operand_slots(instr);
    for (size_t i = 0; i < slots.count; i++) {
      struct Operand* opr = *slots.slot[i].field;
      if (opr == NULL) {
        continue;
      }

      if (opr->type != OPERAND_PSEUDO && opr->type != OPERAND_PSEUDO_MEM) {
        continue;
      }

      if (pseudo_map_get(pseudo_map, opr) != NULL) {
        continue;
      }

      struct Slice* name = operand_symbol_name(opr);
      struct AsmSymbolEntry* sym_entry = asm_symbol_table_get(asm_symbol_table, name);
      if (sym_entry == NULL) {
        asm_gen_error("stack-map", NULL,
                      "missing symbol table entry for pseudo %.*s",
                      name == NULL ? 0 : (int)name->len,
                      name == NULL ? "<null>" : name->start);
      }

      struct Operand* mapped = arena_alloc(sizeof(struct Operand));
      if (is_static_symbol_operand(opr)) {
        mapped->type = OPERAND_DATA;
        mapped->op.data.label = name;
        mapped->op.data.offset = 0;
        mapped->asm_type = sym_entry->type;
      } else {
        mapped->type = OPERAND_MEMORY;
        mapped->op.memory.base = BP;
        mapped->op.memory.offset = allocate_stack_slot(opr, &stack_bytes);
        // Offset is applied at lookup time so each pseudo-mem use keeps its own byte offset.
        mapped->asm_type = sym_entry->type;
      }

      pseudo_map_insert(pseudo_map, opr, mapped);
    }
  }

  // pad to 4-byte alignment
  size_t padding = (4 - (stack_bytes % 4)) % 4;
  stack_bytes += padding;

  return stack_bytes;
}

// Append one operand field to a slot list.
static void add_slot(struct OperandSlots* slots, struct Operand** field, enum OperandRole role) {
  slots->slot[slots->count].field = field;
  slots->slot[slots->count].role = role;
  slots->count++;
}

struct OperandSlots asm_operand_slots(struct AsmInstr* asm_instr) {
  struct OperandSlots slots = { .count = 0 };
  union AsmInstrVariant* in = &asm_instr->instr;
  switch (asm_instr->type) {
    case ASM_MOV:
      add_slot(&slots, &in->asm_mov.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_mov.src, OPERAND_USE);
      break;
    case ASM_VOLATILE_READ:
      add_slot(&slots, &in->asm_volatile_read.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_volatile_read.src, OPERAND_USE);
      break;
    case ASM_VOLATILE_WRITE:
      add_slot(&slots, &in->asm_volatile_write.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_volatile_write.src, OPERAND_USE);
      break;
    case ASM_UNARY:
      add_slot(&slots, &in->asm_unary.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_unary.src, OPERAND_USE);
      break;
    case ASM_BINARY:
      add_slot(&slots, &in->asm_binary.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_binary.src1, OPERAND_USE);
      add_slot(&slots, &in->asm_binary.src2, OPERAND_USE);
      break;
    case ASM_CMP:
      add_slot(&slots, &in->asm_cmp.src1, OPERAND_USE);
      add_slot(&slots, &in->asm_cmp.src2, OPERAND_USE);
      break;
    case ASM_PUSH:
      add_slot(&slots, &in->asm_push.src, OPERAND_USE);
      break;
    case ASM_INDIRECT_CALL:
      add_slot(&slots, &in->asm_indirect_call.src, OPERAND_USE);
      break;
    case ASM_TAIL_CALL_INDIRECT:
      add_slot(&slots, &in->asm_tail_call_indirect.src, OPERAND_USE);
      break;
    case ASM_GET_ADDRESS:
      add_slot(&slots, &in->asm_get_address.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_get_address.src, OPERAND_ADDRESS);
      break;
    case ASM_LOAD:
      add_slot(&slots, &in->asm_load.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_load.src, OPERAND_USE);
      break;
    case ASM_VOLATILE_LOAD:
      add_slot(&slots, &in->asm_volatile_load.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_volatile_load.src, OPERAND_USE);
      break;
    case ASM_STORE:
      add_slot(&slots, &in->asm_store.src, OPERAND_USE);
      add_slot(&slots, &in->asm_store.dst, OPERAND_USE);
      break;
    case ASM_VOLATILE_STORE:
      add_slot(&slots, &in->asm_volatile_store.src, OPERAND_USE);
      add_slot(&slots, &in->asm_volatile_store.dst, OPERAND_USE);
      break;
    case ASM_TRUNC:
      add_slot(&slots, &in->asm_trunc.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_trunc.src, OPERAND_USE);
      break;
    case ASM_EXTEND:
      add_slot(&slots, &in->asm_extend.dst, OPERAND_DEF);
      add_slot(&slots, &in->asm_extend.src, OPERAND_USE);
      break;
    case ASM_CALL:
    case ASM_TAIL_CALL:
    case ASM_JUMP:
    case ASM_COND_JUMP:
    case ASM_LABEL:
    case ASM_RET:
    case ASM_BOUNDARY:
      break;
  }
  return slots;
}

// Replace every pseudo operand in a body with its mapped stack or data location.
void replace_pseudo(struct AsmInstr* asm_instr) {
  for (struct AsmInstr* instr = asm_instr; instr != NULL; instr = instr->next) {
    struct OperandSlots slots = asm_operand_slots(instr);
    for (size_t i = 0; i < slots.count; i++) {
      replace_operand_if_pseudo(slots.slot[i].field);
    }
  }
}

// Return whether a pseudo operand names static storage or a function symbol.
bool is_static_symbol_operand(const struct Operand* opr) {
  struct Slice* name = operand_symbol_name(opr);
  if (opr == NULL || name == NULL || global_symbol_table == NULL) {
    return false;
  }
  struct SymbolEntry* entry = symbol_table_get(global_symbol_table, name);
  if (entry == NULL || entry->attrs == NULL) {
    return false;
  }
  if (entry->type != NULL && entry->type->type == FUN_TYPE) {
    // Function symbols live in the text segment and should be treated as static operands.
    return true;
  }
  return entry->attrs->attr_type == STATIC_ATTR ||
         entry->attrs->attr_type == CONST_ATTR;
}

// Reserve an aligned stack slot for a pseudo operand and return its BP-relative offset.
int allocate_stack_slot(struct Operand* opr, size_t* stack_bytes) {
  assert(opr != NULL);
  assert(opr->type == OPERAND_PSEUDO || opr->type == OPERAND_PSEUDO_MEM);

  struct Slice* name = operand_symbol_name(opr);
  struct AsmSymbolEntry* sym_entry = asm_symbol_table_get(asm_symbol_table, name);
  if (sym_entry == NULL) {
    asm_gen_error("stack-map", NULL,
                  "missing symbol table entry for pseudo %.*s",
                  name == NULL ? 0 : (int)name->len,
                  name == NULL ? "<null>" : name->start);
  }
  // add padding if necessary for alignment
  size_t alignment = asm_type_alignment(sym_entry->type);
  size_t padding = (alignment - (*stack_bytes % alignment)) % alignment;
  *stack_bytes += padding;

  size_t next_size = *stack_bytes + asm_type_size(sym_entry->type);
  *stack_bytes = next_size;
  return -((int)next_size);
}

// Rewrite an operand that refers to a pseudo-register.
void replace_operand_if_pseudo(struct Operand** field) {
  if (field == NULL || *field == NULL) {
    return;
  }

  if (pseudo_map == NULL) {
    asm_gen_error("stack-map", NULL, "pseudo map not initialized before replacement");
  }

  struct Operand* mapped = pseudo_map_get(pseudo_map, *field);
  if (mapped != NULL) {
    *field = mapped;
    return;
  }

  if ((*field)->type == OPERAND_PSEUDO || (*field)->type == OPERAND_PSEUDO_MEM) {
    size_t len = 0;
    const char* name = "<unknown>";
    struct Slice* ident = operand_symbol_name(*field);
    if (ident != NULL) {
        name = ident->start;
        len = ident->len;
    }
    asm_gen_error("stack-map", NULL, "missing mapping for pseudo %.*s", (int)len, name);
  }
}

// Translate a TAC value into an assembly operand.
struct Operand* tac_val_to_asm(struct Val* val) {
  if (val == NULL) {
    asm_gen_error("operand", NULL, "NULL TAC value encountered");
  }

  struct Operand* opr = arena_alloc(sizeof(struct Operand));

  switch (val->val_type) {
    case CONSTANT: {
      opr->type = OPERAND_LIT;
      opr->op.lit.value = (int)(val->val.const_value); // assuming fits in int
      opr->asm_type = type_to_asm_type(val->type);
      return opr;
    }
    case VARIABLE: {
      switch (val->type->type) {
        case CHAR_TYPE:
        case SCHAR_TYPE:
        case UCHAR_TYPE:
        case SHORT_TYPE:
        case USHORT_TYPE:
        case INT_TYPE:
        case UINT_TYPE:
        case LONG_TYPE:
        case ULONG_TYPE:
        case ENUM_TYPE:
        case POINTER_TYPE: {
          // Pseudo Operand for scalars
          opr->type = OPERAND_PSEUDO;
          opr->op.pseudo.name = val->val.var_name;
          opr->asm_type = type_to_asm_type(val->type);
          return opr;
        }
        case FUN_TYPE: {
          // Function designators are treated as pointer-sized pseudos.
          opr->type = OPERAND_PSEUDO;
          opr->op.pseudo.name = val->val.var_name;
          opr->asm_type = &kWordType;
          return opr;
        }
        case STRUCT_TYPE:
        case UNION_TYPE: 
        case ARRAY_TYPE: {
          // PseudoMem Operand for arrays, structs, and unions
          opr->type = OPERAND_PSEUDO_MEM;
          opr->op.pseudo_mem.name = val->val.var_name;
          opr->op.pseudo_mem.offset = 0; // offset 0 for now
          opr->asm_type = type_to_asm_type(val->type);
          return opr;
        }
        default:
          asm_gen_error("operand", NULL,
                        "unsupported variable type %d for TAC to ASM conversion",
                        (int)val->type->type);
          return NULL;
      }
    }
    default:
      asm_gen_error("operand", NULL, "unknown TAC value type %d", (int)val->val_type);
      return NULL;
  }
}

// Allocate a pseudo-register entry and initialize its mapping state.
struct Operand* make_pseudo(struct Slice* var_name, struct AsmType* asm_type) {
  if (var_name == NULL) {
    asm_gen_error("operand", NULL, "NULL slice for pseudo operand");
  }

  struct Operand* opr = arena_alloc(sizeof(struct Operand));
  opr->type = OPERAND_PSEUDO;
  opr->op.pseudo.name = var_name;
  opr->asm_type = asm_type;
  return opr;
}

// Allocate a pseudo-register entry representing a memory-backed value.
struct Operand* make_pseudo_mem(struct Slice* var_name, struct AsmType* asm_type, int offset) {
  if (var_name == NULL) {
    asm_gen_error("operand", NULL, "NULL slice for pseudo-mem operand");
  }

  struct Operand* opr = arena_alloc(sizeof(struct Operand));
  opr->type = OPERAND_PSEUDO_MEM;
  opr->op.pseudo_mem.name = var_name;
  opr->op.pseudo_mem.offset = offset;
  opr->asm_type = asm_type;
  return opr;
}

// Compute the alignment required by a checked C type.
size_t type_alignment(struct Type* type, const struct Slice* symbol_name) {
  // will eventually have different alignments for different types
  // short => 2, char => 1
  if (type == NULL) {
    asm_gen_error("type-alignment", symbol_name, "NULL type for static symbol");
  }
  switch (type->type) {
    case CHAR_TYPE:
    case SCHAR_TYPE:
    case UCHAR_TYPE:
      return 1;
    case SHORT_TYPE:
    case USHORT_TYPE:
      return 2;
    case INT_TYPE:
    case UINT_TYPE:
    case LONG_TYPE:
    case ULONG_TYPE:
    case POINTER_TYPE:
      return 4;
    case ENUM_TYPE:
      return 4;
    case ARRAY_TYPE:
      return type_alignment(type->type_data.array_type.element_type, symbol_name);
    case STRUCT_TYPE:
    case UNION_TYPE:
      return get_type_alignment(type);
    default:
      asm_gen_error("type-alignment", symbol_name,
                    "unknown type kind %d", (int)type->type);
      return 0;
  }
}

// Resolve the type table entry for a struct or union type.
// Returns the StructEntry for the aggregate.
static struct StructEntry* get_aggregate_entry(struct Type* type) {
  if (type == NULL) {
    asm_gen_error("aggregate", NULL, "NULL type for aggregate lookup");
  }
  struct TypeEntry* entry = NULL;
  if (type->type == STRUCT_TYPE) {
    entry = type_table_get(global_type_table, type->type_data.struct_type.name);
  } else if (type->type == UNION_TYPE) {
    entry = type_table_get(global_type_table, type->type_data.union_type.name);
  } else {
    asm_gen_error("aggregate", NULL, "non-aggregate type %d for lookup", (int)type->type);
  }
  if (entry == NULL) {
    asm_gen_error("aggregate", NULL, "missing type entry for aggregate");
  }
  return (entry->type == STRUCT_ENTRY) ? entry->data.struct_entry : entry->data.union_entry;
}

// determine how to pass a struct according to the ABI.
// could be 1 register, 2 registers, or passed in memory
struct VarClassList* classify_struct(struct StructEntry* struct_entry) {
  if (struct_entry == NULL) {
    asm_gen_error("struct-classify", NULL, "NULL struct entry");
  }
  int size = struct_entry->size;

  if (size > 8){
    // too large to fit in 2 registers, so pass in memory
    struct VarClassList* list = arena_alloc(sizeof(struct VarClassList));
    struct VarClassList* tail = list;
    list->var_class = MEMORY_CLASS;
    list->next = NULL;
    size -= 4;

    while (size > 0){
      struct VarClassList* next = arena_alloc(sizeof(struct VarClassList));
      next->var_class = MEMORY_CLASS;
      next->next = NULL;
      tail->next = next;
      tail = next;
      size -=4;
    }
    return list;
  }

  // small enough to fit in registers

  if (size > 4){
    // fits in 2 registers
    struct VarClassList* list = arena_alloc(sizeof(struct VarClassList));
    list->var_class = INTEGER_CLASS;
    list->next = arena_alloc(sizeof(struct VarClassList));
    list->next->var_class = INTEGER_CLASS;
    list->next->next = NULL;
    return list;
  }
  
  // fits in 1 register
  struct VarClassList* list = arena_alloc(sizeof(struct VarClassList));
  list->var_class = INTEGER_CLASS;
  list->next = NULL;
  return list;
}

// Get the appropriate four-byte type for a given offset within a struct
static struct AsmType* get_fourbyte_type(size_t offset, size_t struct_size){
  if (struct_size - offset >= 4){
    struct AsmType* word = arena_alloc(sizeof(struct AsmType));
    word->type = WORD;
    return word;
  } else if (struct_size - offset == 2){
    struct AsmType* double_ = arena_alloc(sizeof(struct AsmType));
    double_->type = DOUBLE;
    return double_;
  } else if (struct_size - offset == 1){
    struct AsmType* byte = arena_alloc(sizeof(struct AsmType));
    byte->type = BYTE;
    return byte;
  } else {
    // return byte for any other size (e.g., 3 bytes)
    struct AsmType* byte_array = arena_alloc(sizeof(struct AsmType));
    byte_array->type = BYTE_ARRAY;
    byte_array->byte_array.size = struct_size - offset;
    byte_array->byte_array.alignment = 1;
    return byte_array;
  }
}

// Classify function parameters into register and stack arguments according to the ABI
void classify_params(struct Val* params, size_t num_params, bool return_in_memory,
                     struct OperandList** reg_args, struct OperandList** stack_args) {
  *reg_args = NULL;
  *stack_args = NULL;

  size_t regs_available = 0;
  if (return_in_memory){
    // first reg is pointer to return value memory
    regs_available = REG_ARG_LIMIT - 1;
  } else {
    regs_available = REG_ARG_LIMIT;
  }

  struct OperandList* reg_tail = NULL;
  struct OperandList* stack_tail = NULL;

  for (size_t i = 0; i < num_params; i++){
    struct Type* param_type = params[i].type;

    struct Operand* param_opr = tac_val_to_asm(&params[i]);

    if (param_type->type == STRUCT_TYPE ||
        param_type->type == UNION_TYPE) {
      // partition struct/union into fourbytes by class
      struct StructEntry* struct_entry = get_aggregate_entry(param_type);
      struct VarClassList* class_list = classify_struct(struct_entry);

      bool use_stack = true;
      size_t struct_size = struct_entry->size;

      if (class_list->var_class != MEMORY_CLASS){
        // make tentative assignment to registers
        struct OperandList* tentative_regs = NULL;
        struct OperandList* tentative_tail = NULL;
        size_t offset = 0;
        size_t regs_needed = 0;
        for (struct VarClassList* cls = class_list; cls != NULL; cls = cls->next){
          operand_list_append(&tentative_regs, &tentative_tail,
                              fourbyte_piece(operand_symbol_name(param_opr), offset, struct_size));
          offset += 4;
          regs_needed++;
        }

        // finalize assignments if enough registers available
        if (regs_needed <= regs_available){
          use_stack = false;
          regs_available -= regs_needed;

          if (*reg_args == NULL){
            *reg_args = tentative_regs;
          } else {
            reg_tail->next = tentative_regs;
          }
          reg_tail = tentative_tail;
        }
      }

      if (use_stack){
        // assign entire struct to stack
        
        size_t offset = 0;
        for (struct VarClassList* cls = class_list; cls != NULL; cls = cls->next){
          operand_list_append(stack_args, &stack_tail,
                              fourbyte_piece(operand_symbol_name(param_opr), offset, struct_size));
          offset += 4;
        }
      }
    } else {
      // scalar type, assign to register if available, stack if not
      if (regs_available > 0){
        operand_list_append(reg_args, &reg_tail, param_opr);
        regs_available--;
      } else {
        operand_list_append(stack_args, &stack_tail, param_opr);
      }
    }
  }
}

// Classify the return value of a function according to the ABI,
// determining whether it should be returned in memory or in registers.
void classify_return_val(struct Val* ret_val, struct OperandList** ret_var_list, bool* return_in_memory) {
  if (ret_val == NULL) {
    asm_gen_error("return-classify", NULL, "NULL return value");
  }

  struct AsmType* ret_type = type_to_asm_type(ret_val->type);

  if (ret_type->type == BYTE_ARRAY) {
    // aggregate return, may or may not fit in registers
    struct StructEntry* struct_entry = get_aggregate_entry(ret_val->type);
    struct VarClassList* class_list = classify_struct(struct_entry);
    size_t size = struct_entry->size;
    
    if (class_list->var_class == MEMORY_CLASS) {
      // return in memory
      *return_in_memory = true;
      *ret_var_list = NULL;
      return;
    } else {
      // return in registers
      *return_in_memory = false;
      struct OperandList* ret_entry = NULL;
      struct OperandList* ret_tail = NULL;
      size_t offset = 0;

      for (struct VarClassList* cls = class_list; cls != NULL; cls = cls->next){
        operand_list_append(&ret_entry, &ret_tail, fourbyte_piece(ret_val->val.var_name, offset, size));
        offset += 4;
      }

      *ret_var_list = ret_entry;
      return;
    }
  } else {
    // scalar return, fits in register
    struct OperandList* ret_tail = NULL;
    *ret_var_list = NULL;
    operand_list_append(ret_var_list, &ret_tail, tac_val_to_asm(ret_val));
    *return_in_memory = false;
    return;
  }
}

// Lower parameter passing for a function using pre-built Val entries.
// Returns the head of the ASM instruction list setting up parameters.
static struct AsmInstr* set_up_params_from_vals(struct Slice* func_name,
                                                struct Val* params,
                                                size_t num_params,
                                                bool return_in_memory) {
  struct OperandList* reg_param_list = NULL;
  struct OperandList* stack_param_list = NULL;
  classify_params(params, num_params, return_in_memory, &reg_param_list, &stack_param_list);

  struct AsmList out = { NULL, NULL };
  size_t reg_index = 0;

  if (return_in_memory) {
    // The return buffer pointer arrives in R1; keep it in the first stack slot.
    asm_emit(&out, asm_mov(make_asm_mem(BP, -4, &kWordType), reg_operand(R1, &kWordType)));
    reg_index = 1;
  }

  for (struct OperandList* reg_param = reg_param_list; reg_param != NULL; reg_param = reg_param->next) {
    struct Operand* param = reg_param->opr;
    enum Reg param_reg = (enum Reg)(R1 + reg_index);
    size_t param_size = asm_type_size(param->asm_type);
    bool needs_byte_copy = param->asm_type->type == BYTE_ARRAY ||
                           operand_base_alignment(param) < param_size;
    if (needs_byte_copy) {
      asm_emit(&out, copy_bytes_from_reg(func_name, param_reg, param, param_size));
    } else {
      asm_emit(&out, asm_mov(param, reg_operand(param_reg, param->asm_type)));
    }
    reg_index++;
  }

  // Stack parameters start above the saved BP and return address.
  size_t offset = 8;
  for (struct OperandList* stack_param = stack_param_list; stack_param != NULL; stack_param = stack_param->next) {
    struct Operand* param = stack_param->opr;
    size_t param_size = asm_type_size(param->asm_type);
    bool needs_byte_copy = param->asm_type->type == BYTE_ARRAY ||
                           operand_base_alignment(param) < param_size;
    struct Operand* incoming = make_asm_mem(BP, (int)offset, param->asm_type);
    if (needs_byte_copy) {
      asm_emit(&out, copy_bytes(func_name, incoming, param, param_size));
    } else {
      asm_emit(&out, asm_mov(param, incoming));
    }
    offset += 4;
  }

  return out.head;
}

// Lower parameter passing for a function definition.
// Returns the head of the ASM instruction list setting up parameters.
struct AsmInstr* set_up_params(struct Slice* func_name,
                               struct Slice** params,
                               size_t num_params,
                               bool return_in_memory) {
  if (num_params == 0) {
    return set_up_params_from_vals(func_name, NULL, 0, return_in_memory);
  }

  struct Val* param_vals = arena_alloc(num_params * sizeof(struct Val));
  for (size_t i = 0; i < num_params; i++) {
    struct SymbolEntry* entry = symbol_table_get(global_symbol_table, params[i]);
    if (entry == NULL || entry->type == NULL) {
      asm_gen_error("params", func_name,
                    "missing type info for parameter %.*s",
                    (int)params[i]->len, params[i]->start);
    }
    param_vals[i].val_type = VARIABLE;
    param_vals[i].val.var_name = params[i];
    param_vals[i].type = entry->type;
  }

  return set_up_params_from_vals(func_name, param_vals, num_params, return_in_memory);
}

// Compute the alignment encoded by an assembly-level type.
size_t asm_type_alignment(struct AsmType* type){
  if (type == NULL) {
    asm_gen_error("asm-type-alignment", NULL, "NULL asm type");
  }
  switch (type->type) {
    case BYTE:
      return 1;
    case DOUBLE:
      return 2;
    case WORD:
    case LONG_WORD:
      return 4;
    case BYTE_ARRAY:
      return type->byte_array.alignment;
    default:
      asm_gen_error("asm-type-alignment", NULL,
                    "unknown asm type kind %d", (int)type->type);
      return 0;
  }
}

// Allocate an empty pseudo map; release it with destroy_pseudo_map.
struct PseudoMap* create_pseudo_map(size_t num_buckets){
  struct PseudoMap* hmap = malloc(sizeof(struct PseudoMap));
  if (hmap == NULL) {
    asm_gen_error("stack-map", NULL, "allocation failed for pseudo map");
  }
  slice_map_init(&hmap->map, num_buckets, true);
  return hmap;
}

// Insert or replace the location a pseudo-register maps to.
void pseudo_map_insert(struct PseudoMap* hmap, struct Operand* key, struct Operand* value){
  if (hmap == NULL || key == NULL || operand_symbol_name(key) == NULL) {
    asm_gen_error("stack-map", NULL, "invalid pseudo map insert request");
  }
  struct PseudoEntry* entry = slice_map_get(&hmap->map, operand_symbol_name(key));
  if (entry == NULL) {
    entry = arena_alloc(sizeof(struct PseudoEntry));
    entry->pseudo = key;
    slice_map_add(&hmap->map, operand_symbol_name(key), entry);
  }
  entry->mapped = value;
}

// Location of a pseudo operand, or NULL for non-pseudos and unmapped names. A
// PseudoMem key yields a derived operand at the mapped location plus its
// offset, typed by the key.
struct Operand* pseudo_map_get(struct PseudoMap* hmap, struct Operand* key){
  if (hmap == NULL || key == NULL) {
    asm_gen_error("stack-map", NULL, "invalid pseudo map lookup request");
  }
  if (key->type != OPERAND_PSEUDO && key->type != OPERAND_PSEUDO_MEM) {
    return NULL;
  }
  if (operand_symbol_name(key) == NULL) {
    asm_gen_error("stack-map", NULL, "pseudo operand missing identifier");
  }
  struct PseudoEntry* entry = slice_map_get(&hmap->map, operand_symbol_name(key));
  if (entry == NULL || entry->mapped == NULL) {
    return NULL;
  }
  struct Operand* mapped = entry->mapped;
  if (key->type != OPERAND_PSEUDO_MEM) {
    return mapped;
  }
  struct Operand* derived = arena_alloc(sizeof(struct Operand));
  *derived = *mapped;
  derived->asm_type = key->asm_type;
  if (mapped->type == OPERAND_MEMORY) {
    derived->op.memory.offset = mapped->op.memory.offset + key->op.pseudo_mem.offset;
  } else if (mapped->type == OPERAND_DATA) {
    derived->op.data.offset = mapped->op.data.offset + key->op.pseudo_mem.offset;
  }
  return derived;
}

// Return whether the pseudo map has a location for the operand's name.
bool pseudo_map_contains(struct PseudoMap* hmap, struct Operand* key){
  if (hmap == NULL || key == NULL || operand_symbol_name(key) == NULL) {
    asm_gen_error("stack-map", NULL, "invalid pseudo map contains request");
  }
  return slice_map_contains(&hmap->map, operand_symbol_name(key));
}

// Free the map's own storage; operands and entries are arena-owned.
void destroy_pseudo_map(struct PseudoMap* hmap){
  slice_map_free(&hmap->map);
  free(hmap);
}

// Allocate an empty assembly symbol table.
struct AsmSymbolTable* create_asm_symbol_table(size_t numBuckets){
  struct AsmSymbolTable* table = arena_alloc(sizeof(struct AsmSymbolTable));
  slice_map_init(&table->map, numBuckets, false);
  return table;
}

// Add a symbol's assembly metadata; like the symbol table, an existing name
// is kept and found first.
void asm_symbol_table_insert(struct AsmSymbolTable* hmap, struct Slice* key, struct AsmType* type,
    bool is_static, bool is_defined, bool return_on_stack){
  struct AsmSymbolEntry* entry = arena_alloc(sizeof(struct AsmSymbolEntry));
  entry->key = key;
  entry->type = type;
  entry->is_static = is_static;
  entry->is_defined = is_defined;
  entry->return_on_stack = return_on_stack;
  slice_map_add(&hmap->map, key, entry);
}

// Look up a symbol's assembly metadata by name.
struct AsmSymbolEntry* asm_symbol_table_get(struct AsmSymbolTable* hmap, struct Slice* key){
  return slice_map_get(&hmap->map, key);
}

// Return whether the assembly symbol table contains a name.
bool asm_symbol_table_contains(struct AsmSymbolTable* hmap, struct Slice* key){
  return slice_map_contains(&hmap->map, key);
}

// Dump each pseudo-register's stack offset and assigned assembly type.
void print_pseudo_map(struct Slice* func, struct PseudoMap* hmap){
  printf("%.*s pseudo map:\n", (int)func->len, func->start);
  struct SliceMapIter entries = slice_map_iter(&hmap->map);
  for (struct PseudoEntry* cur = slice_map_next_value(&entries); cur != NULL;
       cur = slice_map_next_value(&entries)) {
    struct Slice* name = operand_symbol_name(cur->pseudo);
    printf("  Key: %.*s\n",
      name == NULL ? 0 : (int)name->len,
      name == NULL ? "<null>" : name->start);
    printf("    BP Offset: %d\n",
      cur->mapped->type == OPERAND_MEMORY ? cur->mapped->op.memory.offset :
      cur->mapped->type == OPERAND_DATA ? cur->mapped->op.data.offset : 0);
    printf("    Type: ");
    switch (cur->mapped->asm_type->type) {
      case BYTE:
        printf("BYTE\n");
        break;
      case DOUBLE:
        printf("DOUBLE\n");
        break;
      case WORD:
        printf("WORD\n");
        break;
      case LONG_WORD:
        printf("LONG_WORD\n");
        break;
      case BYTE_ARRAY:
        printf("BYTE_ARRAY(size=%zu, alignment=%zu)\n", 
          cur->mapped->asm_type->byte_array.size, 
          cur->mapped->asm_type->byte_array.alignment);
        break;
      default:
        printf("unknown\n");
        break;
    }
  }
}

// Dump assembly symbol types, linkage, and definition state.
void print_asm_symbol_table(struct AsmSymbolTable* hmap){
  struct SliceMapIter entries = slice_map_iter(&hmap->map);
  for (struct AsmSymbolEntry* cur = slice_map_next_value(&entries); cur != NULL;
       cur = slice_map_next_value(&entries)) {
    printf("Key: %.*s\n", (int)cur->key->len, cur->key->start);
    printf("  Type: ");
    if (cur->type == NULL) {
      printf("NULL\n");
    } else {
      switch (cur->type->type){
        case BYTE:
          printf("BYTE\n");
          break;
        case DOUBLE:
          printf("DOUBLE\n");
          break;
        case WORD:
          printf("WORD\n");
          break;
        case LONG_WORD:
          printf("LONG_WORD\n");
          break;
        case BYTE_ARRAY:
          printf("BYTE_ARRAY(size=%zu, alignment=%zu)\n", cur->type->byte_array.size, cur->type->byte_array.alignment);
          break;
        default:
          printf("Unknown (%d)\n", (int)cur->type->type);
          break;
      }
    }
    printf("  Is Static: %s\n", cur->is_static ? "true" : "false");
    printf("  Is Defined: %s\n", cur->is_defined ? "true" : "false");
    printf("\n");
  }
}

// Return the byte width occupied by an assembly-level type.
size_t asm_type_size(struct AsmType* type){
  switch (type->type){
    case BYTE:
      return 1;
    case DOUBLE:
      return 2;
    case WORD:
      return 4;
    case LONG_WORD:
      return 8;
    case BYTE_ARRAY:
      return type->byte_array.size;
    default:
      asm_gen_error("asm-type-size", NULL, "unknown asm type %d", (int)type->type);
      return 0;
  }
}
