/***************************************************************************
 *
 * Bison grammar
 *
 * - Build an XML parse tree of the source file
 *
 ***************************************************************************/

%{

#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "ovmc.h"
  
char *module_name;
  
#define YYSTYPE  xmlNodePtr

extern char *yytext;
extern int  yyleng;

int yylex();			/* Fix for tool issue */

#define NIL  ((void *) 0)

#define XML_STR(s)  ((xmlChar *)(s))
 
xmlNodePtr xml_new_node(const char *nm)
{
  xmlNodePtr result = xmlNewNode(0, XML_STR(nm));
  char buf[32];
  snprintf(buf, sizeof(buf), "%u", infile_cur->line_num);
  xmlNewProp(result, XML_STR("line"), XML_STR(buf));
  return (result);
}

xmlNodePtr xml_new_node_with_children(const char *nm, ...)
{
  va_list ap;
  va_start(ap, nm);

  xmlNodePtr result = xml_new_node(nm);
  for (;;) {
    xmlNodePtr ch = va_arg(ap, xmlNodePtr);
    if (ch == 0)  break;
    xmlAddChild(result, ch);
  }

  va_end(ap);
  
  return (result);
}
 
xmlNodePtr xml_new_node_with_attrs(char *nm, ...)
{
  va_list ap;
  va_start(ap, nm);

  xmlNodePtr result = xml_new_node(nm);
  for (;;) {
    char *attrnm = va_arg(ap, char *);
    if (attrnm == 0)  break;
    xmlNewProp(result, XML_STR(attrnm), XML_STR(va_arg(ap, char *)));
  }

  va_end(ap);
  
  return (result);
}

xmlNodePtr xml_sym(const char *s)
{
  return (xml_new_node_with_attrs("sym", "val", s, NIL));
}

xmlNodePtr xml_method_call(xmlNodePtr recvr, const char *sel, ...)
{
  xmlNodePtr li = xml_new_node("exprlist");
  va_list ap;
  va_start(ap, sel);
  for (;;) {
    xmlNodePtr x = va_arg(ap, xmlNodePtr);
    if (x == 0)  break;
    xmlAddChild(li, x);
  }
  va_end(ap);
  return (xml_new_node_with_children("methodcall", recvr, xml_sym(sel), li, NIL)
	  );
}
 
char *xml_get_prop(xmlNodePtr nd, char *prop)
{
  return ((char *) xmlGetProp(nd, XML_STR(prop)));
}

int xml_strcmp(const xmlChar *s1, char *s2)
{
  return (xmlStrcmp(s1, XML_STR(s2)));
}

xmlNodePtr xml_copy_node(xmlNodePtr nd)
{
  return (xmlCopyNode(nd, 1));
}

%}

/* "Outer" language */
%nonassoc TOK_MODULE
%nonassoc TOK_IFACE TOK_CLASS TOK_NS
%nonassoc TOK_PARENT TOK_IMPL
%nonassoc TOK_CL_METHOD TOK_CL_VARS TOK_METHOD TOK_SUPER TOK_FUNC

/* Statements */
%nonassoc TOK_SEMIC
%nonassoc TOK_COND TOK_IF TOK_IFNOT TOK_ELSE
%nonassoc TOK_LOOP TOK_WHILE TOK_UNTIL TOK_FOR
%nonassoc TOK_BREAK TOK_CONTINUE
%nonassoc TOK_RETURN
%nonassoc TOK_RAISE TOK_RERAISE
%nonassoc TOK_TRY TOK_CATCH TOK_ANY TOK_NONE
%nonassoc TOK_VAR

/* Separators  */
%left     TOK_COMMA
%nonassoc TOK_COLON

/* Operators */
%nonassoc TOK_DEFINED TOK_TOINT TOK_TOFLOAT TOK_TOSTR
%nonassoc TOK_ANON
%nonassoc TOK_PLUSEQUAL TOK_SUBEQUAL TOK_MULTEQUAL TOK_DIVEQUAL TOK_MODEQUAL TOK_ANDEQUAL TOK_OREQUAL TOK_EQUAL
%left     TOK_QUEST
%left     TOK_LOR
%left     TOK_LAND
%left     TOK_IN
%left     TOK_OR
%left     TOK_CARET
%left     TOK_AND
%left     TOK_DBL_EQUAL TOK_NOT_EQUAL
%left     TOK_LANG TOK_RANG TOK_LE TOK_GE
%left     TOK_LSH TOK_RSH
%left     TOK_PLUS TOK_SUB
%left     TOK_MULT TOK_DIV TOK_MOD
%nonassoc TOK_BANG
%left     TOK_DOT

/* Collections */
/* %left     TOK_LANG TOK_RANG */
%left     TOK_LSQ TOK_RSQ
%left     TOK_LBR TOK_RBR
%left     TOK_LPAR TOK_RPAR
%nonassoc TOK_ENUM TOK_DEF
%nonassoc TOK_LIT TOK_AFILE TOK_ALINE TOK_CONST TOK_HASH  /* Complex literal marker */

/* Terminals */
%nonassoc TOK_NIL
%nonassoc TOK_BOOL
%nonassoc TOK_INT
%nonassoc TOK_FLOAT
%nonassoc TOK_SYM
%nonassoc TOK_STR
%nonassoc TOK_RECURSE

%start module

%%

sym:
	TOK_SYM
	{
	  $$ = xml_sym(yytext);
	}
	;

sym_list1:
	sym
	{
	  $$ = xml_new_node_with_children("symlist", $1, NIL);
	}
	| sym_list1 TOK_COMMA sym
	{
	  xmlAddChild($1, $3);
	}
	;

lit_pair:
	TOK_LANG expr TOK_COMMA expr TOK_RANG
	{
	  $$ = xml_new_node_with_children("pair", $2, $4, NIL);
	}
	;

lit_list:
	TOK_LPAR expr_list TOK_RPAR
	{
	  $$ = xml_new_node_with_children("list", $2, NIL);
	}
	;

lit_array:
	TOK_LSQ expr_list TOK_RSQ
	{
	  $$ = xml_new_node_with_children("array", $2, NIL);
	}
	;

lit_set:
	TOK_LBR expr_list1 TOK_RBR
	{
	  $$ = xml_new_node_with_children("set", $2, NIL);
	}
	;

dict_item:
	expr TOK_COLON expr
	{
	  $$ = xml_new_node_with_children("dictitem", $1, $3, NIL);
	}
	;

dict_items1:
	dict_item
	{
	  $$ = xml_new_node_with_children("dictitems", $1, NIL);
	}
	| dict_items1 TOK_COMMA dict_item
	{
	  xmlAddChild($1, $3);
	}
	;

dict_items:
	/* empty */
	{
	  $$ = xml_new_node("dictitems");
	}
	| dict_items1
	;

lit_dict:
	TOK_LBR dict_items TOK_RBR
	{
	  $$ = xml_new_node_with_children("dict", $2, NIL);
	}
	;

lit_item:
	lit_dict
	| lit_set
	| lit_array
	| lit_list
	| lit_pair
	;

lit:
	TOK_LIT lit_item
	{
	  $$ = $2;
	}
	;

const_lit:
	TOK_CONST lit_item
	{
	  $$ = xml_new_node_with_children("constlit", $2, NIL);
	}
        | TOK_HASH lit_item
	{
	  $$ = xml_new_node_with_children("constlit", $2, NIL);
	}
	;

objname:
	sym
	| objname TOK_DOT sym
	{
	  char *s1 = xml_get_prop($1, "name"), *s2 = xml_get_prop($3, "name");
	  unsigned sz = strlen(s1) + 1 + strlen(s2) + 1;
	  char buf[sz];
	  snprintf(buf, sz, "%s.%s", s1, s2);
	  xmlFreeNode($1);
	  xmlFreeNode($3);
	  $$ = xml_sym(buf);
	}
	;

obj:
	sym
	{
	  $$ = xml_new_node_with_children("obj1", $1, NIL);
	}
	| expr TOK_DOT sym
	{
	  $$ = xml_new_node_with_children("obj2e", $1, $3, NIL);
	}
	| expr TOK_LSQ expr TOK_RSQ
	{
	  $$ = xml_new_node_with_children("obj2", $1, $3, NIL);
	}
	;

expr_list1:
	expr
	{
	  $$ = xml_new_node_with_children("exprlist", $1, NIL);
	}
	| expr_list1 TOK_COMMA expr
	{
	  xmlAddChild($1, $3);
	}
	;

expr_list:
	/* empty */
	{
	  $$ = xml_new_node("exprlist");
	}
	| expr_list1
	;

sel:
	TOK_IN
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_IF
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_IFNOT
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_ELSE
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_COND
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_LOOP
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_WHILE
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_UNTIL
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_FOR
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_BREAK
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_CONTINUE
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_RETURN
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_RAISE
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_RERAISE
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_TRY
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_CATCH
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_ANY
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_NONE
	{
	  $$ = xml_sym(yytext);
	}
	| TOK_VAR
	{
	  $$ = xml_sym(yytext);
	}
	| sym
	;

method_call:
	TOK_SUPER TOK_DOT sel TOK_LPAR expr_list TOK_RPAR
	{
	  $$ = xml_new_node_with_children("methodcall",
					  xml_method_call(xml_method_call(xml_new_node("parent"), "methods", NIL),
							  "ate",
							  xml_new_node_with_attrs("str", "val", xml_get_prop($3, "val"), NIL),
							  NIL
							  ),
					  xml_sym("call"),
					  $5,
					  NIL
					  );
	}
	| expr TOK_DOT sel TOK_LPAR expr_list TOK_RPAR
	{
	  $$ = xml_new_node_with_children("methodcall", $1, $3, $5, NIL);
	}
	;

str:
	TOK_STR
	{
	  yytext[strlen(yytext) - 1] = 0;
	  $$ = xml_new_node_with_attrs("str", "val", yytext + 1, NIL);
	}
	;

method_array_arg:
	sym TOK_LSQ TOK_RSQ
	{
	  $$ = xml_new_node_with_children("methodarrayarg", $1, NIL);
	}
	;

method_arg_list2:
	sym
	{
	  $$ = xml_new_node_with_children("methodarglist", $1, NIL);
	}
	| method_arg_list2 TOK_COMMA sym
	{
	  xmlAddChild($1, $3);
	}
	;

method_arg_list1:
	/* empty */
	{
	  $$ = xml_new_node("methodarglist");
	}
	| method_arg_list2
	;

method_arg_list:
	method_array_arg
	| method_arg_list2 TOK_COMMA method_array_arg
	{
	  xmlAddChild($1, $3);
	}
	| method_arg_list1
	;

anon:
	TOK_ANON TOK_LPAR method_arg_list TOK_RPAR block
	{
	  $$ = xml_new_node_with_children("anon", $3, $5, NIL);
	}
	;

enum_item:
	sym TOK_EQUAL expr
	{
	  $$ = xml_new_node_with_children("enumassign", $1, $3, NIL);
	}
	| sym
	;

enum_list1:
	enum_list1 TOK_COMMA enum_item
	{
	  xmlAddChild($1, $3);
	}
	| enum_item
	{
	  $$ = xml_new_node_with_children("enum", $1, NIL);
	}
	;

enum_list:
	/* empty */
	{
	  $$ = xml_new_node("enum");
	}
	| enum_list1
	;

enum_stmt:
	TOK_ENUM TOK_LBR enum_list TOK_RBR
	{
	  $$ = $3;
	}
	;

const_item:
	sym TOK_EQUAL expr
	{
	  $$ = xml_new_node_with_children("constassign", $1, $3, NIL);
	}
	;

const_list1:
	const_list1 TOK_COMMA const_item
	{
	  xmlAddChild($1, $3);
	}
	| const_item
	{
	  $$ = xml_new_node_with_children("def", $1, NIL);
	}
	;

const_list:
	/* empty */
	{
	  $$ = xml_new_node("def");
	}
	| const_list1
	;

const_stmt:
	TOK_DEF TOK_LBR const_list TOK_RBR
	{
	  $$ = $3;
	}
	;

expr:
	TOK_LPAR expr TOK_RPAR
	{
	  $$ = $2;
	}
	| TOK_AFILE
	{
	  $$ = xml_new_node_with_attrs("str", "val", scanner_infile_cur_file(), 0);
	}
	| TOK_ALINE
	{
	  $$ = xml_new_node("atline");
	}
        | TOK_MODULE
	{
	  $$ = xml_new_node("atmodule");
	}
        | TOK_NS
	{
	  $$ = xml_new_node("atns");
	}
	| TOK_CLASS
	{
	  $$ = xml_new_node("atclass");
	}
	| TOK_METHOD
	{
	  $$ = xml_new_node("atmethod");
	}
	| TOK_PARENT
	{
	  $$ = xml_new_node("parent");
	}
	| TOK_RECURSE
	{
	  $$ = xml_new_node("recurse");
	}
	| TOK_NIL
	{
	  $$ = xml_new_node("nil");
	}
	| TOK_BOOL
	{
	  $$ = xml_new_node_with_attrs("bool", "val", yytext, NIL);
	}
	| TOK_INT
	{
	  $$ = xml_new_node_with_attrs("int", "val", yytext, NIL);
	}
	| TOK_FLOAT
	{
	  $$ = xml_new_node_with_attrs("float", "val", yytext, NIL);
	}
	| str
        | lit
	| const_lit
	| obj
	| method_call
	| TOK_DEFINED TOK_LPAR objname TOK_RPAR
	{
	  $$ = xml_new_node_with_children("atdefined", $3, NIL);
	}
	| TOK_TOINT TOK_LPAR expr TOK_RPAR
	{
	  $$ = xml_new_node_with_children("atint", $3, NIL);
	}
	| TOK_TOFLOAT TOK_LPAR expr TOK_RPAR
	{
	  $$ = xml_new_node_with_children("atfloat", $3, NIL);
	}
	| TOK_TOSTR TOK_LPAR expr TOK_RPAR
	{
	  $$ = xml_new_node_with_children("atstr", $3, NIL);
	}
	| TOK_BANG expr
	{
	  $$ = xml_new_node_with_children("lnot", $2, NIL);
	}
	| TOK_SUB expr
	{
	  $$ = xml_new_node_with_children("minus", $2, NIL);
	}
	| expr TOK_PLUS expr
	{
	  $$ = xml_new_node_with_children("add", $1, $3, NIL);
	}
	| expr TOK_SUB expr
	{
	  $$ = xml_new_node_with_children("sub", $1, $3, NIL);
	}
	| expr TOK_MULT expr
	{
	  $$ = xml_new_node_with_children("mul", $1, $3, NIL);
	}
	| expr TOK_DIV expr
	{
	  $$ = xml_new_node_with_children("div", $1, $3, NIL);
	}
	| expr TOK_MOD expr
	{
	  $$ = xml_new_node_with_children("mod", $1, $3, NIL);
	}
	| expr TOK_LANG expr
	{
	  $$ = xml_new_node_with_children("lt", $1, $3, NIL);
	}
	| expr TOK_RANG expr
	{
	  $$ = xml_new_node_with_children("gt", $1, $3, NIL);
	}
	| expr TOK_LE expr
	{
	  $$ = xml_new_node_with_children("le", $1, $3, NIL);
	}
	| expr TOK_GE expr
	{
	  $$ = xml_new_node_with_children("ge", $1, $3, NIL);
	}
	| expr TOK_DBL_EQUAL expr
	{
	  $$ = xml_new_node_with_children("equal", $1, $3, NIL);
	}
	| expr TOK_NOT_EQUAL expr
	{
	  $$ = xml_new_node_with_children("notequal", $1, $3, NIL);
	}
	| expr TOK_IN expr
	{
	  $$ = xml_new_node_with_children("in", $1, $3, NIL);
	}
	| expr TOK_AND expr
	{
	  $$ = xml_new_node_with_children("band", $1, $3, NIL);
	}
	| expr TOK_OR expr
	{
	  $$ = xml_new_node_with_children("bor", $1, $3, NIL);
	}
	| expr TOK_CARET expr
	{
	  $$ = xml_new_node_with_children("bxor", $1, $3, NIL);
	}
	| expr TOK_LAND expr
	{
	  $$ = xml_new_node_with_children("land", $1, $3, NIL);
	}
	| expr TOK_LOR expr
	{
	  $$ = xml_new_node_with_children("lor", $1, $3, NIL);
	}
	| expr TOK_QUEST expr TOK_COLON expr
	{
	  $$ = xml_new_node_with_children("condexpr", $1, $3, $5, NIL);
	}
	| anon
	;

assign_stmt:
	obj TOK_PLUSEQUAL expr
	{
	  $$ = xml_new_node_with_children("assign",
					  xml_copy_node($1),
					  xml_new_node_with_children("add", $1, $3, NIL),
					  NIL
					  );
	}
	| obj TOK_SUBEQUAL expr
	{
	  $$ = xml_new_node_with_children("assign",
					  xml_copy_node($1),
					  xml_new_node_with_children("sub", $1, $3, NIL),
					  NIL
					  );
	}
	| obj TOK_MULTEQUAL expr
	{
	  $$ = xml_new_node_with_children("assign",
					  xml_copy_node($1),
					  xml_new_node_with_children("mul", $1, $3, NIL),
					  NIL
					  );
	}
	| obj TOK_DIVEQUAL expr
	{
	  $$ = xml_new_node_with_children("assign",
					  xml_copy_node($1),
					  xml_new_node_with_children("div", $1, $3, NIL),
					  NIL
					  );
	}
	| obj TOK_MODEQUAL expr
	{
	  $$ = xml_new_node_with_children("assign",
					  xml_copy_node($1),
					  xml_new_node_with_children("mod", $1, $3, NIL),
					  NIL
					  );
	}
	| obj TOK_ANDEQUAL expr
	{
	  $$ = xml_new_node_with_children("assign",
					  xml_copy_node($1),
					  xml_new_node_with_children("band", $1, $3, NIL),
					  NIL
					  );
	}
	| obj TOK_OREQUAL expr
	{
	  $$ = xml_new_node_with_children("assign",
					  xml_copy_node($1),
					  xml_new_node_with_children("bor", $1, $3, NIL),
					  NIL
					  );
	}
	| obj TOK_EQUAL expr
	{
	  $$ = xml_new_node_with_children("assign", $1, $3, NIL);
	}
	;

return_stmt:
	TOK_RETURN TOK_LPAR expr TOK_RPAR
	{
	  $$ = xml_new_node_with_children("return", $3, NIL);
	}
	| TOK_RETURN
	{
	  $$ = xml_new_node("return");
	}	
	;

raise_stmt:
        TOK_RAISE expr
	{
	  $$ = xml_new_node_with_children("raise", $2, NIL);
	}
	;

if_or_not:
	TOK_IF
	{
	  $$ = xml_new_node("if");
	}
	| TOK_IFNOT
	{
	  $$ = xml_new_node("ifnot");
	}
	;

if_stmt:
	if_or_not TOK_LPAR expr TOK_RPAR block TOK_ELSE block
	{
	  xmlAddChild($1, $3);
	  xmlAddChild($1, $5);
	  xmlAddChild($1, $7);
	}
	| if_or_not TOK_LPAR expr TOK_RPAR block
	{
	  xmlAddChild($1, $3);
	  xmlAddChild($1, $5);
	}
	;

cond_stmt:
	TOK_COND block
	{
	  $$ = xml_new_node_with_children("cond", $2, NIL);
	}
	;

loop_stmt:
	TOK_LOOP block
	{
	  $$ = xml_new_node_with_children("loop", $2, NIL);
	}
	;

while_stmt:
	TOK_WHILE TOK_LPAR expr TOK_RPAR block
	{
	  $$ = xml_new_node_with_children("while", $3, $5, NIL);
	}
	;

until_stmt:
	TOK_UNTIL TOK_LPAR expr TOK_RPAR block
	{
	  $$ = xml_new_node_with_children("until", $3, $5, NIL);
	}
	;

// Put "for" statement in "fake" block, to limit scope of iterator var
for_stmt:
	TOK_FOR sym TOK_LPAR expr TOK_RPAR block
	{
	  $$ = xml_new_node_with_children("block",
					  xml_new_node_with_children("for", $2, $4, $6, NIL),
					  NIL
					  );
	}
	;

catch:
	TOK_CATCH TOK_LPAR expr TOK_RPAR block
	{
	  $$ = xml_new_node_with_children("catch", $3, $5, NIL);
	}
	| TOK_CATCH block
	{
	  $$ = xml_new_node_with_children("catch", $2, NIL);
	}

catch_list:
	catch
	{
	  $$ = xml_new_node_with_children("catchlist", $1, NIL);
	}
	| catch_list catch
	{
	  xmlAddChild($1, $2);
	}
	;

// Put "try" statement in a "fake" block, to limit scope of catch var
try_stmt:
	TOK_TRY TOK_LPAR sym TOK_RPAR block catch_list TOK_ANY block TOK_NONE block
	{
	  $$ = xml_new_node_with_children("block",
					  xml_new_node_with_children("tryanynone", $3, $5, $6, $8, $10, NIL),
					  NIL
					  );
	}
	| TOK_TRY TOK_LPAR sym TOK_RPAR block catch_list TOK_ANY block
	{
	  $$ = xml_new_node_with_children("block",
					 xml_new_node_with_children("tryany", $3, $5, $6, $8, NIL),
					 NIL
					 );
	}
	| TOK_TRY TOK_LPAR sym TOK_RPAR block catch_list TOK_NONE block
	{
	  $$ = xml_new_node_with_children("block",
					  xml_new_node_with_children("trynone", $3, $5, $6, $8, NIL),
					  NIL
					  );
	}
	| TOK_TRY TOK_LPAR sym TOK_RPAR block catch_list
	{
	  $$ = xml_new_node_with_children("block",
					  xml_new_node_with_children("try", $3, $5, $6, NIL),
					  NIL
					  );
	}
	;

var_item:
	sym TOK_EQUAL expr
	{
	  $$ = xml_new_node_with_children("assign", xml_new_node_with_children("obj1", $1, NIL), $3, NIL);
	}
	| sym
	;

var_list:
	var_list TOK_COMMA var_item
	{
	  xmlAddChild($1, $3);
	}
	| var_item
	{
	  $$ = xml_new_node_with_children("var", $1, NIL);
	}
	;

var_stmt:
	TOK_VAR var_list
	{
	  $$ = $2;
	}

break_stmt:
	TOK_BREAK TOK_INT
	{
	  $$ = xml_new_node_with_attrs("break", "val", yytext, NIL);
	}
	| TOK_BREAK
	{
	  $$ = xml_new_node_with_attrs("break", "val", "1", NIL);
	}
	;

func_decl:
    TOK_FUNC sym TOK_LPAR method_arg_list TOK_RPAR block
	{
		xmlNodePtr obj1 = xml_new_node_with_children("obj1", xmlCopyNode($2, 1), NIL);
		xmlNodePtr nd = xml_new_node_with_children("methoddecl", $2, $4, NIL);
		nd = xml_new_node_with_children("func", nd, $6, NIL);
		$$ = xml_new_node_with_children("assign", obj1, nd, NIL);
	}
    ;

stmt1:
	break_stmt
	| TOK_CONTINUE
	{
	  $$ = xml_new_node("continue");
	}
	| return_stmt
	| assign_stmt
	| raise_stmt
	| TOK_RERAISE
	{
	  $$ = xml_new_node("reraise");
	}
	| var_stmt
	| method_call
	| enum_stmt
	| const_stmt
	;

stmt:
	stmt1 TOK_SEMIC
	| if_stmt
	| cond_stmt
	| loop_stmt
	| while_stmt
	| until_stmt
	| for_stmt
	| try_stmt
	| block
	| func_decl
	;

stmt_list1:
	stmt
	{
	  xmlNodePtr nd = xml_new_node("block");
	  if ($1 != 0)  xmlAddChild(nd, $1);
	  $$ = nd;
	}
	| stmt_list1 stmt
	{
	  xmlAddChild($1, $2);
	}
	;

stmt_list:
	/* empty */
	{
	  $$ = xml_new_node("block");
	}
	| stmt_list1
	;

block:
	TOK_LBR stmt_list TOK_RBR
	{
	  $$ = $2;
	}
	;

method_decl:
	TOK_METHOD sym TOK_LPAR method_arg_list TOK_RPAR
	{
	  $$ = xml_new_node_with_children("methoddecl", $2, $4, NIL);
	}
	;

method:
	method_decl block
	{
	  $$ = xml_new_node_with_children("method", $1, $2, NIL);
	}
	;

cl_method_decl:
	TOK_CL_METHOD sym TOK_LPAR method_arg_list TOK_RPAR
	{
	  $$ = xml_new_node_with_children("clmethoddecl", $2, $4, NIL);
	}
	;

cl_method:
	cl_method_decl block
	{
	  $$ = xml_new_node_with_children("clmethod", $1, $2, NIL);
	}
	;

mstmt:
	stmt
	;

cl_item:
	assign_stmt TOK_SEMIC
	| cl_method
	| method
	;

cl_body1:
	cl_item
	{
	  $$ = xml_new_node_with_children("clbody", $1, NIL);
	}
	| cl_body1 cl_item
	{
	  xmlAddChild($1, $2);
	}
	;

cl_body:
	/* empty */
	{
	  $$ = xml_new_node("clbody");
	}
	| cl_body1
	;

iface_stmt:
	TOK_CL_VARS sym_list1 TOK_SEMIC
	{
	  $$ = xml_new_node_with_children("clvars", $2, NIL);
	}
	| cl_method_decl TOK_SEMIC
	| method_decl TOK_SEMIC
	;

iface_body1:
	iface_stmt
	{
	  $$ = xml_new_node_with_children("ifacebody", $1, NIL);
	}
	| iface_body1 iface_stmt
	{
	  xmlAddChild($1, $2);
	}
	;

iface_body:
	/* empty */
	{
	  $$ = xml_new_node("ifacebody");
	}
	| iface_body1
	;

iface_parents:
	/* empty */
	{
	  $$ = xml_new_node("symlist");
	}
	| TOK_PARENT sym_list1
	{
	  $$ = $2;
	}
	;

iface:
	TOK_IFACE sym iface_parents TOK_LBR iface_body TOK_RBR
	{
	  $$ = xml_new_node_with_children("iface", $2, $3, $5, NIL);
	}
	| TOK_IFACE sym iface_parents TOK_SEMIC
	{
	  $$ = xml_new_node_with_children("iface", $2, $3, NIL);
	}

cl_ifaces:
	/* empty */
	{
	  $$ = xml_new_node("symlist");
	}
	| TOK_IMPL sym_list1
	{
	  $$ = $2;
	}
	;

class:
	TOK_CLASS sym TOK_PARENT expr cl_ifaces TOK_LBR cl_body TOK_RBR
	{
	  $$ = xml_new_node_with_children("class", $2, $4, $5, $7, NIL);
	}
	| TOK_CLASS sym cl_ifaces TOK_LBR cl_body TOK_RBR
	{
	  $$ = xml_new_node_with_children("class",
					  $2,
					  xml_new_node_with_children("obj1",
								     xml_sym("#Object"),
								     NIL
								     ),
					  $3,
					  $5,
					  NIL
					  );
	}
	;

ns_stmt:
	mstmt
	| namespace
	| class
	;

ns_stmts1:
	ns_stmt
	{
	  $$ = xml_new_node_with_children("nsstmts", $1, NIL);
	}
	| ns_stmts1 ns_stmt
	{
	  xmlAddChild($1, $2);
	}
	;

ns_stmts:
	/* empty */
	{
	  $$ = xml_new_node("nsstmts");
	}
	| ns_stmts1
	;

namespace:
	TOK_NS sym TOK_LBR ns_stmts TOK_RBR
	{
	  $$ = xml_new_node_with_children("namespace", $2, $4, NIL);
	}
	;

module_stmt:
	mstmt
	| namespace
	| class
	| iface
	;

module1:
	module_stmt
	{
	  xmlNodePtr nd = xml_new_node_with_children("module", $1, NIL);
	  xmlNewProp(nd, XML_STR("val"), XML_STR(module_name));
	  $$ = nd;
	}
	| module1 module_stmt
	{
	  if ($2 != 0)  xmlAddChild($1, $2);
	}
	;

module:
	/* empty */
	| module1
	{
	  root = $1;
	}
