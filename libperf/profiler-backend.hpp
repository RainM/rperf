#ifndef __PROFILER_HEADER__
#define __PROFILER_HEADER__

#include <stdint.h>

#if defined(__cplusplus)
#define __API__ extern "C"
#else
#define __API__
#endif

__API__ void visit_sample(uint64_t timestamp, const char* symbol_name, const char* dso);
__API__ void prepare_top(void);
__API__ int get_top_len(void);
__API__ const char* get_top_by_idx(int idx);
__API__ uint64_t get_counters_by_idx(int idx);
__API__ int get_invoke_count_by_idx(int idx);

#endif
