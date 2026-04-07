#ifndef WCWIDTH_H_INCLUDED
#define WCWIDTH_H_INCLUDED

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int tsm_wcwidth(wchar_t ucs);
int tsm_ucs4_wcwidth(uint32_t ucs);

#ifdef __cplusplus
}
#endif


#endif
