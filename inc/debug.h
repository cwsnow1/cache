#include <features.h>

#ifdef NDEBUG
/* This prints an "Assertion failed" message and aborts.  */
extern void __assert_fail (const char *__assertion, const char *__file, unsigned int __line, const char *__function) __THROW __attribute__ ((__noreturn__));
#define CODE_FOR_ASSERT(...)
#define __ASSERT_FUNCTION __extension__ __PRETTY_FUNCTION__
#define assert_release(expr) ((expr) ? __ASSERT_VOID_CAST (0) \
    : __assert_fail (#expr, __FILE__, __LINE__, __ASSERT_FUNCTION))
#else
#define CODE_FOR_ASSERT(x) x
#define assert_release assert
#endif
