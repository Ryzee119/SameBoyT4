#pragma once
#include_next <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static inline int _vscprintf(const char *format, va_list pargs)
{
    int retval;
    va_list argcopy;
    va_copy(argcopy, pargs);
    retval = vsnprintf(NULL, 0, format, argcopy);
    va_end(argcopy);
    return retval;
}

static inline int vasprintf(char **str, const char *fmt, va_list args)
{
    size_t size = _vscprintf(fmt, args) + 1;
    *str = (char *)malloc(size);
    size_t ret = vsprintf(*str, fmt, args);
    if (ret != size - 1)
    {
        free(*str);
        *str = NULL;
        return -1;
    }
    return ret;
}