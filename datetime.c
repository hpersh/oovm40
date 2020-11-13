/***************************************************************************
 *
 * Module to implement various functions for date and time
 *
 * Implements the following interface:
 *
 * @class Delay
 * {
 *     @classmethod sleep(time); // Sleep, time in seconds
 * }
 *
 * @class Datetime
 * {
 *     // Returns time, as an integer, a la Linux time(2)
 *     @classmethod time();
 *
 *     // Converts time returned from time() above to a string of the form
 *     // "Wed Jun 30 21:49:08 1993", a la libc ctime(3).
 *     // Note that while the string returned by libc has a newline at the
 *     // end, the string returned by this function does not.
 *     @classmethod ctime(time);
 * }
 *
 * @class Tm
 * {
 *     // An instance of the Tm class is an object that looks like struct tm,
 *     // from libc ctime(3), thus:
 *     // field     type      description
 *     // tm_sec    #Integer  Seconds (0-60)
 *     // tm_min    #Integer  Minutes (0-59)
 *     // tm_hour   #Integer  Hours (0-23)
 *     // tm_mday   #Integer  Day of the month (1-31)
 *     // tm_mon    #Integer  Month (0-11)
 *     // tm_year   #Integer  Year - 1900
 *     // tm_wday   #Integer  Day of the week (0-6, Sunday = 0)
 *     // tm_yday   #Integer  Day in the year (0-365, 1 Jan = 0)
 *     // tm_isdst  #Boolean  Daylight saving time
 * 
 *     // Return an instance of the Tm class (see above), based on the given
 *     // time, returned from Datetime.time() (see above)
 *     @classmethod new(time);
 *
 *     // Return a time, as an integer (a la Datetime.ctime() above), based on
 *     // the Tm instance
 *     @method mktime();
 *
 *     // Compare 2 Tm instances, return an integer, as follows:
 *     // -1 <=> method receiver is less than argument
 *     // 0  <=> method receiver and argument are equal
 *     // 1  <=> method receiver is greater than argument
 *     @method cmp(tm);
 *
 *     // Converts a Tm instance to a string of the form
 *     // "Wed Jun 30 21:49:08 1993", a la libc asctime(3).
 *     // Note that while the string returned by libc has a newline at the
 *     // end, the string returned by this function does not.
 *     @method String();
 *
 *     // Same as String()     
 *     @method write();
 * }
 *
 ***************************************************************************/

#include <unistd.h>
#include <time.h>
#include <string.h>

#include "oovm.h"

#define METHOD_MODULE  datetime
#define METHOD_CLASS   Delay

CM_DECL(sleep)
{
    ovm_method_argc_chk_exact(th, 2);
    sleep(ovm_inst_intval(th, &argv[1]));
    ovm_inst_assign(dst, &argv[0]);
}

#undef  METHOD_CLASS
#define METHOD_CLASS  Datetime

CM_DECL(time)
{
    ovm_method_argc_chk_exact(th, 1);
    ovm_int_newc(dst, time(0));
}

CM_DECL(ctime)
{
    ovm_method_argc_chk_exact(th, 2);
    char buf[26];
    time_t t = ovm_inst_intval(th, &argv[1]);
    ctime_r(&t, buf);
    ovm_str_newc(dst, strlen(buf), buf);
}

static void
tm_to_inst(ovm_thread_t th, ovm_inst_t inst, struct tm *tm)
{
    ovm_inst_t work = ovm_stack_alloc(th, 3);

    ovm_inst_assign(&work[-3], inst);

    ovm_str_newch(&work[-2], OVM_STR_CONST_HASH(tm_sec));
    ovm_int_newc(&work[-1], tm->tm_sec);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);

    ovm_str_newch(&work[-2], OVM_STR_CONST_HASH(tm_min));
    ovm_int_newc(&work[-1], tm->tm_min);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);

    ovm_str_newch(&work[-2], OVM_STR_CONST_HASH(tm_hour));
    ovm_int_newc(&work[-1], tm->tm_hour);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);

    ovm_str_newch(&work[-2], OVM_STR_CONST_HASH(tm_mday));
    ovm_int_newc(&work[-1], tm->tm_mday);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);

    ovm_str_newch(&work[-2], OVM_STR_CONST_HASH(tm_mon));
    ovm_int_newc(&work[-1], tm->tm_mon);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);

    ovm_str_newch(&work[-2], OVM_STR_CONST_HASH(tm_year));
    ovm_int_newc(&work[-1], tm->tm_year);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);

    ovm_str_newch(&work[-2], OVM_STR_CONST_HASH(tm_wday));
    ovm_int_newc(&work[-1], tm->tm_wday);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);

    ovm_str_newch(&work[-2], OVM_STR_CONST_HASH(tm_yday));
    ovm_int_newc(&work[-1], tm->tm_yday);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);

    ovm_str_newch(&work[-2], OVM_STR_CONST_HASH(tm_isdst));
    ovm_bool_newc(&work[-1], tm->tm_isdst);
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(atput), 3);

    ovm_stack_unwind(th, work);
}

static void
inst_to_tm(ovm_thread_t th, struct tm *tm, ovm_inst_t inst)
{
    memset(tm, 0, sizeof(*tm));

    ovm_inst_t work = ovm_stack_alloc(th, 2);

    ovm_inst_assign(&work[-2], inst);

    ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(tm_sec));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    tm->tm_sec = ovm_inst_intval(th, &work[-1]);

    ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(tm_min));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    tm->tm_min = ovm_inst_intval(th, &work[-1]);

    ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(tm_hour));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    tm->tm_hour = ovm_inst_intval(th, &work[-1]);

    ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(tm_mday));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    tm->tm_mday = ovm_inst_intval(th, &work[-1]);

    ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(tm_mon));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    tm->tm_mon = ovm_inst_intval(th, &work[-1]);

    ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(tm_year));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    tm->tm_year = ovm_inst_intval(th, &work[-1]);

    ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(tm_wday));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    tm->tm_wday = ovm_inst_intval(th, &work[-1]);

    ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(tm_yday));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    tm->tm_yday = ovm_inst_intval(th, &work[-1]);

    ovm_str_newch(&work[-1], OVM_STR_CONST_HASH(tm_isdst));
    ovm_method_callsch(th, &work[-1], OVM_STR_CONST_HASH(ate), 2);
    tm->tm_isdst = ovm_inst_boolval(th, &work[-1]);

    ovm_stack_unwind(th, work);
}

#undef  METHOD_CLASS
#define METHOD_CLASS  Tm

CM_DECL(init)
{
    ovm_method_argc_chk_exact(th, 2);
    time_t t = ovm_inst_intval(th, &argv[1]);
    struct tm tm[1];
    localtime_r(&t, tm);
    tm_to_inst(th, &argv[0], tm);
    ovm_inst_assign(dst, &argv[0]);
}

CM_DECL(cmp)
{
    ovm_method_argc_chk_exact(th, 2);
    struct tm tm1[1], tm2[1];
    inst_to_tm(th, tm1, &argv[0]);
    inst_to_tm(th, tm2, &argv[1]);
    time_t t1 = mktime(tm1);
    time_t t2 = mktime(tm2);
    int cmp;
    if (t1 > t2)       cmp = 1;
    else if (t2 > t1)  cmp = -1;
    else               cmp = 0;
    ovm_int_newc(dst, cmp);
}

CM_DECL(mktime)
{
    ovm_method_argc_chk_exact(th, 1);
    struct tm tm[1];
    inst_to_tm(th, tm, &argv[0]);
    ovm_int_newc(dst, mktime(tm));
}

CM_DECL(write)
{
    ovm_method_argc_chk_exact(th, 1);
    struct tm tm[1];
    inst_to_tm(th, tm, &argv[0]);
    char buf[26];
    asctime_r(tm, buf);
    ovm_str_newc(dst, strlen(buf), buf);
}

void __datetime_init__(ovm_thread_t th, ovm_inst_t dst, unsigned argc, ovm_inst_t argv)
{
    ovm_inst_t old = th->sp;

    ovm_stack_push(th, &argv[0]);

    ovm_stack_push_obj(th, ovm_consts.Object);
    ovm_class_new(th, OVM_STR_CONST_HASH(Delay), 0, 0, 0);

#undef  METHOD_CLASS
#define METHOD_CLASS  Delay
  
    ovm_classmethod_add(th, OVM_STR_CONST_HASH(sleep), METHOD_NAME(sleep));

    ovm_stack_free(th, 1);
  
    ovm_stack_push_obj(th, ovm_consts.Object);
    ovm_class_new(th, OVM_STR_CONST_HASH(Datetime), 0, 0, 0);

#undef  METHOD_CLASS
#define METHOD_CLASS  Datetime
  
    ovm_classmethod_add(th, OVM_STR_CONST_HASH(time),  METHOD_NAME(time));
    ovm_classmethod_add(th, OVM_STR_CONST_HASH(ctime), METHOD_NAME(ctime));

    ovm_stack_free(th, 1);

    ovm_stack_alloc(th, 4);
    ovm_inst_assign_obj(&th->sp[0], ovm_consts.Metaclass);
    ovm_str_newch(&th->sp[1], OVM_STR_CONST_HASH(Tm));
    ovm_inst_assign_obj(&th->sp[2], ovm_consts.Object);
    ovm_method_callsch(th, &th->sp[3], OVM_STR_CONST_HASH(new), 3);
    ovm_stack_free(th, 3);

#undef  METHOD_CLASS
#define METHOD_CLASS  Tm
  
    ovm_method_add(th, _OVM_STR_CONST_HASH("__init__"), METHOD_NAME(init));
    ovm_method_add(th, OVM_STR_CONST_HASH(mktime),      METHOD_NAME(mktime));
    ovm_method_add(th, OVM_STR_CONST_HASH(cmp),         METHOD_NAME(cmp));
    ovm_method_add(th, OVM_STR_CONST_HASH(String),      METHOD_NAME(write));
    ovm_method_add(th, OVM_STR_CONST_HASH(write),       METHOD_NAME(write));

    ovm_stack_unwind(th, old);
}
