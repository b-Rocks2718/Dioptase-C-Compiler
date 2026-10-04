/* A comment is replaced by one space (C11 5.1.1.2 phase 3), so a block comment
   spanning lines does not end the directive it appears in (C11 6.10.3.5 EXAMPLE 6). */
#define OBJ_LIKE /* white space */ (1-1) /* other */
#define FUNC_LIKE( a )( /* note the white space */ \
 a /* other stuff on this line
 */ )
#define AFTER /* starts here
   and ends here */ 42
FUNC_LIKE(3) OBJ_LIKE AFTER
int line = __LINE__;
