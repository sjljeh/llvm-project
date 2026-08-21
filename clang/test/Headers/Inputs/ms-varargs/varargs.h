#ifndef _MSC_VARARGS_TEST_H
#define _MSC_VARARGS_TEST_H

#define va_dcl int va_alist;
#define va_start(ap) ((void)(ap = &va_alist))

#endif
