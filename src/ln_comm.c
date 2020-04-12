/**
 * ln_comm.c,
 * the stack of coroutine-switching supported 
 * by 'context/assembly' originally should not 
 * consume up to 11Kib burst in my mind last 
 * night when cooking.
 * 
 * there must be something wrong. 
 * ------------------------------
 * 
 * the `printf() family` spotlighted quickly,
 * stack up to 11Kib if more than one parameters 
 * passed. 
 * 
 * the truth is that 
 * [1] the    `buffered_vfprintf`
 * would be called if multiple parameters passed 
 * to `printf() family`. simply look at the 
 * `buffered_vfprintf` */
#if 0 /* glibc2.17 */
static int internal_function
buffered_vfprintf (register _IO_FILE *s, const CHAR_T *format,
                   _IO_va_list args)
{
    /* _IO_BUFSIZ equals 8192 */
    CHAR_T buf[_IO_BUFSIZ]; 
    
    /* ... */
}
#endif
/**
 * [2] the `_IO_fwrite` would be called directly if 
 * only one parameter passed to `printf() family`. 
 * i.e., no _IO_BUFSIZ included.
 * 
 * fwrite() selected to instead the `printf() family`. 
 * this why the `ln_comm.c` born. common routines can 
 * be included in this file, of course. 
 * 
 * what's more, i guess that `vsnprintf` can be 
 * implement yourself to further reduce stack.
 * 
 * lxr, 2020.04 */
#include "ln_comm.h"
#include <stdarg.h>

/* use _wfprintf() to instead `print family in glibc` 
   would reduce about 10Kib stack memory. 
   
   further, the stack consuming of coroutine is 
   very small(less than 1Kib) if no error-log for 
   cocroutine. but we would't break up with error-log. */
int 
ln_fprintf(int maxb, const char *fmt, ...)
{
    int ret = 0;
    va_list args;
    char *buf = NULL;

    buf = calloc(maxb + 1, 1);
    if (!buf) return CODE_NOMEM;

    va_start(args, fmt);
    ret = fwrite(buf, 1, vsnprintf(buf, maxb, fmt, args), stderr);
    va_end(args);

    free(buf);
    return ret;
}
