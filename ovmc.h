#include <stdio.h>

#include "libxml/tree.h"

#include "oovm.h"

xmlNodePtr root;

struct infile {
  struct infile *prev;
  const char    *filename;
  FILE          *fp;
  unsigned      line_num;
};

struct infile *infile_cur;
 
struct parse_node;

char *module_name;

#ifdef __cplusplus
extern "C" {
#endif

void scanner_infile_init(const char *filename);
const char *scanner_infile_cur_file(void);
unsigned scanner_infile_cur_line(void);
void scanner_include(const char *filename);
void scanner_infile_print(FILE *fp);
FILE *include_file_open(const char *filename);
void include_file_pop(void);
  
int yywrap();

int yyerror(char *s);

int yyparse(void);

#ifdef __cplusplus
}
#endif
