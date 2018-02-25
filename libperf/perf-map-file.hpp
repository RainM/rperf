#if !defined(__PERF_MAP_AGENT_H__)
#define __PERF_MAP_AGENT_H__

#include <stdio.h>

#if defined(__cplusplus)
#define __API__ extern "C"
#else
#define __API__
#endif

__API__ FILE *perf_map_open(void);
__API__ int perf_map_close(FILE *fp);
__API__ void perf_map_write_entry(FILE *method_file, const void* code_addr, unsigned int code_size, const char* entry);

#endif // !defined(__PERF_MAP_AGENT_H__)
