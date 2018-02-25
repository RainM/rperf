////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 *   libperfmap: a JVM agent to create perf-<pid>.map files for consumption
 *               with linux perf-tools
 *   Copyright (C) 2013-2015 Johannes Rudolph<johannes.rudolph@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "perf-map-file.hpp"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>

#include <jni.h>
#include <jvmti.h>
#include <jvmticmlr.h>

#include <cstddef>

#include <iostream>
#include <algorithm>
#include <set>
#include <map>
#include <utility>

FILE *method_file = NULL;

void open_map_file();
void close_map_file();

struct compiled_method_info {
    size_t start_addr;
    size_t length;
    std::string name;

    compiled_method_info(std::string name_, size_t addr, size_t len): start_addr(addr), length(len), name(name_) { }
};

bool operator < (const compiled_method_info& m1, const compiled_method_info& m2) {
    return m1.start_addr < m2.start_addr;
}

bool operator == (const compiled_method_info& m1, const compiled_method_info& m2) {
    return m1.start_addr == m2.start_addr;
}

std::ostream& operator << (std::ostream& os, const compiled_method_info& m) {
    return os << m.name << "@" << std::hex << m.start_addr << " for " << m.length << "bytes";
}

struct jit_compiled_method {
    typedef std::set<compiled_method_info> inner_methods_t;

    inner_methods_t inner_methods;

    jmethodID method_id;

    jit_compiled_method(): method_id(0) {}

    jit_compiled_method(jmethodID method): method_id(method) {}

    typename inner_methods_t::const_iterator begin_inner() const {
	return std::begin(inner_methods);
    }

    typename inner_methods_t::const_iterator end_inner() const {
	return std::end(inner_methods);
    }

    inner_methods_t::reference front() {
	return const_cast<inner_methods_t::reference>(*inner_methods.begin());
    }

    void add_inner_info(std::string&& name, size_t addr, size_t len) {
	inner_methods.emplace(name, addr, len);
    }
};

bool operator < (const jit_compiled_method& m1, const jit_compiled_method& m2) {
    return m1.method_id < m2.method_id;
}

namespace {
    std::map<jmethodID, jit_compiled_method> methods;
}

jit_compiled_method& get_jit_info(jmethodID method) {
    auto it = methods.find(method);
    //return const_cast<jit_compiled_method&>(*it);
    if (it != methods.end()) {
	return it->second;
    } else {
	//jit_compiled_method jit_record(method);
	auto& res = methods[method];
	res.method_id = method;
	return res;
    }
}

void remove_jit_info(jmethodID m) {
    auto it = methods.find(m);
    if (it != std::end(methods)) {
	methods.erase(it);
    }
}

extern "C" void dump_perf_file() {
    open_map_file();

    int total_symbols = 0;
    auto it = methods.begin(), it_end = methods.end();
    while (it != it_end) {
	auto& jit_record = it->second;
	auto item_it = jit_record.begin_inner(), item_end = jit_record.end_inner();
	while (item_it != item_end) {
	    //std::cout << *item_it << std::endl;
	    total_symbols += 1;
	    //perf_map_write_entry(method_file, code_addr, code_size, entry);
	    
	    perf_map_write_entry(method_file, (const void*)item_it->start_addr, item_it->length, item_it->name.c_str());

	    ++item_it;
	}

	++it;
    }

    std::cout << "Processed: " << total_symbols << " symbols" << std::endl;
    
    close_map_file();
}


#define STRING_BUFFER_SIZE 2000
#define BIG_STRING_BUFFER_SIZE 20000

bool unfold_inlined_methods = false;
bool unfold_simple = false;
bool unfold_all = false;
bool print_method_signatures = false;
bool print_source_loc = false;
bool clean_class_names = false;
bool debug_dump_unfold_entries = false;

void open_map_file() {
    if (!method_file)
        method_file = perf_map_open();
}
void close_map_file() {
    perf_map_close(method_file);
    method_file = NULL;
}

static int get_line_number(jvmtiLineNumberEntry *table, jint entry_count, jlocation loc) {
  int i;
  for (i = 0; i < entry_count; i++)
    if (table[i].start_location > loc) return table[i - 1].line_number;

  return -1;
}

void class_name_from_sig(char *dest, size_t dest_size, const char *sig) {
    if (clean_class_names && sig[0] == 'L') {
        const char *src = sig + 1;
        int i;
        for(i = 0; i < (dest_size - 1) && src[i]; i++) {
            char c = src[i];
            if (c == '/') c = '.';
            if (c == ';') c = 0;
            dest[i] = c;
        }
        dest[i] = 0;
    } else
        strncpy(dest, sig, dest_size);
}

static void sig_string(jvmtiEnv *jvmti, jmethodID method, char *output, size_t noutput) {
    char *sourcefile = NULL;
    char *method_name = NULL;
    char *msig = NULL;
    char *csig = NULL;
    //const char *empty = "";
    jvmtiLineNumberEntry *lines = NULL;

    jclass clazz;
    //jvmtiError error = 0;
    jint entrycount = 0;

    strncpy(output, "<error writing signature>", noutput);

    if (!jvmti->GetMethodName(method, &method_name, &msig, NULL)) {
        if (!jvmti->GetMethodDeclaringClass(method, &clazz) &&
            !jvmti->GetClassSignature(clazz, &csig, NULL)) {

            char source_info[1000] = "";
            char *method_signature = "";

            if (print_source_loc) {
                if (!jvmti->GetSourceFileName(clazz, &sourcefile)) {
                    if (!jvmti->GetLineNumberTable(method, &entrycount, &lines)) {
                        int lineno = -1;
                        if(entrycount > 0) lineno = lines[0].line_number;
                        snprintf(source_info, sizeof(source_info), "(%s:%d)", sourcefile, lineno);

                        if (lines != NULL) jvmti->Deallocate((unsigned char *) lines);
                    }
                    if (sourcefile != NULL) jvmti->Deallocate((unsigned char *)sourcefile);
                }
            }

            if (print_method_signatures && msig)
                method_signature = msig;

            char class_name[STRING_BUFFER_SIZE];
            class_name_from_sig(class_name, sizeof(class_name), csig);
            snprintf(output, noutput, "%s::%s%s%s", class_name, method_name, method_signature, source_info);

            if (csig != NULL) jvmti->Deallocate((unsigned char *)csig);
        }
        if (method_name != NULL) jvmti->Deallocate((unsigned char *)method_name);
        if (msig != NULL) jvmti->Deallocate((unsigned char* ) msig);
    }
}

void generate_single_entry(jvmtiEnv *jvmti, jmethodID method, const void *code_addr, jint code_size) {
    char entry[STRING_BUFFER_SIZE];
    sig_string(jvmti, method, entry, sizeof(entry));
    perf_map_write_entry(method_file, code_addr, code_size, entry);
}

/* Generates either a simple or a complex unfolded entry. */
void generate_unfolded_entry(jvmtiEnv *jvmti, jmethodID method, char *buffer, size_t buffer_size, const char *root_name) {
    if (unfold_simple)
        sig_string(jvmti, method, buffer, buffer_size);
    else {
        char entry_name[STRING_BUFFER_SIZE];
        sig_string(jvmti, method, entry_name, sizeof(entry_name));
        snprintf(buffer, buffer_size, "%s in %s", entry_name, root_name);
    }
}

static ptrdiff_t ptr_diff_in_bytes(const void* ptr_end, const void* ptr_begin) {
    ptrdiff_t ptr_diff = static_cast<const char*>(ptr_end) - static_cast<const char*>(ptr_begin);
    return ptr_diff;
}

/* Generates and writes a single entry for a given inlined method. */
void write_unfolded_entry(
        jvmtiEnv *jvmti,
        PCStackInfo *info,
        jmethodID root_method,
        const char *root_name,
        const void *start_addr,
        const void *end_addr) {
    // needs to accommodate: entry_name + " in " + root_name
    char inlined_name[STRING_BUFFER_SIZE * 2 + 4];
    const char *entry_p;

    if (unfold_all) {
        char full_name[BIG_STRING_BUFFER_SIZE];
        full_name[0] = '\0';
        int i;
        for (i = info->numstackframes - 1; i >= 0; i--) {
            //printf("At %d method is %d len %d remaining %d\n", i, info->methods[i], strlen(full_name), sizeof(full_name) - 1 - strlen(full_name));
            sig_string(jvmti, info->methods[i], inlined_name, sizeof(inlined_name));
            strncat(full_name, inlined_name, sizeof(full_name) - 1 - strlen(full_name)); // TODO optimize
            if (i != 0) strncat(full_name, "->", sizeof(full_name));
        }
        entry_p = full_name;
    } else {
        jmethodID cur_method = info->methods[0]; // top of stack
        if (cur_method != root_method) {
            generate_unfolded_entry(jvmti, cur_method, inlined_name, sizeof(inlined_name), root_name);
            entry_p = inlined_name;
        } else
            entry_p = root_name;
    }

    ptrdiff_t method_len = ptr_diff_in_bytes(end_addr, start_addr);//static_cast<const char*>(end_addr) - static_cast<const char*>(start_addr);
    perf_map_write_entry(method_file, start_addr, method_len /*end_addr - start_addr*/, entry_p);
}

void dump_entries(
        jvmtiEnv *jvmti,
        jmethodID root_method,
        jint code_size,
        const void* code_addr,
        jint map_length,
        const jvmtiAddrLocationMap* map,
        const void* compile_info) {
    const jvmtiCompiledMethodLoadRecordHeader *header = 
	static_cast<const jvmtiCompiledMethodLoadRecordHeader *>(compile_info);
    char root_name[STRING_BUFFER_SIZE];
    sig_string(jvmti, root_method, root_name, sizeof(root_name));
    printf("At %s size %x from %p to %p", root_name, code_size, code_addr, code_addr + code_size);
    if (header->kind == JVMTI_CMLR_INLINE_INFO) {
        const jvmtiCompiledMethodLoadInlineRecord *record = (jvmtiCompiledMethodLoadInlineRecord *) header;
        printf(" with %d entries\n", record->numpcs);

        int i;
        for (i = 0; i < record->numpcs; i++) {
            PCStackInfo *info = &record->pcinfo[i];
            printf("  %p has %d stack entries\n", info->pc, info->numstackframes);

            int j;
            for (j = 0; j < info->numstackframes; j++) {
                char buf[2000];
                sig_string(jvmti, info->methods[j], buf, sizeof(buf));
                printf("    %s\n", buf);
            }
        }
    } else printf(" with no inline info\n");
}

void generate_unfolded_entries(
        jvmtiEnv *jvmti,
        jmethodID root_method,
        jint code_size,
        const void* code_addr,
        jint map_length,
        const jvmtiAddrLocationMap* map,
        const void* compile_info) {
    const jvmtiCompiledMethodLoadRecordHeader *header = 
	static_cast<const jvmtiCompiledMethodLoadRecordHeader *>(compile_info);
    char root_name[STRING_BUFFER_SIZE];

    sig_string(jvmti, root_method, root_name, sizeof(root_name));

    if (debug_dump_unfold_entries)
        dump_entries(jvmti, root_method, code_size, code_addr, map_length, map, compile_info);

    if (header->kind == JVMTI_CMLR_INLINE_INFO) {
        const jvmtiCompiledMethodLoadInlineRecord *record = (jvmtiCompiledMethodLoadInlineRecord *) header;

        const void *start_addr = code_addr;
        jmethodID cur_method = root_method;

        // walk through the method meta data per PC to extract address range
        // per inlined method.
        int i;
        for (i = 0; i < record->numpcs; i++) {
            PCStackInfo *info = &record->pcinfo[i];
            jmethodID top_method = info->methods[0];

            // as long as the top method remains the same we delay recording
            if (cur_method != top_method) {
                // top method has changed, record the range for current method
                void *end_addr = info->pc;

                if (i > 0) {
                    write_unfolded_entry(jvmti, &record->pcinfo[i - 1], root_method, root_name, start_addr, end_addr);
                } else {
		    ptrdiff_t entry_len = ptr_diff_in_bytes(end_addr, start_addr);
                    generate_single_entry(jvmti, root_method, start_addr, entry_len/*end_addr - start_addr*/);
		}

                start_addr = info->pc;
                cur_method = top_method;
            }
        }

        // record the last range if there's a gap
        if (start_addr != code_addr + code_size) {
            // end_addr is end of this complete code blob
            const void *end_addr = code_addr + code_size;

            if (i > 0) {
                write_unfolded_entry(jvmti, &record->pcinfo[i - 1], root_method, root_name, start_addr, end_addr);
            } else {
		ptrdiff_t entry_len = ptr_diff_in_bytes(end_addr, start_addr);
                generate_single_entry(jvmti, root_method, start_addr, entry_len/*end_addr - start_addr*/);
	    }
        }
    } else
        generate_single_entry(jvmti, root_method, code_addr, code_size);
}

static void JNICALL
cbCompiledMethodLoad(
            jvmtiEnv *jvmti,
            jmethodID method,
            jint code_size,
            const void* code_addr,
            jint map_length,
            const jvmtiAddrLocationMap* map,
            const void* compile_info) {
    /*
    if (unfold_inlined_methods && compile_info != NULL)
        generate_unfolded_entries(jvmti, method, code_size, code_addr, map_length, map, compile_info); 
    else
        generate_single_entry(jvmti, method, code_addr, code_size);
    */

    char entry[STRING_BUFFER_SIZE] = {};
    sig_string(jvmti, method, entry, sizeof(entry));
    auto& info = get_jit_info(method);
    //printf("load: %p@%s[%p/%d]\n", method, entry, code_addr, code_size);
    info.inner_methods.clear();
    compiled_method_info compiled_method(entry, (size_t)code_addr, (size_t)code_size);
    info.inner_methods.insert(compiled_method);

    //dump_perf_file();
}

static void JNICALL
cbCompiledMethodUnLoad(
    jvmtiEnv* jvmti,
    jmethodID method,
    const void* info) 
{
    //printf("Unload: %d\n", method);
    remove_jit_info(method);
}

void JNICALL
cbDynamicCodeGenerated(jvmtiEnv *jvmti,
            const char* name,
            const void* address,
            jint length) {
    perf_map_write_entry(method_file, address, length, name);
}

void set_notification_mode(jvmtiEnv *jvmti, jvmtiEventMode mode) {
    jvmti->SetEventNotificationMode(mode,
          JVMTI_EVENT_COMPILED_METHOD_LOAD, (jthread)NULL);
    jvmti->SetEventNotificationMode(mode,
          JVMTI_EVENT_DYNAMIC_CODE_GENERATED, (jthread)NULL);
    jvmti->SetEventNotificationMode(mode, JVMTI_EVENT_COMPILED_METHOD_UNLOAD, (jthread)NULL);
}

jvmtiError enable_capabilities(jvmtiEnv *jvmti) {
    jvmtiCapabilities capabilities;

    memset(&capabilities,0, sizeof(capabilities));
    capabilities.can_generate_all_class_hook_events  = 1;
    capabilities.can_tag_objects                     = 1;
    capabilities.can_generate_object_free_events     = 1;
    capabilities.can_get_source_file_name            = 1;
    capabilities.can_get_line_numbers                = 1;
    capabilities.can_generate_vm_object_alloc_events = 1;
    capabilities.can_generate_compiled_method_load_events = 1;

    // Request these capabilities for this JVM TI environment.
    return jvmti->AddCapabilities(&capabilities);
}

jvmtiError set_callbacks(jvmtiEnv *jvmti) {
    jvmtiEventCallbacks callbacks;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.CompiledMethodLoad  = &cbCompiledMethodLoad;
    callbacks.DynamicCodeGenerated = &cbDynamicCodeGenerated;
    callbacks.CompiledMethodUnload = &cbCompiledMethodUnLoad;
    return jvmti->SetEventCallbacks(&callbacks, (jint)sizeof(callbacks));
}

JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    open_map_file();

    unfold_simple = strstr(options, "unfoldsimple") != NULL;
    unfold_all = strstr(options, "unfoldall") != NULL;
    unfold_inlined_methods = strstr(options, "unfold") != NULL || unfold_simple || unfold_all;
    print_method_signatures = strstr(options, "msig") != NULL;
    print_source_loc = strstr(options, "sourcepos") != NULL;
    clean_class_names = strstr(options, "dottedclass") != NULL;
    debug_dump_unfold_entries = strstr(options, "debug_dump_unfold_entries") != NULL;

    jvmtiEnv *jvmti;
    vm->GetEnv((void **)&jvmti, JVMTI_VERSION_1);
    enable_capabilities(jvmti);
    set_callbacks(jvmti);
    set_notification_mode(jvmti, JVMTI_ENABLE);
    jvmti->GenerateEvents(JVMTI_EVENT_DYNAMIC_CODE_GENERATED);
    jvmti->GenerateEvents(JVMTI_EVENT_COMPILED_METHOD_LOAD);

    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
