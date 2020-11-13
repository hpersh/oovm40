#include <sys/types.h>          /* See NOTES */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "oovm.h"

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

static ovm_obj_t my_class;      /* Just a short-cut, really held in module/class hierarchy */

struct ovm_obj_socket {
    struct ovm_obj  base[1];
    int             domain, type, proto;
    struct sockaddr sa_local[1], sa_remote[1];
    int             fd, _errno;
};
typedef struct ovm_obj_socket *ovm_obj_socket_t;

static inline ovm_obj_socket_t ovm_obj_socket(ovm_obj_t obj)
{
    return ((ovm_obj_socket_t) obj);
}

static inline ovm_obj_socket_t ovm_inst_socketval_nochk(ovm_inst_t inst)
{
    return (ovm_obj_socket(inst->objval));
}

static inline ovm_obj_socket_t ovm_inst_socketval(ovm_thread_t th, ovm_inst_t inst)
{
    if (ovm_inst_of_raw(inst) != ovm_obj_class(my_class))  ovm_except_inv_value(th, inst);
    return (ovm_inst_socketval_nochk(inst));
}

static void socket_cleanup(ovm_obj_t obj)
{
    close(ovm_obj_socket(obj)->fd);
}

static void socket_init(ovm_obj_t obj, va_list ap)
{
    ovm_obj_socket_t s = ovm_obj_socket(obj);
    s->domain = va_arg(ap, int);
    s->type   = va_arg(ap, int);
    s->proto  = va_arg(ap, int);
    s->fd     = va_arg(ap, int);
}

static ovm_obj_socket_t socket_newc(ovm_inst_t dst, int domain, int type, int proto, int fd)
{
    return (ovm_obj_socket(ovm_obj_alloc(dst, sizeof(struct ovm_obj_socket), ovm_obj_class(my_class), 2, socket_init, domain, type, proto, fd)));
}

static bool inet_addr_inst(ovm_inst_t inst, struct sockaddr_in *sa)
{
    if (ovm_inst_of_raw(inst) != OVM_CL_PAIR)  return (false);
    ovm_obj_pair_t pr = ovm_inst_pairval_nochk(inst);
    if (ovm_inst_of_raw(pr->first) != OVM_CL_STRING
        || ovm_inst_of_raw(pr->second) != OVM_CL_INTEGER
        ) {
        return (false);
    }

    sa->sin_family = AF_INET;

    if (inet_aton(ovm_inst_strval_nochk(pr->first)->data, &sa->sin_addr) == 0)  return (false);
    int port = pr->second->intval;
    if (port < 0 || port > 65535)  return (false);
    sa->sin_port = htons(port);

    return (true);
}

#define METHOD_MODULE socket
#define METHOD_CLASS  Socket

CM_DECL(new)
{
    int domain, type, proto;
    ovm_obj_t remote = 0;
    struct sockaddr_in sa[1];

    switch (argc) {
    case 2:
        {
            ovm_inst_t work = ovm_stack_alloc(th, 2);

            ovm_inst_assign(&work[-2], &argv[1]);
            ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(domain));
            ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
            domain = ovm_inst_intval(th, &work[-1]);
            ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(type));
            ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
            type = ovm_inst_intval(th, &work[-1]);
            ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(proto));
            ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
            proto = ovm_inst_intval(th, &work[-1]);
            ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(remote));
            ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(at), 2);
            remote = work[-1].objval;
            if (remote != 0) {
                if (!inet_addr_inst(ovm_obj_pair(remote)->second, sa))  ovm_except_inv_value(th, &work[-1]);
            }
            
            break;
        }
    case 4:
        domain = ovm_inst_intval(th, &argv[1]);
        type   = ovm_inst_intval(th, &argv[2]);
        proto  = ovm_inst_intval(th, &argv[3]);
        break;
    default:
        ;
    }

    int fd = socket(domain, type, proto);
    if (fd < 0) {
        ovm_inst_assign_obj(dst, 0);
        return;
    }

    ovm_obj_socket_t s = socket_newc(dst, domain, type, proto, fd);

    if (type == SOCK_STREAM && remote != 0 && sa->sin_addr.s_addr != 0) {
        if (connect(s->fd, (struct sockaddr *) sa, sizeof(*sa)) != 0) {
            s->_errno = errno;
            ovm_inst_assign_obj(dst, 0);
        } else {
            memcpy(&s->sa_remote, sa, sizeof(s->sa_remote));
        }
    }
}

_CM_DECL(socket$Socket$errno)
{
    ovm_int_newc(dst, ovm_inst_socketval(th, &argv[0])->_errno);
}

CM_DECL(bind)
{
    ovm_method_argc_chk_exact(th, 2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    ovm_obj_socket_t s = ovm_inst_socketval(th, recvr);
    switch (s->domain) {
    case AF_INET:
        {
            struct sockaddr_in sa[1];
            if (!inet_addr_inst(arg, sa))  ovm_except_inv_value(th, arg);
            if (bind(s->fd, (struct sockaddr *) sa, sizeof(*sa)) != 0) {
                s->_errno = errno;
                ovm_inst_assign_obj(dst, 0);
            } else {
                memcpy(&s->sa_local, sa, sizeof(s->sa_local));
                ovm_inst_assign(dst, recvr);
            }
        }
        return;
    default:
        ;
    }
  
    ovm_except_inv_value(th, arg);
}

CM_DECL(connect)
{
    ovm_method_argc_chk_exact(th, 2);
    ovm_inst_t recvr = &argv[0], arg = &argv[1];
    ovm_obj_socket_t s = ovm_inst_socketval(th, recvr);
    switch (s->domain) {
    case AF_INET:
        {
            struct sockaddr_in sa[1];
            if (!inet_addr_inst(arg, sa))  ovm_except_inv_value(th, arg);
            if (connect(s->fd, (struct sockaddr *) sa, sizeof(*sa)) != 0) {
                s->_errno = errno;
                ovm_inst_assign_obj(dst, 0);
            } else {
                memcpy(&s->sa_remote, sa, sizeof(s->sa_remote));
                ovm_inst_assign(dst, recvr);
            }
        }
        return;
    default:
        ;
    }
  
    ovm_except_inv_value(th, arg);
}

CM_DECL(listen)
{
    ovm_method_argc_chk_exact(th, 2);
    ovm_obj_socket_t s = ovm_inst_socketval(th, &argv[0]);
    int qlen = ovm_inst_intval(th, &argv[1]);

    int rc = listen(s->fd, qlen);
    if (rc < 0) {
        s->_errno = errno;
        ovm_inst_assign_obj(dst, 0);
        return;
    }

    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(accept)
{
    ovm_obj_socket_t s = ovm_inst_socketval(th, &argv[0]);
    socklen_t  salen = sizeof(s->sa_remote);
  
    int fd = accept(s->fd, s->sa_remote, &salen);
    if (fd < 0) {
        s->_errno = errno;
        ovm_inst_assign_obj(dst, 0);
        return;
    }

    socket_newc(dst, s->domain, s->type, s->proto, fd);
}


CM_DECL(read)
{
    ovm_method_argc_chk_exact(th, 2);
    ovm_obj_socket_t s = ovm_inst_socketval(th, &argv[0]);
    ovm_intval_t n = ovm_inst_intval(th, &argv[1]);
    if (n < 0)  ovm_except_inv_value(th, &argv[1]);

    ovm_inst_t work = ovm_stack_alloc(th, 2);
    
    ovm_inst_assign_obj(&work[-2], ovm_consts.Bytearray);
    ovm_int_newc(&work[-1], n);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(new), 2);
    ovm_obj_barray_t b = ovm_inst_barrayval_nochk(&work[-1]);
    int nn = read(s->fd, b->data, n);
    if (nn < 0) {
        ovm_int_newc(dst, nn);
        return;
    }
    if (nn == n) {
        ovm_inst_assign(dst, &work[-1]);
        return;
    }
    
    ovm_stack_alloc(th, 1);
    ovm_inst_assign_obj(&work[-3], ovm_consts.Bytearray);
    ovm_int_newc(&work[-2], nn);
    ovm_method_callsch(th, dst, OVM_STR_CONST_HASH(new), 2);
    memcpy(ovm_inst_barrayval_nochk(dst)->data, b->data, nn);
}

CM_DECL(readln)
{
    ovm_method_argc_chk_exact(th, 1);
    ovm_obj_socket_t s = ovm_inst_socketval(th, &argv[0]);
  
    struct ovm_clist cl[1];
    ovm_clist_init(cl);

    char c;
    for (;;) {
        int n = read(s->fd, &c, 1);
        if (n <= 0)  break;
        ovm_clist_append_char(cl, c);
        if (c == '\n')  break;
    }

    ovm_str_clist(dst, cl);

    ovm_clist_fini(cl);
}

static const char *domain_to_str(unsigned domain)
{
    return (domain == AF_INET ? "AF_INET" : "UNKNOWN");
}

static const char *type_to_str(unsigned type)
{
    switch (type) {
    case SOCK_STREAM:
        return ("SOCK_STREAM");
    case SOCK_DGRAM:
        return ("SOCK_DGRAM");
    default:
        ;
    }
    return ("UNKNOWN");
}

static const char *proto_to_str(unsigned proto)
{
    return ("");
}

static void
socket_write(ovm_thread_t th, ovm_inst_t dst, ovm_obj_socket_t s)
{
    ovm_inst_t work = ovm_stack_alloc(th, 2);

    struct ovm_clist cl[1];
    ovm_clist_init(cl);

    ovm_inst_assign_obj(&work[-2], ovm_consts.Object);
    ovm_str_newc(&work[-1], OVM_STR_CONST(write));
    ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(method), 2);
    ovm_inst_assign_obj(&work[-1], s->base);
    ovm_method_callsch(th, &work[-2], OVM_STR_CONST_HASH(call), 2);
    ovm_clist_append_str(th, cl, ovm_inst_strval(th, &work[-2]));

    ovm_clist_appendc(cl, _OVM_STR_CONST("{domain: "));
    ovm_clist_appendc1(cl, domain_to_str(s->domain));

    ovm_clist_appendc(cl, _OVM_STR_CONST(", type: "));
    ovm_clist_appendc1(cl, type_to_str(s->type));

    ovm_clist_appendc(cl, _OVM_STR_CONST(", proto: "));
    ovm_clist_appendc1(cl, proto_to_str(s->proto));

    char buf[128];
  
    ovm_clist_appendc(cl, _OVM_STR_CONST(", local: <\""));
    inet_ntop(s->domain, &((struct sockaddr_in *) s->sa_local)->sin_addr, buf, sizeof(buf));
    ovm_clist_appendc1(cl, buf);
    ovm_clist_appendc(cl, _OVM_STR_CONST("\", "));
    snprintf(buf, sizeof(buf), "%d", ntohs(((struct sockaddr_in *) s->sa_local)->sin_port));
    ovm_clist_appendc1(cl, buf);
    ovm_clist_appendc(cl, _OVM_STR_CONST(">, remote: <\""));
    inet_ntop(s->domain, &((struct sockaddr_in *) s->sa_remote)->sin_addr, buf, sizeof(buf));
    ovm_clist_appendc1(cl, buf);
    ovm_clist_appendc(cl, _OVM_STR_CONST("\", "));
    snprintf(buf, sizeof(buf), "%d", ntohs(((struct sockaddr_in *) s->sa_remote)->sin_port));
    ovm_clist_appendc1(cl, buf);
    ovm_clist_appendc(cl, _OVM_STR_CONST(">}"));

    ovm_str_clist(dst, cl);

    ovm_clist_fini(cl);
    ovm_stack_unwind(th, work);
}


CM_DECL(write)
{
    if (argc == 1) {
        socket_write(th, dst, ovm_inst_socketval(th, &argv[0]));
        return;
    }

    ovm_method_argc_chk_range(th, 2, 3);

    ovm_obj_socket_t s = ovm_inst_socketval(th, &argv[0]);
    ovm_inst_t arg = &argv[1];
    const unsigned char *p = 0;
    unsigned n = 0;
#if 0
    if (ovm_inst_is_obj_cl(arg, OVM_CL_BYTEARRAY)) {
        ovm_obj_bytearr_t b = ovm_inst_barrval_nochk(arg);
        p = b->data;
        n = b->size;
    } else
#endif
        if (ovm_inst_of_raw(arg) == OVM_CL_STRING) {
            ovm_obj_str_t ss = ovm_inst_strval_nochk(arg);
            p = (const unsigned char *) ss->data;
            n = ss->size - 1;
        } else  ovm_except_inv_value(th, arg);

    ovm_int_newc(dst, write(s->fd, p, n));
}


static struct {
    unsigned   nm_size;
    const char *nm;
    unsigned   hash;
    int        val;
} class_vars[] = {
    { _OVM_STR_CONST_HASH("#AF_INET"),     AF_INET },
    { _OVM_STR_CONST_HASH("#SOCK_DGRAM"),  SOCK_DGRAM },
    { _OVM_STR_CONST_HASH("#SOCK_STREAM"), SOCK_STREAM }
};

void __socket_init__(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)
{
    ovm_inst_t old = th->sp;

    ovm_stack_push(th, &argv[0]);

    ovm_stack_push_obj(th, ovm_consts.Object);
    ovm_class_new(th, OVM_STR_CONST_HASH(Socket), 0, socket_cleanup, socket_cleanup);
    my_class = th->sp->objval;

    ovm_inst_t work = ovm_stack_alloc(th, 3);

    ovm_inst_assign(&work[-3], &work[0]);
    unsigned i;
    for (i = 0; i < ARRAY_SIZE(class_vars); ++i) {
        ovm_str_newc(&work[-2], class_vars[i].nm_size, class_vars[i].nm);
        ovm_int_newc(&work[-1], class_vars[i].val);
        ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);
    }

    ovm_stack_unwind(th, work);
    
    ovm_classmethod_add(th, OVM_STR_CONST_HASH(new), METHOD_NAME(new));
    ovm_method_add(th, _OVM_STR_CONST_HASH("errno"), socket$Socket$errno);
    ovm_method_add(th, OVM_STR_CONST_HASH(bind),     METHOD_NAME(bind));
    ovm_method_add(th, OVM_STR_CONST_HASH(connect),  METHOD_NAME(connect));
    ovm_method_add(th, OVM_STR_CONST_HASH(listen),   METHOD_NAME(listen));
    ovm_method_add(th, OVM_STR_CONST_HASH(accept),   METHOD_NAME(accept));
    ovm_method_add(th, OVM_STR_CONST_HASH(read),     METHOD_NAME(read));
    ovm_method_add(th, OVM_STR_CONST_HASH(readln),   METHOD_NAME(readln));
    ovm_method_add(th, OVM_STR_CONST_HASH(write),    METHOD_NAME(write));
    ovm_method_add(th, OVM_STR_CONST_HASH(String),   METHOD_NAME(write));

    ovm_stack_unwind(th, old);
}

// Local Variables:
// c-basic-offset: 4
// End:
