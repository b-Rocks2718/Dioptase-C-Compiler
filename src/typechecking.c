#include "typechecking.h"
#include "arena.h"

#include <stdio.h>

struct SymbolTable* global_symbol_table = NULL;

bool typecheck_program(struct Program* program) {
  global_symbol_table = create_symbol_table(1024);

  // typecheck each declaration in the program
  for (struct DeclarationList* cur = program->dclrs; cur != NULL; cur = cur->next){
    if (!typecheck_file_scope_dclr(&cur->dclr)) {
      return false;
    }
  }
  return true;
}

bool typecheck_file_scope_dclr(struct Declaration* dclr) {
  switch (dclr->type) {
    case VAR_DCLR:
      // Type check variable declaration
      return typecheck_file_scope_var(&dclr->dclr.var_dclr);
    case FUN_DCLR:
      // Type check function declaration
      return typecheck_func(&dclr->dclr.fun_dclr);
    default:
      printf("Type error: Unknown declaration type in typecheck_file_scope_dclr\n");
      return false; // Unknown declaration type
  }
}

bool typecheck_file_scope_var(struct VariableDclr* var_dclr) {
  enum IdentInitType init_type = -1;
  // init type of new declaration
  if (var_dclr->init != NULL) {
    init_type = INITIAL;
  } else if (var_dclr->storage != EXTERN) {
    init_type = TENTATIVE;
  } else {
    init_type = NO_INIT;
  }

  // if there is an initializer, typecheck it
  if (var_dclr->init != NULL) {
    if (var_dclr->init->type != LIT) {
      // For simplicity, we only allow literal initializers for global variables
      printf("Type error: Non-constant initializer for global variable %.*s\n",
             (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    if (var_dclr->type->type == POINTER_TYPE) {
      // For simplicity, we only allow null pointer constants as initializers for global pointer variables
      struct LitExpr* lit_expr = &var_dclr->init->expr.lit_expr;
      if (lit_expr->value.int_val != 0) {
        printf("Type error: Invalid pointer initializer for global variable %.*s\n",
               (int)var_dclr->name->len, var_dclr->name->start);
        return false;
      }
    }

    if (!typecheck_expr(var_dclr->init)) {
      return false;
    }
  }

  struct SymbolEntry* entry = symbol_table_get(global_symbol_table, var_dclr->name);

  bool global = (var_dclr->storage != STATIC);

  // check if this variable has been declared before
  if (entry != NULL) {

    // reject function types
    if (entry->type->type == FUN_TYPE) {
      printf("Type error: Function %.*s redeclared as variable\n",
             (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    // ensure both declarations have the same type
    if (!compare_types(entry->type, var_dclr->type)) {
      printf("Type error: Conflicting declarations for variable %.*s\n",
             (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    // check for duplicate definitions
    if (entry->attrs->init.init_type == INITIAL && var_dclr->init != NULL) {
      printf("Type error: Conflicting file scope variable definitions for variable %.*s\n",
             (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    // check for conflicting linkage
    if (entry->attrs->is_global != global) {
      printf("Type error: Conflicting variable linkage for variable %.*s\n",
             (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    // by this point, we know the types match and linkage is consistent

    // the only case where we need to update the symbol table is if
    // we are upgrading the initialization status
    // the ordering is NO_INIT < TENTATIVE < INITIAL

    if (init_type > entry->attrs->init.init_type) {
      // upgrade init type
      entry->attrs->init.init_type = init_type;
      if (init_type == INITIAL) {
        entry->attrs->init.init_list = arena_alloc(sizeof(struct InitList));
        entry->attrs->init.init_list->value.int_type = INT_INIT; // assuming int type for simplicity
        entry->attrs->init.init_list->next = NULL;
      }
    }
  } else {
    // new declaration, add to symbol table
    struct IdentAttr* attrs = arena_alloc(sizeof(struct IdentAttr));
    attrs->attr_type = STATIC_ATTR;
    attrs->is_defined = (init_type == INITIAL);
    attrs->is_global = global;
    attrs->init.init_type = init_type;
    if (init_type == INITIAL) {
      attrs->init.init_list = arena_alloc(sizeof(struct InitList));
      attrs->init.init_list->value.int_type = INT_INIT; // assuming int type for simplicity
      attrs->init.init_list->value.value = var_dclr->init->expr.lit_expr.value.int_val;
      attrs->init.init_list->next = NULL;
    } else {
      attrs->init.init_list = NULL;
    }

    symbol_table_insert(global_symbol_table, var_dclr->name, var_dclr->type, attrs);
  }

  return true;
}

/*
typecheckFunc :: AST.FunctionDclr -> StateT SymbolTable Result FunctionDclr
typecheckFunc (AST.FunctionDclr name type_  mStorage params mBody) = do
  let hasBody = case mBody of
        Just _ -> True
        Nothing -> False
      global = mStorage /= Just Static
  when (isArrayType type_) $
    lift (Err $ "Semantics Error: function " ++ show name ++ " returns an array")
  let newParams = processFunParam <$> params
  maps <- get
  case lookup name maps of
    Just (oldType, oldAttrs) -> do
      let (alreadyDefined, oldGlobal) = getFunAttrs oldAttrs
      if isFunc oldType && oldType /= type_ then
        if isFunc oldType
          then lift (Err $ "Semantics Error: Incompatible function declarations for " ++ show name ++
            ": " ++ show oldType ++ " /= " ++ show type_)
        else do
          -- function shadows variable, so we add to the map instead of replacing the old def
          let attrs = FunAttr (hasBody || alreadyDefined) global
          put $ replace name (type_, attrs) maps
      else if oldGlobal && mStorage == Just Static
        then lift (Err $ "Semantics Error: Static function declaration follows non-static for function " ++ show name)
      else if hasBody && alreadyDefined
        then lift (Err $ "Semantics Error: Multiple definitions for function " ++ show name)
      else do
        let attrs = FunAttr (hasBody || alreadyDefined) global
        put $ replace name (type_, attrs) maps
    Nothing -> do
      let attrs = FunAttr hasBody global
      put $ (name, (type_, attrs)) : maps

  typedParams <- typecheckParams newParams
  typedBody <- liftMaybe typecheckBlock mBody
  return (FunctionDclr name type_ mStorage typedParams typedBody)
*/
  
bool typecheck_func(struct FunctionDclr* func_dclr) {
  struct SymbolEntry* entry = symbol_table_get(global_symbol_table, func_dclr->name);

  if (entry == NULL) {
    // new function declaration
    struct IdentAttr* attrs = arena_alloc(sizeof(struct IdentAttr));
    attrs->attr_type = FUN_ATTR;
    attrs->is_defined = (func_dclr->body != NULL);
    attrs->is_global = (func_dclr->storage != STATIC);
    symbol_table_insert(global_symbol_table, func_dclr->name, func_dclr->type, attrs);
  } else {

  }

  return false;
}

/*
typecheckParams :: [AST.VariableDclr] -> StateT SymbolTable Result [VariableDclr]
typecheckParams params = do
  maps <- get
  put $ maps ++ paramMaps params
  foldr typecheckParamsFold (pure []) params

typecheckParamsFold :: AST.VariableDclr ->
    StateT SymbolTable Result [VariableDclr] ->
    StateT SymbolTable Result [VariableDclr]
typecheckParamsFold (AST.VariableDclr name type_ Nothing Nothing) paramsState = do
  params <- paramsState
  return (VariableDclr name type_ Nothing Nothing : params)
typecheckParamsFold _ _ = error "Compiler Error: function parameter should not have initializer"

paramMaps :: [AST.VariableDclr] -> SymbolTable
paramMaps = foldr paramMapsFold []

paramMapsFold :: AST.VariableDclr -> SymbolTable -> SymbolTable
paramMapsFold (AST.VariableDclr name type_ Nothing Nothing) table =
  (name, (type_, LocalAttr)) : table
paramMapsFold _ _ = error "Compiler Error: function parameter should not have initializer"
*/

/*
typecheckBlock :: AST.Block -> StateT SymbolTable Result Block
typecheckBlock (AST.Block items) = Block <$> foldl' typecheckBlockFold (return []) items

typecheckBlockFold :: StateT SymbolTable Result [BlockItem] -> AST.BlockItem ->
  StateT SymbolTable Result [BlockItem]
typecheckBlockFold oldState item = do
  typedBlocks <- oldState
  typedBlock <- typecheckBlockItem item
  return (append typedBlocks typedBlock)
*/

/*
typecheckBlockItem :: AST.BlockItem -> StateT SymbolTable Result BlockItem
typecheckBlockItem item = case item of
  AST.StmtBlock stmt -> StmtBlock <$> typecheckStmt stmt
  AST.DclrBlock dclr -> DclrBlock <$> typecheckLocalDclr dclr
*/

/*
typecheckStmt :: AST.Stmt -> StateT SymbolTable Result Stmt
typecheckStmt stmt = case stmt of
  AST.RetStmt expr mFunc -> do
    typedExpr <- typecheckAndConvert expr
    maps <- get
    let retType = case lookup (fromJust mFunc) maps of
            (Just (FunType _ type_, _)) -> type_
            _ -> error "Compiler Error: this should be a function declaration"
    retVal <- case convertByAssignment typedExpr retType of
      Ok v -> return v
      other -> lift other
    return (RetStmt retVal)
  AST.ExprStmt expr -> ExprStmt <$> typecheckAndConvert expr
  AST.IfStmt expr stmt1 mStmt2 -> do
    typedStmt2 <- liftMaybe typecheckStmt mStmt2
    typedExpr <- typecheckAndConvert expr
    typedStmt1 <- typecheckStmt stmt1
    return (IfStmt typedExpr typedStmt1 typedStmt2)
  AST.GoToStmt label -> return (GoToStmt label)
  AST.LabeledStmt name stmt' -> LabeledStmt name <$> typecheckStmt stmt'
  AST.CompoundStmt block -> CompoundStmt <$> typecheckBlock block
  AST.BreakStmt label -> return (BreakStmt label)
  AST.ContinueStmt label -> return (ContinueStmt label)
  AST.WhileStmt expr stmt' label -> do
    typedExpr <- typecheckAndConvert expr
    typedStmt <- typecheckStmt stmt'
    return (WhileStmt typedExpr typedStmt label)
  AST.DoWhileStmt stmt' expr label -> do
    typedStmt <- typecheckStmt stmt'
    typedExpr <- typecheckAndConvert expr
    return (DoWhileStmt typedStmt typedExpr label)
  AST.ForStmt init_ mExpr1 mExpr2 stmt' label -> do
    typedInit <- typecheckForInit init_
    typedExpr1 <- liftMaybe typecheckAndConvert mExpr1
    typedExpr2 <- liftMaybe typecheckAndConvert mExpr2
    typedStmt <- typecheckStmt stmt'
    return (ForStmt typedInit typedExpr1 typedExpr2 typedStmt label)
  AST.SwitchStmt expr stmt' label cases -> do
    typedExpr <- typecheckAndConvert expr
    typedStmt <- typecheckStmt stmt'
    if isArithmeticType (getExprType typedExpr) then
      return (SwitchStmt typedExpr typedStmt label cases)
    else
      lift (Err "Switch condition must have arithmetic type")
  AST.CaseStmt expr stmt' label -> do
    typedStmt <- typecheckStmt stmt'
    typedExpr <- typecheckAndConvert expr
    return (CaseStmt typedExpr typedStmt label)
  AST.DefaultStmt stmt' label -> do
    typedStmt <- typecheckStmt stmt'
    return (DefaultStmt typedStmt label)
  AST.NullStmt -> return NullStmt
*/

/*
typecheckForInit :: AST.ForInit -> StateT SymbolTable Result ForInit
typecheckForInit init_ = case init_ of
  AST.InitDclr dclr -> InitDclr <$> typecheckLocalVar dclr
  AST.InitExpr (Just expr) -> InitExpr . Just <$> typecheckAndConvert expr
  AST.InitExpr Nothing -> return (InitExpr Nothing)
*/

/*
typecheckLocalDclr :: AST.Declaration -> StateT SymbolTable Result Declaration
typecheckLocalDclr dclr = case dclr of
  AST.VarDclr v -> VarDclr <$> typecheckLocalVar v
  AST.FunDclr f -> FunDclr <$> typecheckFunc f
*/

/*
getName :: String -> String
getName = show . takeWhile (/= '.')
*/

/*
typecheckLocalVar :: AST.VariableDclr -> StateT SymbolTable Result VariableDclr
typecheckLocalVar (AST.VariableDclr v type_ mStorage mInit) = do
  maps <- get
  if mStorage == Just Extern then
    if isJust mInit then
      lift (Err $ "Initializer on local extern variable declaration for variable " ++ getName v)
    else case lookup v maps of
      Nothing -> do
        put $ (v, (type_, StaticAttr NoInit True)) : maps -- add v to symbol table
        return (VariableDclr v type_ mStorage Nothing) -- mExpr was Nothing, no need to typecheck
      Just (FunType _ _, _) -> lift (Err $ "Function " ++ v ++ " redeclared as variable")
      Just (oldType, _) ->
        if type_ == oldType then
          return (VariableDclr v type_ mStorage Nothing) -- mExpr was Nothing, no need to typecheck
        else
          lift (Err $ "Conflicting types for variable " ++ getName v)
  else if mStorage == Just Static then
    case isInitConst type_ <$> mInit of
      Just (Just i) -> do -- the expr was constant, so we're good
        put $ (v, (type_, StaticAttr (Initial i) False)) : maps
        e <- liftMaybe (typecheckInit type_) mInit
        return (VariableDclr v type_ mStorage e)
      Just Nothing -> lift (Err $ "Non -constant initializer on local static variable " ++ getName v)
      Nothing -> do -- there was no expr initializer
        put $ (v, (type_, StaticAttr (Initial $ intStaticInit type_ 0) True)) : maps
        return (VariableDclr v type_ mStorage Nothing)
  else do -- it's a local variable, and we need to typecheck the initializer
    put ((v, (type_, LocalAttr)) : maps)
    typedInit <- liftMaybe (typecheckInit type_) mInit
    return (VariableDclr v type_ mStorage typedInit)
*/

/*
typecheckAndConvert :: AST.Expr -> StateT SymbolTable Result Expr
typecheckAndConvert expr = do
  typedExpr <- typecheckExpr expr
  case getExprType typedExpr of
    ArrayType type_ _ -> return (AddrOf typedExpr (PointerType type_))
    _ -> return typedExpr
*/

/*
typecheckInit :: Type_ -> AST.VarInit -> StateT SymbolTable Result VarInit
typecheckInit targetType init_ = case (targetType, init_) of
  (_, AST.SingleInit expr) -> do
    typedExpr <- typecheckAndConvert expr
    let castExpr = convertByAssignment typedExpr targetType
    case castExpr of
      Ok e -> return (SingleInit e targetType)
      _ -> lift (Err "Semantics Error: invalid initializer 1")
  (ArrayType inner size, AST.CompoundInit inits) -> do
    when (length inits > size) $ 
      lift (Err "Semantics Error: initializer list contains too many values")
    resolved <- traverse (typecheckInit inner) inits
    -- pad with 0s
    let padded = take size (resolved ++ repeat (zeroInitializer inner))
    return (CompoundInit padded targetType)
  _ -> lift (Err "Semantics Error: invalid initializer 2")
*/

/*
typecheckExpr :: AST.Expr -> StateT SymbolTable Result Expr
typecheckExpr e = case e of
  -- pointers support == and !=,
  -- so they are handled slightly differently
  AST.Binary BoolEq left right -> do
    -- typecheck left and right expressions
    typedLeft <- typecheckAndConvert left
    typedRight <- typecheckAndConvert right
    let leftType = getExprType typedLeft
        rightType = getExprType typedRight
        -- figure out the common type to cast to
        commonTypeResult =
          if isPointerType leftType || isPointerType rightType
          then getCommonPointerType typedLeft typedRight
          else return $ getCommonType leftType rightType
    commonType <- case commonTypeResult of
      Ok t -> return t
      Err err -> lift (Err err) -- pointer cast may fail
      Fail -> lift Fail
    let convertedLeft = convertExprType typedLeft commonType
        convertedRight = convertExprType typedRight commonType
    return (Binary BoolEq convertedLeft convertedRight IntType)
  AST.Binary BoolNeq left right -> do
    typedLeft <- typecheckAndConvert left
    typedRight <- typecheckAndConvert right
    let leftType = getExprType typedLeft
        rightType = getExprType typedRight
        commonTypeResult =
          if isPointerType leftType || isPointerType rightType
          then getCommonPointerType typedLeft typedRight
          else return $ getCommonType leftType rightType
    commonType <- case commonTypeResult of
      Ok t -> return t
      Err err -> lift (Err err)
      Fail -> lift Fail
    let convertedLeft = convertExprType typedLeft commonType
        convertedRight = convertExprType typedRight commonType
    return (Binary BoolNeq convertedLeft convertedRight IntType)
  AST.Binary AddOp left right -> do
    typedLeft <- typecheckAndConvert left
    typedRight <- typecheckAndConvert right
    let leftType = getExprType typedLeft
        rightType = getExprType typedRight
    if isArithmeticType leftType && isArithmeticType rightType then do
      let commonType = getCommonType leftType rightType
          convertedLeft = convertExprType typedLeft commonType
          convertedRight = convertExprType typedRight commonType
      return (Binary AddOp convertedLeft convertedRight commonType)
    else if (isArithmeticType leftType && isPointerType rightType) ||
            (isPointerType leftType && isArithmeticType rightType) then
      return (Binary AddOp typedLeft typedRight leftType)
    else lift (Err "Semantics Error: Invalid pointer arithmetic")
  AST.Binary PlusEqOp left right -> do
    typedLeft <- typecheckAndConvert left
    typedRight <- typecheckAndConvert right
    let leftType = getExprType typedLeft
        rightType = getExprType typedRight
    if isArithmeticType leftType && isArithmeticType rightType then do
      let commonType = getCommonType leftType rightType
          convertedLeft = convertExprType typedLeft commonType
          convertedRight = convertExprType typedRight commonType
      return (Binary PlusEqOp convertedLeft convertedRight commonType)
    else if (isArithmeticType leftType && isPointerType rightType) ||
            (isPointerType leftType && isArithmeticType rightType) then
      return (Binary PlusEqOp typedLeft typedRight leftType)
    else lift (Err "Semantics Error: Invalid pointer arithmetic")
  AST.Binary SubOp left right -> do
    typedLeft <- typecheckAndConvert left
    typedRight <- typecheckAndConvert right
    let leftType = getExprType typedLeft
        rightType = getExprType typedRight
    if isArithmeticType leftType && isArithmeticType rightType then do
      let commonType = getCommonType leftType rightType
          convertedLeft = convertExprType typedLeft commonType
          convertedRight = convertExprType typedRight commonType
      return (Binary SubOp convertedLeft convertedRight commonType)
    else if isPointerType leftType && isArithmeticType rightType then
      return (Binary SubOp typedLeft typedRight leftType)
    else lift (Err "Semantics Error: Invalid pointer arithmetic")
  AST.Binary MinusEqOp left right -> do
    typedLeft <- typecheckAndConvert left
    typedRight <- typecheckAndConvert right
    let leftType = getExprType typedLeft
        rightType = getExprType typedRight
    if isArithmeticType leftType && isArithmeticType rightType then do
      let commonType = getCommonType leftType rightType
          convertedLeft = convertExprType typedLeft commonType
          convertedRight = convertExprType typedRight commonType
      return (Binary MinusEqOp convertedLeft convertedRight commonType)
    else if isPointerType leftType && isArithmeticType rightType then
      return (Binary MinusEqOp typedLeft typedRight leftType)
    else lift (Err "Semantics Error: Invalid pointer arithmetic")
  AST.Binary op left right -> do
    typedLeft <- typecheckAndConvert left
    typedRight <- typecheckAndConvert right
    if op == BoolAnd || op == BoolOr
      then return (Binary op typedLeft typedRight AST.IntType)
    else do
      let leftType = getExprType typedLeft
          rightType = getExprType typedRight
      when (isPointerType leftType || isPointerType rightType) $
        lift (Err "Semantics Error: Invalid pointer arithmetic")
      let commonType = getCommonType leftType rightType
          convertedLeft = convertExprType typedLeft commonType
          convertedRight = convertExprType typedRight commonType
      return (Binary op convertedLeft convertedRight $
        if op `elem` AST.relationalOps then
          IntType
        else
          commonType)
  AST.Assign left right -> do
    typedLeft <- typecheckAndConvert left
    unless (isLValue typedLeft)
      (lift $ Err "Semantics Error: Cannot assign to non-lvalue")
    typedRight <- typecheckAndConvert right
    -- cast right expr to left expr
    let leftType = getExprType typedLeft
        convertedRightResult = convertByAssignment typedRight leftType
    convertedRight <- case convertedRightResult of
      Ok r -> return r
      other -> lift other
    return (Assign typedLeft convertedRight leftType)
  AST.PostAssign expr op -> do
    typedExpr <- typecheckAndConvert expr
    return (PostAssign typedExpr op (getExprType typedExpr))
  AST.Conditional c left right -> do
    typedC <- typecheckAndConvert c
    typedLeft <- typecheckAndConvert left
    typedRight <- typecheckAndConvert right
    -- find common type for left and right expr
    let leftType = getExprType typedLeft
        rightType = getExprType typedRight
        commonTypeResult =
          if isPointerType leftType || isPointerType rightType
          then getCommonPointerType typedLeft typedRight
          else return $ getCommonType leftType rightType
    commonType <- case commonTypeResult of
      Ok t -> return t
      Err err -> lift (Err err)
      Fail -> lift Fail
    let convertedLeft = convertExprType typedLeft commonType
        convertedRight = convertExprType typedRight commonType
    return (Conditional typedC convertedLeft convertedRight commonType)
  AST.FunctionCall name args -> do
    maps <- get
    case lookup name maps of
      Just (rsltType, _) -> case rsltType of
        AST.FunType paramTypes retType ->
          if length paramTypes /= length args then
            lift (Err $ "Function " ++ show name ++ " called with wrong number of arguments")
          else do
            argsRslt <- typecheckArgs args paramTypes
            return (FunctionCall name argsRslt retType)
        _ -> lift
          (Err $ "Variable " ++ show (head $ splitOn "." name) ++ " cannot be used as a function")
      Nothing -> error $ "Compiler Error: missed function declaration for " ++ show name
  AST.Var v -> do
    maps <- get
    case lookup v maps of
      Just (rsltType, _) -> case rsltType of
        AST.FunType _ _ -> lift (Err $ "Function " ++ show v ++ " cannot be used as a variable")
        type_ -> return (Var v type_)
      Nothing -> error $ "Compiler Error: missed variable declaration for " ++ show v
  AST.Unary op expr' -> do
    rslt <- typecheckAndConvert expr'
    when (isPointerType (getExprType rslt) &&
          op `elem` [Negate, Complement]) $
      lift (Err "Semantics Error: invalid pointer operation")
    let type_ = if op == BoolNot
        then IntType
        else getExprType rslt
    return (Unary op rslt type_)
  AST.Lit c -> case c of
    AST.ConstInt _ -> return (Lit c IntType)
    AST.ConstUInt _ -> return (Lit c UIntType)
    AST.ConstLong _ -> return (Lit c LongType)
    AST.ConstULong _ -> return (Lit c ULongType)
  AST.Cast target expr -> do
    when (isArrayType target) $
      lift (Err "Semantics Error: cannot cast to array type")
    rslt <- typecheckAndConvert expr
    return (Cast target rslt)
  AST.AddrOf inner -> do
    typedInner <- typecheckExpr inner
    if isLValue typedInner then do
      let referenced = getExprType typedInner
      return (AddrOf typedInner (PointerType referenced))
    else lift (Err $ "Semantics Error: Can't take the address of a non-lvalue " ++ show inner)
  AST.Dereference inner -> do
    typedInner <- typecheckAndConvert inner
    case getExprType typedInner of
      PointerType referenced -> return (Dereference typedInner referenced)
      _ -> lift (Err $ "Semantics Error: Cannot dereference non-pointer " ++ show inner)
  AST.Subscript left right -> do
    typedLeft <- typecheckAndConvert left
    typedRight <- typecheckAndConvert right
    let leftType = getExprType typedLeft
        rightType = getExprType typedRight
    if isPointerType leftType && isArithmeticType rightType then
      return (Subscript typedLeft typedRight (getRefType leftType))
    else if isArithmeticType leftType && isPointerType rightType then
      return (Subscript typedRight typedLeft (getRefType rightType))
    else lift (Err "Semantics Error: Invalid subscript use")
*/

bool typecheck_expr(struct Expr* expr) {
  return false;
}

/*
convertExprType :: Expr -> Type_ -> Expr
convertExprType expr type_ =
  if getExprType expr == type_ then
    expr
  else Cast type_ expr
*/

/*
typecheckArgs :: [AST.Expr] -> [Type_] -> StateT SymbolTable Result [Expr]
typecheckArgs args types =
  foldr typecheckArgsFold (return []) (zip args types)

typecheckArgsFold :: (AST.Expr, Type_) ->
  StateT SymbolTable Result [Expr] ->
  StateT SymbolTable Result [Expr]
typecheckArgsFold (arg, paramType) args = do
  typedArg <- typecheckAndConvert arg
  typedArgs <- args
  convertedArg <- case convertByAssignment typedArg paramType of
    Ok expr -> return expr
    other -> lift other
  return (convertedArg : typedArgs)
*/

/*
-- finds the type to use for implicit casting
getCommonType :: Type_ -> Type_ -> Type_
getCommonType t1 t2
  | t1 == t2 = t1
  | typeSize t1 == typeSize t2 =
    if isSigned t1 then t2 else t1
  | typeSize t1 > typeSize t2 = t1
  | otherwise = t2

isLValue :: Expr -> Bool
isLValue expr = case expr of
  Var _ _ -> True
  Dereference _ _ -> True
  Subscript {} -> True
  _ -> False
*/

/*
getCommonPointerType :: Expr -> Expr -> Result Type_
getCommonPointerType expr1 expr2
  | t1 == t2 = Ok t1
  | isNullPointerConstant expr1 = Ok t2
  | isNullPointerConstant expr2 = Ok t1
  | otherwise = Err "Semantics Error: Expressions have incompatible types"
  where t1 = getExprType expr1
        t2 = getExprType expr2
*/

/*
convertByAssignment :: Expr -> Type_ -> Result Expr
convertByAssignment expr target
  | getExprType expr == target =
    return expr
  | isArithmeticType (getExprType expr) && isArithmeticType target =
    return (convertExprType expr target)
  | isPointerType (getExprType expr) && isPointerType target = -- TODO: should really only allow this for null pointer constants
    return (convertExprType expr target)
  | otherwise =
    Err "Semantics Error: cannot convert type for assignment"
*/


// ------------------------- Symbol Table Functions ------------------------- //

struct SymbolTable* create_symbol_table(size_t numBuckets){
  struct SymbolTable* table = arena_alloc(sizeof(struct SymbolTable));
  table->size = numBuckets;
  table->arr = arena_alloc(sizeof(struct SymbolEntry*) * numBuckets);
  for (size_t i = 0; i < numBuckets; i++){
    table->arr[i] = NULL;
  }
  return table;
}

void symbol_table_insert(struct SymbolTable* hmap, struct Slice* key, struct Type* type, struct IdentAttr* attrs){
  size_t label = hash_slice(key) % hmap->size;
  
  struct SymbolEntry* newEntry = arena_alloc(sizeof(struct SymbolEntry));
  newEntry->key = key;
  newEntry->type = type;
  newEntry->attrs = attrs;
  newEntry->next = NULL;

  if (hmap->arr[label] == NULL){
    hmap->arr[label] = newEntry;
  } else {
    struct SymbolEntry* cur = hmap->arr[label];
    while (cur->next != NULL){
      cur = cur->next;
    }
    cur->next = newEntry;
  }
}

struct SymbolEntry* symbol_table_get(struct SymbolTable* hmap, struct Slice* key){
  size_t label = hash_slice(key) % hmap->size;

  struct SymbolEntry* cur = hmap->arr[label];
  while (cur != NULL){
    if (compare_slice_to_slice(cur->key, key)){
      return cur;
    }
    cur = cur->next;
  }
  return NULL;
}

bool symbol_table_contains(struct SymbolTable* hmap, struct Slice* key){
  size_t label = hash_slice(key) % hmap->size;

  struct SymbolEntry* cur = hmap->arr[label];
  while (cur != NULL){
    if (compare_slice_to_slice(cur->key, key)){
      return true;
    }
    cur = cur->next;
  }
  return false;
}

