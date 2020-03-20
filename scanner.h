struct infile {
  struct infile *prev;
  const char    *filename;
  FILE          *fp;
  unsigned      line_num;
};

extern struct infile *infile_cur;

extern "C" {
void scanner_infile_print(FILE *fp);
const char *scanner_infile_cur_file(void);
unsigned scanner_infile_cur_line(void);
void scanner_include(const char *filename);
void scanner_infile_pop(void);
void scanner_infile_init(const char *filename);
}
