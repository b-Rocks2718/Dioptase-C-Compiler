#include "asm_gen.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>

struct PseudoMap* pseudo_map = NULL;

static struct Slice text_directive_slice = {"text", 4};
static struct Slice data_directive_slice = {"data", 4};

static const size_t STACK_SLOT_SIZE = 4; // 4 bytes per stack slot

// Purpose: Print a slice to stderr for error reporting.
// Inputs: slice may be NULL; otherwise points to a valid slice.
// Outputs: Writes a best-effort identifier representation to stderr.
// Invariants/Assumptions: slice->start may be non-null-terminated.
static void asm_gen_fprint_slice(const struct Slice* slice) {
    if (slice == NULL || slice->start == NULL) {
        fputs("<null>", stderr);
        return;
    }
    fprintf(stderr, "%.*s", (int)slice->len, slice->start);
}

// Purpose: Emit a formatted asm_gen error with optional context and exit.
// Inputs: operation labels the failing step; func_name may be NULL.
// Outputs: Writes an actionable error message to stderr and exits.
// Invariants/Assumptions: fmt is a printf-style format string.
static void asm_gen_error(const char* operation,
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
    exit(1);
}

// Purpose: Name a TAC instruction type for diagnostics.
// Inputs: type is the TAC instruction enum value.
// Outputs: Returns a string literal describing the TAC opcode.
// Invariants/Assumptions: Unknown values map to "TAC<unknown>".
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
        case TACCMP:
            return "TACCMP";
        case TACJUMP:
            return "TACJUMP";
        case TACLABEL:
            return "TACLABEL";
        case TACCOPY:
            return "TACCOPY";
        case TACCALL:
            return "TACCALL";
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

// Purpose: Detect whether a pseudo operand maps to a static storage symbol.
// Inputs: opr is the operand to classify.
// Outputs: Returns true if the operand names a static symbol.
// Invariants/Assumptions: global_symbol_table is initialized before use.
static bool is_static_symbol_operand(const struct Operand* opr);

// Purpose: Reserve space for a new stack slot in the current frame.
// Inputs: stack_bytes tracks the total allocated stack bytes.
// Outputs: Returns the negative offset from BP for the new slot.
// Invariants/Assumptions: stack_bytes is non-NULL.
static int allocate_stack_slot(size_t* stack_bytes);

// Purpose: Replace a pseudo operand field with its mapped location if present.
// Inputs: field points to an operand field that may hold a pseudo.
// Outputs: Updates *field in place when a mapping exists.
// Invariants/Assumptions: pseudo_map is initialized before use.
static void replace_operand_if_pseudo(struct Operand** field);

// Purpose: Append a top-level ASM node to the program list.
// Inputs: prog is the ASM program; node is the top-level to append.
// Outputs: Updates prog->head/tail to include node.
// Invariants/Assumptions: node->next is either NULL or a valid list tail.
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

// Purpose: Lower a TAC program into the ASM IR representation.
// Inputs: tac_prog is the TAC program to lower (must be non-NULL);
//         emit_sections controls whether .data/.text directives are emitted.
// Outputs: Returns a newly allocated ASM program rooted in arena storage.
// Invariants/Assumptions: TAC top-level lists are well-formed and acyclic.
struct AsmProg* prog_to_asm(struct TACProg* tac_prog, bool emit_sections) {
    if (tac_prog == NULL) {
        asm_gen_error("program", NULL, "input TAC program is NULL");
    }

    struct AsmProg* asm_prog = arena_alloc(sizeof(struct AsmProg));
    asm_prog->head = NULL;
    asm_prog->tail = NULL;

    if (emit_sections) {
        // emit .data
        struct AsmTopLevel* data_directive = arena_alloc(sizeof(struct AsmTopLevel));
        data_directive->type = ASM_SECTION;
        data_directive->name = &data_directive_slice;
        data_directive->next = NULL;
        append_asm_top_level(asm_prog, data_directive);
    }

    for (struct TopLevel* tac_top = tac_prog->statics; tac_top != NULL; tac_top = tac_top->next) {
        struct AsmTopLevel* asm_top = top_level_to_asm(tac_top);
        if (asm_top == NULL) {
            asm_gen_error("top-level", NULL, "failed to lower TAC static");
        }
        append_asm_top_level(asm_prog, asm_top);
    }

    if (emit_sections) {
        // emit .text
        struct AsmTopLevel* text_directive = arena_alloc(sizeof(struct AsmTopLevel));
        text_directive->type = ASM_SECTION;
        text_directive->name = &text_directive_slice;
        text_directive->next = NULL;
        append_asm_top_level(asm_prog, text_directive);
    }

    for (struct TopLevel* tac_top = tac_prog->head; tac_top != NULL; tac_top = tac_top->next) {
        
        struct AsmTopLevel* asm_top = top_level_to_asm(tac_top);
        if (asm_top == NULL) {
            asm_gen_error("top-level", NULL, "failed to lower TAC top-level");
        }
        append_asm_top_level(asm_prog, asm_top);
    }

    if (pseudo_map != NULL) {
        destroy_pseudo_map(pseudo_map);
        pseudo_map = NULL;
    }

    return asm_prog;
}

struct AsmTopLevel* top_level_to_asm(struct TopLevel* tac_top) {
    if (tac_top == NULL) {
        asm_gen_error("top-level", NULL, "NULL TAC top-level encountered");
    }

    struct AsmTopLevel* asm_top = arena_alloc(sizeof(struct AsmTopLevel));
    asm_top->next = NULL;
    
    if (tac_top->type == FUNC) {
        asm_top->type = ASM_FUNC;
        asm_top->name = tac_top->name;
        asm_top->global = tac_top->global;

        // convert params
        struct AsmInstr* asm_body = params_to_asm(tac_top->params, tac_top->num_params);

        // prepend stack allocation instruction
        // ASM:
        // Binary Sub SP, SP, <stack_bytes>
        struct AsmInstr* alloc_instr = arena_alloc(sizeof(struct AsmInstr));
        alloc_instr->type = ASM_BINARY;
        alloc_instr->alu_op = ALU_SUB;
        alloc_instr->dst = arena_alloc(sizeof(struct Operand));
        alloc_instr->dst->type = OPERAND_REG;
        alloc_instr->dst->reg = SP;
        alloc_instr->dst->lit_value = 0;
        alloc_instr->src1 = arena_alloc(sizeof(struct Operand));
        alloc_instr->src1->type = OPERAND_REG;
        alloc_instr->src1->reg = SP;
        alloc_instr->src2 = arena_alloc(sizeof(struct Operand));
        alloc_instr->src2->type = OPERAND_LIT;
        alloc_instr->src2->lit_value = 0; // placeholder, to be filled after stack size calculation
        alloc_instr->next = asm_body;
        asm_body = alloc_instr;

        // convert body instructions
        for (struct TACInstr* tac_instr = tac_top->body; tac_instr != NULL; tac_instr = tac_instr->next) {
            struct AsmInstr* asm_instr = instr_to_asm(tac_top->name, tac_instr);
            if (asm_body == NULL) {
                asm_body = asm_instr;
            } else {
                // append to asm_body
                struct AsmInstr* last = asm_body;
                while (last->next != NULL) {
                    last = last->next;
                }
                last->next = asm_instr;
            }
        }

        size_t stack_size = create_maps(asm_body);
        asm_body->src2->lit_value = (int)(stack_size + 4); // update stack allocation size

        replace_pseudo(asm_body);

        // Pseudo maps are per-function; reclaim them after lowering.
        if (pseudo_map != NULL) {
            destroy_pseudo_map(pseudo_map);
            pseudo_map = NULL;
        }

        asm_top->body = asm_body;

        return asm_top;
    } else if (tac_top->type == STATIC_VAR) {
        asm_top->type = ASM_STATIC_VAR;
        asm_top->name = tac_top->name;
        asm_top->global = tac_top->global;

        asm_top->alignment = type_alignment(tac_top->var_type, tac_top->name);
        asm_top->init_values = tac_top->init_values;
        asm_top->num_inits = tac_top->num_inits;  

        return asm_top;
    } else {
        asm_gen_error("top-level", tac_top->name,
                      "unsupported top-level type %d", (int)tac_top->type);
        return NULL;
    }
}

struct AsmInstr* params_to_asm(struct Slice** params, size_t num_params) {
    struct AsmInstr* head = NULL;
    struct AsmInstr* tail = NULL;

    // TAC:
    // Params p0..pN
    // ASM:
    // Mov Pseudo(p0), Reg(R1) ... Mov Pseudo(p7), Reg(R8)
    // Mov Pseudo(p8+), Mem(BP, offset)
    for (size_t i = 0; i < num_params; i++) {
        struct AsmInstr* copy_instr = arena_alloc(sizeof(struct AsmInstr));
        copy_instr->next = NULL;

        if (i < 8){
          // first 8 args are passed in registers R1-R8
          copy_instr->type = ASM_MOV;
          copy_instr->dst = arena_alloc(sizeof(struct Operand));
          copy_instr->dst->type = OPERAND_PSEUDO;
          copy_instr->dst->pseudo = params[i];
          copy_instr->src1 = arena_alloc(sizeof(struct Operand));
          copy_instr->src1->type = OPERAND_REG;
          copy_instr->src1->reg = (enum Reg)(R1 + i);
          copy_instr->src2 = NULL;
        } else {
          // additional args are passed on the stack at BP + offset
          copy_instr->type = ASM_MOV;
          copy_instr->dst = arena_alloc(sizeof(struct Operand));
          copy_instr->dst->type = OPERAND_PSEUDO;
          copy_instr->dst->pseudo = params[i];
          copy_instr->src1 = arena_alloc(sizeof(struct Operand));
          copy_instr->src1->type = OPERAND_MEMORY;
          copy_instr->src1->reg = BP;
          // the - 6 is we do -8 for the 8 stack operands, but + 2
          // because bp and ra are on the stack before any params
          copy_instr->src1->lit_value = (int)(4 * (i - 6)); // offset from BP
          copy_instr->src2 = NULL;
        }

        if (head == NULL) {
            head = copy_instr;
            tail = copy_instr;
        } else {
            tail->next = copy_instr;
            tail = copy_instr;
        }
    }

    return head;
}

struct AsmInstr* instr_to_asm(struct Slice* func_name, struct TACInstr* tac_instr) {
    if (tac_instr == NULL) {
        asm_gen_error("instruction", func_name, "NULL TAC instruction encountered");
    }

    struct AsmInstr* asm_instr = arena_alloc(sizeof(struct AsmInstr));

    switch (tac_instr->type) {
        case TACRETURN:{
            // TAC:
            // Return val
            //
            // ASM:
            // Mov R1, val
            // Ret
            struct TACReturn* ret_instr = &tac_instr->instr.tac_return;
            asm_instr->type = ASM_MOV;
            asm_instr->dst = arena_alloc(sizeof(struct Operand));
            asm_instr->dst->type = OPERAND_REG;
            asm_instr->dst->reg = R1;
            asm_instr->src1 = tac_val_to_asm(ret_instr->dst);
            asm_instr->src2 = NULL;
            struct AsmInstr* ret_asm_instr = arena_alloc(sizeof(struct AsmInstr));
            ret_asm_instr->type = ASM_RET;
            ret_asm_instr->next = NULL;
            ret_asm_instr->dst = NULL;
            ret_asm_instr->src1 = NULL;
            ret_asm_instr->src2 = NULL;
            asm_instr->next = ret_asm_instr;
            return asm_instr;
        }
        case TACCOPY:{
            // TAC:
            // Copy dst, src
            //
            // ASM:
            // Mov dst, src
            struct TACCopy* copy_instr = &tac_instr->instr.tac_copy;
            asm_instr->type = ASM_MOV;
            asm_instr->dst = tac_val_to_asm(copy_instr->dst);
            asm_instr->src1 = tac_val_to_asm(copy_instr->src);
            asm_instr->src2 = NULL;
            asm_instr->next = NULL;
            return asm_instr;
        }
        case TACUNARY:{
            // TAC:
            // Unary op dst, src
            //
            // ASM:
            // Unary op dst, src
            struct TACUnary* unary_instr = &tac_instr->instr.tac_unary;
            asm_instr->type = ASM_UNARY;
            asm_instr->unary_op = unary_instr->op;
            asm_instr->dst = tac_val_to_asm(unary_instr->dst);
            asm_instr->src1 = tac_val_to_asm(unary_instr->src);
            asm_instr->src2 = NULL;
            asm_instr->next = NULL;
            return asm_instr;
        }
        case TACBINARY:{
            // TAC:
            // Binary op dst, src1, src2
            //
            // ASM:
            // Binary op dst, src1, src2
            struct TACBinary* binary_instr = &tac_instr->instr.tac_binary;
            asm_instr->type = ASM_BINARY;
            asm_instr->alu_op = binary_instr->alu_op;
            asm_instr->dst = tac_val_to_asm(binary_instr->dst);
            asm_instr->src1 = tac_val_to_asm(binary_instr->src1);
            asm_instr->src2 = tac_val_to_asm(binary_instr->src2);
            asm_instr->next = NULL;
            return asm_instr;
        }
        case TACCOND_JUMP:{
            // TAC:
            // CondJump cond, label
            //
            // ASM:
            // CondJump cond, label
            struct TACCondJump* cond_jump_instr = &tac_instr->instr.tac_cond_jump;
            asm_instr->type = ASM_COND_JUMP;
            asm_instr->cond = cond_jump_instr->condition;
            asm_instr->label = cond_jump_instr->label;
            asm_instr->dst = NULL;
            asm_instr->src1 = NULL;
            asm_instr->src2 = NULL;
            asm_instr->next = NULL;
            return asm_instr;
        }
        case TACCMP:{
            // TAC:
            // Cmp src1, src2
            //
            // ASM:
            // Cmp src1, src2
            struct TACCmp* cmp_instr = &tac_instr->instr.tac_cmp;
            asm_instr->type = ASM_CMP;
            asm_instr->dst = NULL;
            asm_instr->src1 = tac_val_to_asm(cmp_instr->src1);
            asm_instr->src2 = tac_val_to_asm(cmp_instr->src2);
            asm_instr->next = NULL;
            return asm_instr;
        }
        case TACJUMP:{
            // TAC:
            // Jump label
            //
            // ASM:
            // Jump label
            struct TACJump* jump_instr = &tac_instr->instr.tac_jump;
            asm_instr->type = ASM_JUMP;
            asm_instr->label = jump_instr->label;
            asm_instr->dst = NULL;
            asm_instr->src1 = NULL;
            asm_instr->src2 = NULL;
            asm_instr->next = NULL;
            return asm_instr;
        }
        case TACLABEL:{
            // TAC:
            // Label label
            //
            // ASM:
            // Label label
            struct TACLabel* label_instr = &tac_instr->instr.tac_label;
            asm_instr->type = ASM_LABEL;
            asm_instr->label = label_instr->label;
            asm_instr->dst = NULL;
            asm_instr->src1 = NULL;
            asm_instr->src2 = NULL;
            asm_instr->next = NULL;
            return asm_instr;
        }
        case TACCALL:{
            // TAC:
            // Call func -> dst (args...)
            //
            // ASM:
            // Push stack args (reverse order)
            // Mov reg args into R1..R8
            // Call func
            // Binary Add SP, SP, stack_bytes
            // Mov dst, R1
            struct TACCall* call_instr = &tac_instr->instr.tac_call;
            struct AsmInstr* call_head = NULL;
            struct AsmInstr** call_tail = &call_head;

            size_t num_args = call_instr->num_args;
            struct Val* args = call_instr->args;
            const size_t REG_ARG_LIMIT = 8;
            size_t stack_arg_start = num_args > REG_ARG_LIMIT ? REG_ARG_LIMIT : num_args;

            for (size_t idx = num_args; idx > stack_arg_start; ) {
                idx--;
                struct AsmInstr* push_instr = arena_alloc(sizeof(struct AsmInstr));
                push_instr->type = ASM_PUSH;
                push_instr->src1 = tac_val_to_asm(&args[idx]);
                push_instr->src2 = NULL;
                push_instr->dst = NULL;
                push_instr->next = NULL;
                *call_tail = push_instr;
                call_tail = &push_instr->next;
            }

            size_t reg_arg_count = num_args < REG_ARG_LIMIT ? num_args : REG_ARG_LIMIT;
            for (size_t i = 0; i < reg_arg_count; i++) {
                struct AsmInstr* mov_instr = arena_alloc(sizeof(struct AsmInstr));
                mov_instr->type = ASM_MOV;
                mov_instr->dst = arena_alloc(sizeof(struct Operand));
                mov_instr->dst->type = OPERAND_REG;
                mov_instr->dst->reg = (enum Reg)(R1 + i);
                mov_instr->src1 = tac_val_to_asm(&args[i]);
                mov_instr->src2 = NULL;
                mov_instr->next = NULL;
                *call_tail = mov_instr;
                call_tail = &mov_instr->next;
            }

            struct AsmInstr* call_asm = arena_alloc(sizeof(struct AsmInstr));
            call_asm->type = ASM_CALL;
            call_asm->label = call_instr->func_name;
            call_asm->dst = NULL;
            call_asm->src1 = NULL;
            call_asm->src2 = NULL;
            call_asm->next = NULL;
            *call_tail = call_asm;
            call_tail = &call_asm->next;

            size_t stack_arg_count = num_args > REG_ARG_LIMIT ? num_args - REG_ARG_LIMIT : 0;
            if (stack_arg_count > 0) {
                int stack_bytes = (int)(stack_arg_count * STACK_SLOT_SIZE);
                struct AsmInstr* stack_adjust = arena_alloc(sizeof(struct AsmInstr));
                stack_adjust->type = ASM_BINARY;
                stack_adjust->alu_op = ALU_ADD;
                stack_adjust->dst = arena_alloc(sizeof(struct Operand));
                stack_adjust->dst->type = OPERAND_REG;
                stack_adjust->dst->reg = SP;
                stack_adjust->src1 = arena_alloc(sizeof(struct Operand));
                stack_adjust->src1->type = OPERAND_REG;
                stack_adjust->src1->reg = SP;
                stack_adjust->src2 = arena_alloc(sizeof(struct Operand));
                stack_adjust->src2->type = OPERAND_LIT;
                stack_adjust->src2->lit_value = stack_bytes;
                stack_adjust->next = NULL;
                *call_tail = stack_adjust;
                call_tail = &stack_adjust->next;
            }

            if (call_instr->dst != NULL) {
                struct AsmInstr* result_mov = arena_alloc(sizeof(struct AsmInstr));
                result_mov->type = ASM_MOV;
                result_mov->dst = tac_val_to_asm(call_instr->dst);
                result_mov->src1 = arena_alloc(sizeof(struct Operand));
                result_mov->src1->type = OPERAND_REG;
                result_mov->src1->reg = R1;
                result_mov->src2 = NULL;
                result_mov->next = NULL;
                *call_tail = result_mov;
                call_tail = &result_mov->next;
            }

            return call_head;
        }
        case TACGET_ADDRESS:{
            // TAC:
            // GetAddress dst, &src
            //
            // ASM:
            // GetAddress dst, &src
            struct TACGetAddress* get_addr_instr = &tac_instr->instr.tac_get_address;
            asm_instr->type = ASM_GET_ADDRESS;
            asm_instr->dst = tac_val_to_asm(get_addr_instr->dst);
            asm_instr->src1 = tac_val_to_asm(get_addr_instr->src);
            asm_instr->src2 = NULL;
            asm_instr->next = NULL;
            return asm_instr;
        }
        case TACLOAD:{
            // TAC:
            // Load dst, [ptr]
            //
            // ASM:
            // Mov R3, ptr
            // Mov R4, [R3 + 0]
            // Mov dst, R4
            struct TACLoad* load_instr = &tac_instr->instr.tac_load;
            asm_instr->type = ASM_MOV;
            asm_instr->dst = arena_alloc(sizeof(struct Operand));
            asm_instr->dst->type = OPERAND_REG;
            asm_instr->dst->reg = R1;
            asm_instr->src1 = tac_val_to_asm(load_instr->src_ptr);
            asm_instr->src2 = NULL;

            struct AsmInstr* mov_mem_instr = arena_alloc(sizeof(struct AsmInstr));
            mov_mem_instr->type = ASM_MOV;
            mov_mem_instr->dst = arena_alloc(sizeof(struct Operand));
            mov_mem_instr->dst->type = OPERAND_REG;
            mov_mem_instr->dst->reg = R2;
            mov_mem_instr->src1 = arena_alloc(sizeof(struct Operand));
            mov_mem_instr->src1->type = OPERAND_MEMORY;
            mov_mem_instr->src1->reg = R1;
            mov_mem_instr->src1->lit_value = 0;
            mov_mem_instr->src2 = NULL;
            mov_mem_instr->next = NULL;
            asm_instr->next = mov_mem_instr;
            
            struct AsmInstr* final_mov_instr = arena_alloc(sizeof(struct AsmInstr));
            final_mov_instr->type = ASM_MOV;
            final_mov_instr->dst = tac_val_to_asm(load_instr->dst);
            final_mov_instr->src1 = arena_alloc(sizeof(struct Operand));
            final_mov_instr->src1->type = OPERAND_REG;
            final_mov_instr->src1->reg = R2;
            final_mov_instr->src2 = NULL;
            final_mov_instr->next = NULL;
            mov_mem_instr->next = final_mov_instr;

            return asm_instr;
        }
        case TACSTORE:{
            // TAC:
            // Store [ptr], src
            //
            // ASM:
            // Mov R3, ptr
            // Mov R4, src
            // Mov [R3 + 0], R4
            struct TACStore* store_instr = &tac_instr->instr.tac_store;
            asm_instr->type = ASM_MOV;
            asm_instr->dst = arena_alloc(sizeof(struct Operand));
            asm_instr->dst->type = OPERAND_REG;
            asm_instr->dst->reg = R1;
            asm_instr->src1 = tac_val_to_asm(store_instr->dst_ptr);
            asm_instr->src2 = NULL;
            
            struct AsmInstr* mov_src_instr = arena_alloc(sizeof(struct AsmInstr));
            mov_src_instr->type = ASM_MOV;
            mov_src_instr->dst = arena_alloc(sizeof(struct Operand));
            mov_src_instr->dst->type = OPERAND_REG;
            mov_src_instr->dst->reg = R2;
            mov_src_instr->src1 = tac_val_to_asm(store_instr->src);
            mov_src_instr->src2 = NULL;

            struct AsmInstr* store_mem_instr = arena_alloc(sizeof(struct AsmInstr));
            store_mem_instr->type = ASM_MOV;
            store_mem_instr->dst = arena_alloc(sizeof(struct Operand));
            store_mem_instr->dst->type = OPERAND_MEMORY;
            store_mem_instr->dst->reg = R1;
            store_mem_instr->dst->lit_value = 0;
            store_mem_instr->src1 = arena_alloc(sizeof(struct Operand));
            store_mem_instr->src1->type = OPERAND_REG;
            store_mem_instr->src1->reg = R2;
            store_mem_instr->src2 = NULL;

            store_mem_instr->next = NULL;
            asm_instr->next = mov_src_instr;
            mov_src_instr->next = store_mem_instr;
            
            return asm_instr;
        }
        case TACCOPY_TO_OFFSET:{
          asm_gen_error("instruction", func_name,
                        "TAC instruction %s is unsupported in asm_gen",
                        tac_instr_name(tac_instr->type));
          return NULL;
        }
        default:
          asm_gen_error("instruction", func_name,
                        "unknown TAC instruction type %d (%s)",
                        (int)tac_instr->type,
                        tac_instr_name(tac_instr->type));
          return NULL;
    }

    return asm_instr;
}

size_t create_maps(struct AsmInstr* asm_instr) {
    if (pseudo_map != NULL) {
        asm_gen_error("stack-map", NULL, "pseudo map already initialized");
    }

    pseudo_map = create_pseudo_map(128);
    size_t stack_bytes = 0;

    for (struct AsmInstr* instr = asm_instr; instr != NULL; instr = instr->next) {
        size_t op_count = 0;
        struct Operand** ops = get_ops(instr, &op_count);
        if (ops == NULL) {
            continue;
        }

        for (size_t i = 0; i < op_count; i++) {
            struct Operand* opr = ops[i];
            if (opr == NULL) {
                continue;
            }

            if (opr->type != OPERAND_PSEUDO && opr->type != OPERAND_PSEUDO_MEM) {
                continue;
            }

            if (pseudo_map_get(pseudo_map, opr) != NULL) {
                continue;
            }

            struct Operand* mapped = arena_alloc(sizeof(struct Operand));
            if (is_static_symbol_operand(opr)) {
                mapped->type = OPERAND_DATA;
                mapped->reg = 0;
                mapped->lit_value = 0;
                mapped->pseudo = opr->pseudo;
            } else {
                mapped->type = OPERAND_MEMORY;
                mapped->reg = BP;
                mapped->lit_value = allocate_stack_slot(&stack_bytes);
                // TODO: handle PSEUDO_MEM offset (part of implementing arrays)
                //if (opr->type == OPERAND_PSEUDO_MEM) {
                //    mapped->lit_value += opr->lit_value;
                //}
                mapped->pseudo = NULL;
            }

            pseudo_map_insert(pseudo_map, opr, mapped);
        }
    }

    return stack_bytes;
}

struct Operand** get_ops(struct AsmInstr* asm_instr, size_t* out_count) {
    size_t src_count = 0;
    struct Operand** srcs = get_srcs(asm_instr, &src_count);
    struct Operand* dst = get_dst(asm_instr);

    size_t total_count = src_count + (dst != NULL ? 1 : 0);
    struct Operand** ops = arena_alloc(total_count * sizeof(struct Operand*));

    size_t index = 0;
    if (dst != NULL) {
        ops[index++] = dst;
    }
    for (size_t i = 0; i < src_count; i++) {
        ops[index++] = srcs[i];
    }

    *out_count = total_count;
    return ops;
}

struct Operand** get_srcs(struct AsmInstr* asm_instr, size_t* out_count) {
    switch (asm_instr->type) {
        case ASM_MOV:
            *out_count = 1;
            struct Operand** srcs_mov = arena_alloc(sizeof(struct Operand*));
            srcs_mov[0] = asm_instr->src1;
            return srcs_mov;
        case ASM_UNARY:
            *out_count = 1;
            struct Operand** srcs_unary = arena_alloc(sizeof(struct Operand*));
            srcs_unary[0] = asm_instr->src1;
            return srcs_unary;
        case ASM_BINARY:
            *out_count = 2;
            struct Operand** srcs_binary = arena_alloc(2 * sizeof(struct Operand*));
            srcs_binary[0] = asm_instr->src1;
            srcs_binary[1] = asm_instr->src2;
            return srcs_binary;
        case ASM_CMP:
            *out_count = 2;
            struct Operand** srcs_cmp = arena_alloc(2 * sizeof(struct Operand*));
            srcs_cmp[0] = asm_instr->src1;
            srcs_cmp[1] = asm_instr->src2;
            return srcs_cmp;
        case ASM_PUSH:
            *out_count = 1;
            struct Operand** srcs_push = arena_alloc(sizeof(struct Operand*));
            srcs_push[0] = asm_instr->src1;
            return srcs_push;
        case ASM_GET_ADDRESS:
            *out_count = 1;
            struct Operand** srcs_getaddr = arena_alloc(sizeof(struct Operand*));
            srcs_getaddr[0] = asm_instr->src1;
            return srcs_getaddr;
        default:
            *out_count = 0;
            return NULL;
    }
}

struct Operand* get_dst(struct AsmInstr* asm_instr) {
    switch (asm_instr->type) {
        case ASM_MOV:
            return asm_instr->dst;
        case ASM_UNARY:
            return asm_instr->dst;
        case ASM_BINARY:
            return asm_instr->dst;
        case ASM_GET_ADDRESS:
            return asm_instr->dst;
        default:
            return NULL;
    }
}

void replace_pseudo(struct AsmInstr* asm_instr) {
    for (struct AsmInstr* instr = asm_instr; instr != NULL; instr = instr->next) {
        replace_operand_if_pseudo(&instr->dst);
        replace_operand_if_pseudo(&instr->src1);
        replace_operand_if_pseudo(&instr->src2);
    }
}

static bool is_static_symbol_operand(const struct Operand* opr) {
    if (opr == NULL || opr->pseudo == NULL || global_symbol_table == NULL) {
        return false;
    }
    struct SymbolEntry* entry = symbol_table_get(global_symbol_table, opr->pseudo);
    return entry != NULL && entry->attrs != NULL && entry->attrs->attr_type == STATIC_ATTR;
}

static int allocate_stack_slot(size_t* stack_bytes) {
    size_t next_size = *stack_bytes + STACK_SLOT_SIZE;
    *stack_bytes = next_size;
    return -((int)next_size);
}

static void replace_operand_if_pseudo(struct Operand** field) {
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
        if ((*field)->pseudo != NULL) {
            name = (*field)->pseudo->start;
            len = (*field)->pseudo->len;
        }
        asm_gen_error("stack-map", NULL, "missing mapping for pseudo %.*s", (int)len, name);
    }
}

struct Operand* tac_val_to_asm(struct Val* val) {
    if (val == NULL) {
        asm_gen_error("operand", NULL, "NULL TAC value encountered");
    }

    struct Operand* opr = arena_alloc(sizeof(struct Operand));

    switch (val->val_type) {
        case CONSTANT: {
            opr->type = OPERAND_LIT;
            opr->lit_value = (int)(val->val.const_value); // assuming fits in int
            return opr;
        }
        case VARIABLE: {
            opr->type = OPERAND_PSEUDO;
            opr->pseudo = val->val.var_name;
            return opr;
        }
        default:
            asm_gen_error("operand", NULL, "unknown TAC value type %d", (int)val->val_type);
            return NULL;
    }
}

struct Operand* make_pseudo_mem(struct Val* val, int offset) {
    if (val == NULL) {
        asm_gen_error("operand", NULL, "NULL TAC value for pseudo-mem operand");
    }

    if (val->val_type == VARIABLE) {
        struct Operand* opr = arena_alloc(sizeof(struct Operand));
        opr->type = OPERAND_PSEUDO_MEM;
        opr->pseudo = val->val.var_name;
        opr->lit_value = offset;
        return opr;
    } else {
        asm_gen_error("operand", NULL,
                      "pseudo-mem operand requires variable TAC value (got %d)",
                      (int)val->val_type);
        return NULL;
    }
}

size_t type_alignment(struct Type* type, const struct Slice* symbol_name) {
    // will eventually have different alignments for different types
    // short => 2, char => 1
    if (type == NULL) {
        asm_gen_error("type-alignment", symbol_name, "NULL type for static symbol");
    }
    switch (type->type) {
        case INT_TYPE:
        case UINT_TYPE:
        case LONG_TYPE:
        case ULONG_TYPE:
        case POINTER_TYPE:
            return 4;
        default:
            asm_gen_error("type-alignment", symbol_name,
                          "unknown type kind %d", (int)type->type);
            return 0;
    }
}


struct PseudoMap* create_pseudo_map(size_t num_buckets){
  struct PseudoEntry** arr = malloc(num_buckets * sizeof(struct PseudoEntry*));
  struct PseudoMap* hmap = malloc(sizeof(struct PseudoMap));

  if (arr == NULL || hmap == NULL) {
    free(arr);
    free(hmap);
    asm_gen_error("stack-map", NULL, "allocation failed for pseudo map");
  }

  for (int i = 0; i < num_buckets; ++i){
    arr[i] = NULL;
  }

  hmap->size = num_buckets;
  hmap->arr = arr;

  return hmap;
}


struct PseudoEntry* create_pseudo_entry(struct Operand* key, struct Operand* value){
  struct PseudoEntry* entry = malloc(sizeof(struct PseudoEntry));

  entry->pseudo = key;
  entry->mapped = value;
  entry->next = NULL;

  return entry;
}

void pseudo_entry_insert(struct PseudoEntry* entry, struct Operand* key, struct Operand* value){
  if (compare_slice_to_slice(entry->pseudo->pseudo, key->pseudo)){
    entry->mapped = value;
  } else if (entry->next == NULL){
    entry->next = create_pseudo_entry(key, value);
  } else {
    pseudo_entry_insert(entry->next, key, value);
  }
}


void pseudo_map_insert(struct PseudoMap* hmap, struct Operand* key, struct Operand* value){
  if (hmap == NULL || key == NULL || key->pseudo == NULL) {
    asm_gen_error("stack-map", NULL, "invalid pseudo map insert request");
  }
  size_t label = hash_slice(key->pseudo) % hmap->size;
  
  if ((hmap->arr[label]) == NULL){
    hmap->arr[label] = create_pseudo_entry(key, value);
  } else {
    pseudo_entry_insert(hmap->arr[label], key, value);
  }
}

struct Operand* pseudo_entry_get(struct PseudoEntry* entry, struct Operand* key){
  if (compare_slice_to_slice(entry->pseudo->pseudo, key->pseudo)){
    return entry->mapped;
  } else if (entry->next == NULL){
    return 0;
  } else {
    return pseudo_entry_get(entry->next, key);
  }
}

struct Operand* pseudo_map_get(struct PseudoMap* hmap, struct Operand* key){
  if (hmap == NULL || key == NULL) {
    asm_gen_error("stack-map", NULL, "invalid pseudo map lookup request");
  }
  if (key->type != OPERAND_PSEUDO && key->type != OPERAND_PSEUDO_MEM) {
    // not a pseudo operand, do not replace
    return NULL;
  }
  if (key->pseudo == NULL) {
    asm_gen_error("stack-map", NULL, "pseudo operand missing identifier");
  }
  
  size_t label = hash_slice(key->pseudo) % hmap->size;

  if (hmap->arr[label] == NULL){
    return 0;
  } else {
    return pseudo_entry_get(hmap->arr[label], key);
  }
}


bool pseudo_entry_contains(struct PseudoEntry* entry, struct Operand* key){
  if (compare_slice_to_slice(entry->pseudo->pseudo, key->pseudo)){
    return true;
  } else if (entry->next == NULL){
    return false;
  } else {
    return pseudo_entry_contains(entry->next, key);
  }
}

bool pseudo_map_contains(struct PseudoMap* hmap, struct Operand* key){
  if (hmap == NULL || key == NULL || key->pseudo == NULL) {
    asm_gen_error("stack-map", NULL, "invalid pseudo map contains request");
  }
  size_t label = hash_slice(key->pseudo) % hmap->size;

  if (hmap->arr[label] == NULL){
    return false;
  } else {
    return pseudo_entry_contains(hmap->arr[label], key);
  }
}

void destroy_pseudo_entry(struct PseudoEntry* entry){
  if (entry->next != NULL) destroy_pseudo_entry(entry->next);
  free(entry);
}

void destroy_pseudo_map(struct PseudoMap* hmap){
  for (int i = 0; i < hmap->size; ++i){
    if (hmap->arr[i] != NULL) destroy_pseudo_entry(hmap->arr[i]);
  }
  free(hmap->arr);
  free(hmap);
}
