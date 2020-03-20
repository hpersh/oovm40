/***************************************************************************
 *
 * Module to implement various functions for DNS
 *
 * Implements the following interface:
 * 
 * @class Dns
 * {
 *     // Returns a list of objects describing the IP address information
 *     // for the given hostname.
 *     // Each object has fields corresponding to fields in the 
 *     // struct addrinfo structure returned by getaddrinfo(3).
 *     // The optional hints argument is an object of the same form,
 *     // which contains hints that will be fed to getaddrinfo().
 *     @classmethod getaddrinfo(hostname, hints[]);
 * }
 *
 ***************************************************************************/

#include <sys/types.h>          /* See NOTES */
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "oovm.h"

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

static ovm_obj_t my_class;	/* Just a short-cut, really held in module/class hierarchy */

#define METHOD_MODULE  dns
#define METHOD_CLASS   Dns

CM_DECL(getaddrinfo)
{
    ovm_method_argc_chk_range(th, 2, 3);

    ovm_obj_str_t s = ovm_inst_strval(th, &argv[1]);
    const char *p;
    unsigned n, k;
    for (k = 0, p = s->data, n = s->size - 1; n > 0; --n, ++p) {
	char c = *p;
	if (c == '.') {
	    if (k == 0)  break;
	    k = 0;
	} else if (c >= '0' && c <= '9') {
	    ++k;
	} else break;
    }

    if (n == 0) {
	ovm_inst_assign(dst, &argv[1]);
	return;
    }
  
    ovm_inst_t work = ovm_stack_alloc(th, 4);

    struct addrinfo hints[1];
    memset(hints, 0, sizeof(*hints));
    hints->ai_flags  = AI_CANONNAME;
    hints->ai_family = AF_UNSPEC;
    if (argc == 3) {
	ovm_inst_assign(&work[-4], &argv[2]);
	ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(List), 1);
	while (!ovm_inst_is_nil(&work[-1])) {
	  ovm_inst_assign(&work[-4], &work[-1]);
	  ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(car), 1);
	  ovm_inst_assign(&work[-4], &work[-2]);
	  ovm_method_callsch(th, &work[-3], OVM_STR_CONST_HASH(second), 1);
	  if (ovm_inst_of_raw(&work[-3]) != OVM_CL_INTEGER) {
	    ovm_except_inv_value(th, &argv[2]);
	  }
	  int val = work[-3].intval;
	  ovm_inst_assign(&work[-4], &work[-2]);
	  ovm_method_callsch(th, &work[-3], OVM_STR_CONST_HASH(first), 1);
	  do {
	    ovm_str_newc(&work[-4], OVM_STR_CONST(ai_family));
	    ovm_method_callsch(th, &work[-4], OVM_STR_CONST_HASH(equal), 2);
	    if (ovm_inst_boolval(th, &work[-4])) {
	      hints->ai_family = val;
	      break;
	    }
	    ovm_str_newc(&work[-4], OVM_STR_CONST(ai_socktype));
	    ovm_method_callsch(th, &work[-4], OVM_STR_CONST_HASH(equal), 2);
	    if (ovm_inst_boolval(th, &work[-4])) {
	      hints->ai_socktype = val;
	      break;
	    }
	    ovm_str_newc(&work[-4], OVM_STR_CONST(ai_protocol));
	    ovm_method_callsch(th, &work[-4], OVM_STR_CONST_HASH(equal), 2);
	    if (ovm_inst_boolval(th, &work[-4])) {
	      hints->ai_protocol = val;
	      break;
	    }
	    ovm_except_inv_value(th, &argv[2]);
	  } while (0);
	  ovm_inst_assign(&work[-4], &work[-1]);
	  ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(cdr), 1);
	}
    }
    
    struct addrinfo *res;
    if (getaddrinfo(s->data, 0, hints, &res) != 0) {
	ovm_inst_assign_obj(dst, 0);
	return;
    }

    ovm_inst_assign_obj(&work[-1], 0);
    for (; res != 0; res = res->ai_next) {
	ovm_inst_assign_obj(&work[-4], ovm_consts.Dictionary);
	ovm_method_callsch(th, &work[-4], OVM_STR_CONST_HASH(new), 1);
	ovm_str_newch(&work[-3], OVM_STR_CONST_HASH(ai_family));
	ovm_int_newc(&work[-2], res->ai_family);
	ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(atput), 3);
	ovm_str_newch(&work[-3], OVM_STR_CONST_HASH(ai_socktype));
	ovm_int_newc(&work[-2], res->ai_socktype);
	ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(atput), 3);
	ovm_str_newch(&work[-3], OVM_STR_CONST_HASH(ai_protocol));
	ovm_int_newc(&work[-2], res->ai_protocol);
	ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(atput), 3);
	char buf[INET6_ADDRSTRLEN];
	void *src;
	switch (res->ai_family) {
	case AF_INET:
	    src = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
	    break;
	case AF_INET6:
	    src = &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
	    break;
	default:
	    src = 0;
	}
	if (src != 0) {
	    inet_ntop(res->ai_family, src, buf, sizeof(buf));
	    ovm_str_newch(&work[-3], OVM_STR_CONST_HASH(ai_addr));
	    ovm_str_newc1(&work[-2], buf);
	    ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(atput), 3);
	}
	if (res->ai_canonname != 0) {
	    ovm_str_newch(&work[-3], OVM_STR_CONST_HASH(ai_canonname));
	    ovm_str_newc1(&work[-2], res->ai_canonname);
	    ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(atput), 3);
	}
	ovm_inst_assign(&work[-3], &work[-4]);
	ovm_inst_assign(&work[-4], &work[-1]);
	ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(cons), 2);
    }

    ovm_inst_assign(&work[-4], &work[-1]);
    ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(reverse), 1);

    freeaddrinfo(res);
}

void __dns_init__(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)
{
    ovm_inst_t old = th->sp;

    ovm_stack_push(th, &argv[0]);
  
    ovm_stack_push_obj(th, ovm_consts.Object);
    ovm_class_new(th, OVM_STR_CONST_HASH(Dns), 0, 0, 0);
    my_class = th->sp->objval;

    ovm_classmethod_add(th, OVM_STR_CONST_HASH(getaddrinfo), OVM_INST_TYPE_CODEMETHOD, METHOD_NAME(getaddrinfo));

    ovm_stack_unwind(th, old);
}
