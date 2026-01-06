#include "asm_gen.h"
#include "arena.h"

#include <stdio.h>

struct PseudoMap* pseudo_map = NULL;

struct AsmProg* prog_to_asm(struct TACProg* tac_prog) {
    struct AsmProg* asm_prog = arena_alloc(sizeof(struct AsmProg));
    asm_prog->head = NULL;
    asm_prog->tail = NULL;

    for (struct TopLevel* tac_top = tac_prog->head; tac_top != NULL; tac_top = tac_top->next) {
        struct AsmTopLevel* asm_top = top_level_to_asm(tac_top);
        if (asm_prog->head == NULL) {
            asm_prog->head = asm_top;
            asm_prog->tail = asm_top;
        } else {
            asm_prog->tail->next = asm_top;
            asm_prog->tail = asm_top;
        }
    }

    return asm_prog;
}

struct AsmTopLevel* top_level_to_asm(struct TopLevel* tac_top) {
    struct AsmTopLevel* asm_top = arena_alloc(sizeof(struct AsmTopLevel));
    
    if (tac_top->type == FUNC) {
        asm_top->type = ASM_FUNC;
        asm_top->name = tac_top->name;
        asm_top->global = tac_top->global;

        // convert params
        struct AsmInstr* asm_body = params_to_asm(tac_top->params, tac_top->num_params);

        // prepend stack allocation instruction
        struct AsmInstr* alloc_instr = arena_alloc(sizeof(struct AsmInstr));
        alloc_instr->type = ALU_SUB; // assuming SUB decreases SP
        alloc_instr->dst = arena_alloc(sizeof(struct Operand));
        alloc_instr->dst->type = OPERAND_MEMORY;
        alloc_instr->dst->reg = SP;
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
            struct AsmInstr* asm_instr = instr_to_asm(tac_instr);
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

        asm_top->body = asm_body;

        return asm_top;
    } else if (tac_top->type == STATIC_VAR) {
        asm_top->type = ASM_STATIC_VAR;
        asm_top->name = tac_top->name;
        asm_top->global = tac_top->global;

        asm_top->alignment = type_alignment(tac_top->var_type);
        asm_top->init_values = tac_top->init_values;
        asm_top->num_inits = tac_top->num_inits;  

        return asm_top;
    } else {
        // unknown top-level type
        printf("ASM Generation Error: unknown top-level type %d\n", (int)tac_top->type);
        return NULL;
    }
}

struct AsmInstr* params_to_asm(struct Slice** params, size_t num_params) {
    struct AsmInstr* head = NULL;
    struct AsmInstr* tail = NULL;

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
        } else {
          // additional args are passed on the stack at BP + offset
          copy_instr->type = ASM_MOV;
          copy_instr->dst = arena_alloc(sizeof(struct Operand));
          copy_instr->dst->type = OPERAND_PSEUDO;
          copy_instr->dst->pseudo = params[i];
          copy_instr->src1 = arena_alloc(sizeof(struct Operand));
          copy_instr->src1->type = OPERAND_MEMORY;
          copy_instr->src1->reg = BP;
          copy_instr->src1->lit_value = (int)(4 * (i - 6)); // offset from BP
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

/*
exprToAsm :: TACAST.Instr -> [Instr]
exprToAsm instr =
  case instr of
    (TACAST.Return val) -> [Mov (Reg R3) (tacValToAsm val), Ret]
    (TACAST.Copy dst src) -> [Mov (tacValToAsm dst) (tacValToAsm src)]
    (TACAST.Unary op dst src) ->
      [Unary op (tacValToAsm dst) (tacValToAsm src)]
    (TACAST.Binary op dst src1 src2 type_) ->
      [Binary op (tacValToAsm dst) (tacValToAsm src1) (tacValToAsm src2) type_]
    (TACAST.CondJump cond label) -> [CondJump cond label]
    (TACAST.Cmp val1 val2) -> [Cmp (tacValToAsm val1) (tacValToAsm val2)]
    (TACAST.Jump label) -> [Jump label]
    (TACAST.Label s) -> [Label s]
    (TACAST.Call name dst srcs) ->
      -- push in reverse order
      getZipList (ZipList stackList <*> ZipList (reverse stackArgs)) ++
      getZipList (ZipList regList <*> ZipList regArgs) ++
      [Call name,
      AllocateStack (length stackArgs), -- deallocate stack
      Mov (tacValToAsm dst) (Reg R3)]
      where regList =
              [Mov (Reg R3) . tacValToAsm,
               Mov (Reg R4) . tacValToAsm,
               Mov (Reg R5) . tacValToAsm,
               Mov (Reg R6) . tacValToAsm]
            regArgs = take 4 srcs
            stackArgs = drop 4 srcs
            stackList = repeat (Push . tacValToAsm)
    (TACAST.GetAddress dst src) -> [GetAddress (tacValToAsm dst) (tacValToAsm src)]
    (TACAST.Load dst ptr) -> [Mov (Reg R3) (tacValToAsm ptr),
                              Mov (Reg R4) (Memory R3 0),
                              Mov (tacValToAsm dst) (Reg R4)]
    (TACAST.Store ptr src) -> [Mov (Reg R3) (tacValToAsm ptr),
                               Mov (Reg R4) (tacValToAsm src),
                              Mov (Memory R3 0) (Reg R4)]
*/

struct AsmInstr* instr_to_asm(struct TACInstr* tac_instr) {
    struct AsmInstr* asm_instr = arena_alloc(sizeof(struct AsmInstr));

    switch (tac_instr->type) {
        case TACRETURN:{
          
        }
        case TACCOPY:{
          
        }
        case TACUNARY:{
          
        }
        case TACBINARY:{

        }
        case TACCOND_JUMP:{
          
        }
        case TACCMP:{

        }
        case TACJUMP:{

        }
        case TACLABEL:{

        }
        case TACCALL:{

        }
        case TACGET_ADDRESS:{

        }
        case TACLOAD:{

        }
        case TACSTORE:{

        }
        case TACCOPY_TO_OFFSET:{
          // not implemented yet
          return NULL;
        }
        default:
          // unknown instruction type
          return NULL;
    }

    return asm_instr;
}

/*
createMaps :: [Instr] -> SymbolTable -> ([(Operand, Operand)], Int)
createMaps xs symbols = foldr (createMapsFold symbols) ([], -1) (xs >>= getOps)

createMapsFold :: SymbolTable -> Operand -> ([(Operand, Operand)], Int) -> ([(Operand, Operand)], Int)
createMapsFold symbols opr (maps, size) =
  case opr of
    (Pseudo v) ->
      case lookup opr maps of
        (Just _) -> (maps, size)
        Nothing -> case lookup v symbols of
          -- static var is stored in data section, not stack
          Just (_, StaticAttr _ _) -> ((opr, Data v):maps, size)
          _ -> ((opr, Memory bp size):maps, size - 1)
    (PseudoMem v _) ->
      case lookup opr maps of
        (Just _) -> (maps, size)
        Nothing -> case lookup v symbols of
          -- static var is stored in data section, not stack
          Just (_, StaticAttr _ _) -> ((opr, Data v):maps, size)
          _ -> ((opr, Memory bp size):maps, size - 1)
    _ -> (maps, size)
*/

size_t create_maps(struct AsmInstr* asm_instr) {
    return 0;
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
            srcs_getaddr[0] = asm_instr->src2;
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

/*
-- map identifiers used as src/dst to stack locations
replacePseudo :: [(Operand, Operand)] -> Instr -> Instr
replacePseudo maps = mapOps f
  where f op = case lookup op maps of
          (Just newOp) -> newOp
          Nothing -> case op of
            Pseudo _ -> error "Compiler Error: Missing map for pseudoregister"
            _ -> op

mapOps :: (Operand -> Operand) -> Instr -> Instr
mapOps f (Mov a b) = Mov (f a) (f b)
mapOps f (Unary op a b) = Unary op (f a) (f b)
mapOps f (Binary op a b c type_) = Binary op (f a) (f b) (f c) type_
mapOps f (Cmp a b) = Cmp (f a) (f b)
mapOps f (Push a) = Push (f a)
mapOps f (GetAddress a b) = GetAddress (f a) (f b)
mapOps _ x = x
*/

void replace_pseudo(struct AsmInstr* asm_instr) {

}

struct Operand* tac_val_to_asm(struct Val* val) {
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
            // unknown val type
            printf("TAC to ASM Error: unknown Val type %d\n", (int)val->val_type);
            return NULL;
    }
}

struct Operand* make_pseudo_mem(struct Val* val, int offset) {
    if (val->val_type == VARIABLE) {
        struct Operand* opr = arena_alloc(sizeof(struct Operand));
        opr->type = OPERAND_PSEUDO_MEM;
        opr->pseudo = val->val.var_name;
        opr->lit_value = offset;
        return opr;
    } else {
        // error: attempted to make PseudoMem for non-var
        printf("Make PseudoMem Error: attempted to make PseudoMem for non-var\n");
        return NULL;
    }
}

size_t type_alignment(struct Type* type) {
    // will eventually have different alignments for different types
    // short => 2, char => 1
    switch (type->type) {
        case INT_TYPE:
        case UINT_TYPE:
        case LONG_TYPE:
        case ULONG_TYPE:
        case POINTER_TYPE:
            return 4;
        default:
            printf("Type Alignment Error: unknown type kind %d\n", (int)type->type);
            return 0;
    }
}
