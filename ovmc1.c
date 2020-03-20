#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>
#include "ovmc.h"
#include "oovm_hash.h"


int yywrap()
{
  return (1);
}

int yyerror()
{
  fprintf(stderr, "Syntax error, ");
  scanner_infile_print(stderr);
  fputs("\n", stderr);

  exit(1);
}

int yydebug = 0;
extern int yy_flex_debug;

extern FILE *yyin;

std::list<char *> include_path_list;

std::list<std::string> include_list;

FILE *include_file_open(const char *filename)
{
  std::string s (filename);

  {
    for (auto inc : include_list) {
      if (inc == s) {
	fprintf(stderr, "Include loop detected for file %s, aborting\n", filename);
	exit(1);
      }
    }
  }
  {
    for (auto incpath : include_path_list) {
      std::string ss = std::string (incpath) + std::string ("/") + s;
      FILE *fp = fopen(ss.c_str(), "r");
      if (fp == 0)  continue;
      include_list.push_back(s);
      return (fp);
    }
  }

  fprintf(stderr, "Include file %s not found, aborting\n", filename);
  exit(1);
}

void include_file_pop(void)
{
  if (!include_list.empty())  include_list.pop_back();
}

int main(int argc, char **argv)
{
  const char *progname = *argv;
  ++argv;  --argc;

  yy_flex_debug = yydebug = 0;  
  
  while (argc > 1 && argv[0][0] == '-') {
    if (strcmp(argv[0], "-d") == 0) {
      yy_flex_debug = yydebug = 1;
      ++argv;  --argc;
      continue;
    }

    if (strcmp(argv[0], "-I") == 0) {
      if (argc < 2)  goto usage;
      include_path_list.push_back(argv[1]);
      argv += 2;  argc -= 2;
      continue;
    }
    
    if (strcmp(argv[0], "-m") == 0) {
      if (argc < 2)  goto usage;
      module_name = argv[1];
      argv += 2;  argc -= 2;
      continue;
    }

#if 0
    if (strcmp(argv[0], "-D") == 0) {
      if (argc < 2)  goto usage;
      char *p = index(argv[1], '=');
      if (p == 0)  goto usage;
      *p = 0;
      std::string s (argv[1]);
      expr_val val;
      val.type = expr_val::TYPE_STR;
      val.strval = std::string (p + 1);
      ns_const_insert(s, val);
      argv += 2;  argc -= 2;
      continue;
    }
#endif
    
    if (strcmp(argv[0], "-O") == 0) {
      optim = true;
      ++argv;  --argc;
      continue;
    }

    goto usage;
  }

  if (argc != 1) {
  usage:
    fprintf(stderr, "usage: %s [-d] [-I <include-dir>] [-D x=y] [-m <module-name>] [ -O ] <input-file>\n", progname);
    return (1);
  }

  if (include_path_list.empty())  include_path_list.push_back((char *) ".");
  
  char *input_filename = *argv;

  char *p = rindex(input_filename, '.');
  if (p == 0 || strcmp(p + 1, "ovm") != 0) {
    fprintf(stderr, "Invalid input file\n");
    return (1);
  }

  yyin = fopen(input_filename, "r");

  outfp = stdout;

  *p = 0;
  if (module_name == 0)  module_name = input_filename;

  scanner_infile_init(input_filename);
  yyparse();

  xmlSaveFile("-", root);
  
  return (0);
}
