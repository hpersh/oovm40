%{

#include <assert.h>

#include "ovmc.h"
#include "grammar.tab.h"

void scanner_infile_print(FILE *fp)
{
  if (infile_cur == 0) {
    fprintf(fp, "No input file!");
    return;
  }

  fprintf(fp, "file %s line %u", infile_cur->filename, infile_cur->line_num);
}

const char *scanner_infile_cur_file(void)
{
  return (infile_cur->filename);
}
 
unsigned scanner_infile_cur_line(void)
{
  return (infile_cur->line_num);
}
 
void scanner_include(const char *filename)
{
  FILE *fp = include_file_open(filename);
  if (fp == 0)  return;

  yyin = fp;
  struct infile *p = (struct infile *) malloc(sizeof(*p));
  p->filename = strdup(filename);
  p->fp = fp;
  p->line_num = 1;
  p->prev = infile_cur;
  infile_cur = p;
  
  yypush_buffer_state(yy_create_buffer(yyin, YY_BUF_SIZE));
}

void scanner_infile_pop(void)
{
  include_file_pop();
  struct infile *p = infile_cur;
  infile_cur = p->prev;
  if (p->fp != 0)  fclose(p->fp);
  free((char *) p->filename);
  free(p);
}
 
void scanner_infile_init(const char *filename)
{
  infile_cur = (struct infile *) malloc(sizeof(*infile_cur));
  infile_cur->prev = 0;
  infile_cur->filename = strdup(filename);
  infile_cur->fp = 0;
  infile_cur->line_num = 1;
}


%}

WHITE           [ \t]+
DIGIT		[0-9]
XDIGIT          [0-9A-Fa-f]
STRING_CHAR	"\\\""|[^\"]
STRING		"\""{STRING_CHAR}*"\""
SYMBOL		"#"?[_A-Za-z][_\-A-Za-z0-9]*

%%

<<EOF>> {
  scanner_infile_pop();
  yypop_buffer_state();
  if (!YY_CURRENT_BUFFER)   yyterminate();
}
"\n"            { ++infile_cur->line_num; }
"//".*          /* Eat up comments */
{WHITE}         /* Eat up whitespace */
{STRING}        { return (TOK_STR); }
"-"?{DIGIT}+"."{DIGIT}*([Ee]"-"?{DIGIT}+)? { return (TOK_FLOAT); }
"-"?{DIGIT}+	{ return (TOK_INT); }
"0"[Xx]{XDIGIT}+ { return (TOK_INT); }
"!"             { return (TOK_BANG); }
"+"             { return (TOK_PLUS); }
"-"             { return (TOK_SUB); }
"*"             { return (TOK_MULT); }
"/"             { return (TOK_DIV); }
"%"             { return (TOK_MOD); }
"=="            { return (TOK_DBL_EQUAL); }
"!="            { return (TOK_NOT_EQUAL); }
"||"            { return (TOK_LOR); }
"&&"            { return (TOK_LAND); }
"."             { return (TOK_DOT); }
","             { return (TOK_COMMA); }
":"             { return (TOK_COLON); }
"?"             { return (TOK_QUEST); }
"`"		{ return (TOK_LIT); }
"<<"            { return (TOK_LSH); }
"<"             { return (TOK_LANG); }
"<="            { return (TOK_LE); }
">>"            { return (TOK_RSH); }
">"             { return (TOK_RANG); }
">="            { return (TOK_GE); }
"^"             { return (TOK_CARET); }
"#"             { return (TOK_HASH); }
"|"             { return (TOK_OR); }
"&"             { return (TOK_AND); }
"["             { return (TOK_LSQ); }
"]"             { return (TOK_RSQ); }
"{"             { return (TOK_LBR); }
"}"             { return (TOK_RBR); }
"("             { return (TOK_LPAR); }
")"             { return (TOK_RPAR); }
"+="            { return (TOK_PLUSEQUAL); }
"-="            { return (TOK_SUBEQUAL); }
"*="            { return (TOK_MULTEQUAL); }
"/="            { return (TOK_DIVEQUAL); }
"%="            { return (TOK_MODEQUAL); }
"&="            { return (TOK_ANDEQUAL); }
"|="            { return (TOK_OREQUAL); }
"="             { return (TOK_EQUAL); }
";"             { return (TOK_SEMIC); }
"#nil"          { return (TOK_NIL); }
"#true"         { return (TOK_BOOL); }
"#false"        { return (TOK_BOOL); }
"in"            { return (TOK_IN); }
"if"            { return (TOK_IF); }
"ifnot"         { return (TOK_IFNOT); }
"else"          { return (TOK_ELSE); }
"cond"		{ return (TOK_COND); }
"loop"          { return (TOK_LOOP); }
"while"         { return (TOK_WHILE); }
"until"         { return (TOK_UNTIL); }
"for"		{ return (TOK_FOR); }
"break"         { return (TOK_BREAK); }
"continue"      { return (TOK_CONTINUE); }
"return"        { return (TOK_RETURN); }
"raise"         { return (TOK_RAISE); }
"reraise"       { return (TOK_RERAISE); }
"try"           { return (TOK_TRY); }
"catch"         { return (TOK_CATCH); }
"any"           { return (TOK_ANY); }
"none"          { return (TOK_NONE); }
"var"           { return (TOK_VAR); }
"@include"{WHITE}{STRING} {
    char *p = index(yytext, '"'), *q = index(p + 1, '"');
    assert(p != 0);
    assert(q != 0);
    *q = 0;
    scanner_include(p + 1);
}
"@file"         { return (TOK_AFILE); }
"@line"         { return (TOK_ALINE); }
"@lit"          { return (TOK_LIT); }
"@const"        { return (TOK_CONST); }
"@enum"         { return (TOK_ENUM); }
"@define"       { return (TOK_DEF); }
"@defined"      { return (TOK_DEFINED); }
"@int"          { return (TOK_TOINT); }
"@float"        { return (TOK_TOFLOAT); }
"@str"          { return (TOK_TOSTR); }
"@anon"         { return (TOK_ANON); }
"@module"       { return (TOK_MODULE); }
"@func"         { return (TOK_FUNC); }
"@recurse"      { return (TOK_RECURSE); }
"@classmethod"  { return (TOK_CL_METHOD); }
"@classvariables"  { return (TOK_CL_VARS); }
"@method"       { return (TOK_METHOD); }
"@super"        { return (TOK_SUPER); }
"@namespace"    { return (TOK_NS); }
"@class"        { return (TOK_CLASS); }
"@interface"    { return (TOK_IFACE); }
"@parent"       { return (TOK_PARENT); }
"@implements"   { return (TOK_IMPL); }
{SYMBOL}        { return (TOK_SYM); } 
. {
  fprintf(stderr, "Unrecognized character: %s, ", yytext);
  scanner_infile_print(stderr);
  fputs("\n", stderr);
  exit(1);
}
