#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "oovm_hash.h"

int main(int argc, char **argv)
{
  if (argc != 2)  abort();

  char *s = argv[1];
  printf("%u", mem_hash(strlen(s), s));

  return (0);
}
