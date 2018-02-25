#include <iostream>
#include <unordered_set>
#include <algorithm>
#include <vector>
#include <cstring>

#include "profiler-backend.hpp"

struct routine {
    std::string method_name;
    uint64_t total_time;
    uint64_t invoke_count;

    routine(std::string& method): method_name(method), total_time(0), invoke_count(0) {}
};

struct hash_by_routine_name {
    size_t operator() (const routine& r) const noexcept {
	std::hash<std::string> string_hash;
	return string_hash(r.method_name);
    }
};

struct equals_by_routine_name {
    bool operator() (const routine& r1, const routine& r2) const {
	return r1.method_name == r2.method_name;
    }
};

struct greater_by_routine_total_time {
    bool operator() (const routine* r1, const routine* r2) {
	return r1->total_time > r2->total_time;
    }
};

routine* last_routine = nullptr;
uint64_t routine_start_timestamp = 0;
std::unordered_set<routine, hash_by_routine_name, equals_by_routine_name> all_routines;


std::vector<routine*> functions_by_self_time;


__API__ void visit_sample(uint64_t timestamp, const char* symbol_name, const char* dso) {
    std::string function;
    function += symbol_name;
    function += "@";
    function += dso;

    if (!routine_start_timestamp) {
	routine_start_timestamp = timestamp;
    }
    if ((!last_routine) || (strcmp(last_routine->method_name.c_str(), function.c_str()))) {
	if (last_routine) {
	    last_routine->total_time += (timestamp - routine_start_timestamp);
	}

	auto it = all_routines.find(routine(function));
	if (it == std::end(all_routines)) {
	    auto func_it = all_routines.emplace(function).first;
	    last_routine = const_cast<routine*>(&*func_it);
	    last_routine->invoke_count = 1;
	} else {
	    const_cast<routine&>(*it).invoke_count += 1;
	    last_routine = const_cast<routine*>(&*it);
	}
	routine_start_timestamp = timestamp;
    }
}

void prepare_top() {
    std::transform(
	std::begin(all_routines),
	std::end(all_routines),
	std::back_inserter(functions_by_self_time),
	[] (const routine& r) {
	    return const_cast<routine*>(&r);
	}
	);
    std::sort(
	std::begin(functions_by_self_time),
	std::end(functions_by_self_time),
	greater_by_routine_total_time());
}

int get_top_len() {
    return functions_by_self_time.size();
}

const char* ppp(int idx) {
    auto ret = functions_by_self_time[idx]->method_name.c_str();
    return ret;
}

const char* get_top_by_idx(int idx) {
    auto res = ppp(idx);
    // return functions_by_self_time[idx]->method_name.c_str();
    return res;
}

uint64_t get_counters_by_idx(int idx) {
    return functions_by_self_time[idx]->total_time;
}

int get_invoke_count_by_idx(int idx) {
    return functions_by_self_time[idx]->invoke_count;
}
