/** ************************************************************************
 *
 * \file oovm_types.h
 * \brief OVM library types
 *
 ***************************************************************************/

#ifndef __OOVM_TYPES_H
#define __OOVM_TYPES_H

#include <pthread.h>

#include "oovm_dllist.h"

#ifndef __cplusplus
typedef unsigned char bool;
#define true   1
#define false  0
#endif

/**
 * \defgroup Types Basic types
 */
/**@{*/

/**
 * \brief Fundamental object
 *
 * Use this type as the basis for deriving other objects.
 * This type should be
 * considered opaque; all fields are for internal use only, and should not be touched.
 */
struct ovm_obj {
    struct ovm_dllist list_node[1];
    unsigned          size;
    struct ovm_obj    *inst_of;
    unsigned          ref_cnt;
    pthread_mutex_t   mutex[1]; /* For mutex access to (even internally) mutable data structures, and
                                   for detecting loops in descent through container data structures
                                */
};
typedef struct ovm_obj *ovm_obj_t;
#define OVM_NIL  ((ovm_obj_t) 0)

struct ovm_inst;
typedef struct ovm_inst *ovm_inst_t;

struct ovm_thread;
typedef struct ovm_thread *ovm_thread_t;

typedef long long   ovm_intval_t;
typedef long double ovm_floatval_t;
typedef void (*ovm_codemethod_t)(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv);
typedef unsigned char *ovm_method_t;

enum {
      OVM_INST_TYPE_OBJ = 0,
      OVM_INST_TYPE_BOOL,
      OVM_INST_TYPE_INT,
      OVM_INST_TYPE_FLOAT,
      OVM_INST_TYPE_CODEMETHOD,
      OVM_INST_TYPE_METHOD
};

/**
 * \brief Instance
 *
 * An instance stores any kind of value, both an atom and a reference to an object.
 * This type should be
 * considered opaque; all fields are for internal use only, and should not be touched.
 */
struct ovm_inst {
    unsigned char type;
    bool          hash_valid;   /* true <=> Cached hash value is valid */
    unsigned      hash;         /* Cached hash value */
    union {
        ovm_obj_t        objval;
        bool             boolval;
        ovm_intval_t     intval;
        ovm_floatval_t   floatval;
        ovm_codemethod_t codemethodval;
        ovm_method_t     methodval;
    };
};

/**@}*/

#endif /* __OOVM_TYPES_H */
