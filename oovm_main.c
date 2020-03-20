#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "oovm.h"

static bool parse_start(char *s, char **start_module, char **start_cl, char **start_method)
{
    static char default_start_class[] = "Start", default_start_method[] = "start";

    *start_module = s;
    char *p = index(s, '.');
    if (p == 0) {
	*start_cl     = default_start_class;
	*start_method = default_start_method;
	return (true);
    }
    *p = 0;
    *start_cl = s = p + 1;
    p = rindex(s, '.');
    if (p == 0) {
	*start_method = default_start_method;
	return (true);    
    }
    *p = 0;
    *start_method = p + 1;
    return (true);
}

int main(int argc, char **argv)
{
    char *progname = argv[0], *start_module, *start_cl, *start_method;
    if (argc < 2 || !parse_start(argv[1], &start_module, &start_cl, &start_method)) {
	fprintf(stderr, "usage: %s <start-method>\n", progname);
	exit(1);
    }

    ovm_thread_t th = ovm_init(0, 0);
    if (th == 0) {
	fputs("ovm initialization failed\n", stderr);
	return (1);
    }

    ovm_inst_t work = ovm_stack_alloc(th, 1);
    int rc;
#ifdef STATIC
#define INIT   __ ## STATIC_MODULE ## _init__
#define ENTRY  STRINGIZE(STATIC_MODULE)
    extern ovm_codemethod_t INIT, ENTRY;
    rc = ovm_run_static(th, &work[-1], INIT, ENTRY, argc - 1, argv + 1);
#else
    rc = ovm_run(th, &work[-1], start_module, start_cl, start_method, argc - 2, argv + 2);
#endif
    if (rc != 0)  return (rc);
    return (ovm_inst_of_raw(&work[-1]) == OVM_CL_INTEGER ? work[-1].intval : 0);
}

/*
Local Variables:
c-basic-offset: 4
End:
*/
