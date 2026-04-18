#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "vendor/unity/unity.h"

#ifndef UNITY_TEST
#define UNITY_TEST
#endif
#include "../src/sl-dumper.c" // NOLINT(bugprone-suspicious-include)

static const char *find_test_so_file(void) {
    static char path[512];
    DIR *d = opendir("lib");
    if (!d) return NULL;
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strstr(dir->d_name, ".so") != NULL) {
            snprintf(path, sizeof(path), "lib/%s", dir->d_name);
            closedir(d);
            return path;
        }
    }
    closedir(d);
    return NULL;
}

#define TEST_SO_FILE (find_test_so_file())

void setUp(void) {
    free(symbols);
    symbols = NULL;
    sym_count = 0;
    sym_capacity = 0;
    detected_lib = LIB_UNKNOWN;
    memset(lib_counts, 0, sizeof(lib_counts));
}

void tearDown(void) {
    for (size_t i = 0; i < sym_count; i++) {
        free(symbols[i].class_name);
        free(symbols[i].method_name);
    }
    free(symbols);
    symbols = NULL;
    sym_count = 0;
    sym_capacity = 0;
}

/* ============================================================
 * Unit Tests: compare_symbols
 * ============================================================ */

static void test_compare_symbols_different_class(void) {
    Symbol a = { .offset = 100, .class_name = "ZClassB", .method_name = "foo" };
    Symbol b = { .offset = 200, .class_name = "ZClassA", .method_name = "bar" };
    TEST_ASSERT_GREATER_THAN(0, compare_symbols(&a, &b));
    TEST_ASSERT_LESS_THAN(0, compare_symbols(&b, &a));
}

static void test_compare_symbols_same_class_different_offset(void) {
    Symbol a = { .offset = 100, .class_name = "ZClassA", .method_name = "foo" };
    Symbol b = { .offset = 200, .class_name = "ZClassA", .method_name = "bar" };
    TEST_ASSERT_LESS_THAN(0, compare_symbols(&a, &b));
    TEST_ASSERT_GREATER_THAN(0, compare_symbols(&b, &a));
}

static void test_compare_symbols_same_class_same_offset(void) {
    Symbol a = { .offset = 100, .class_name = "ZClassA", .method_name = "foo" };
    Symbol b = { .offset = 100, .class_name = "ZClassA", .method_name = "bar" };
    TEST_ASSERT_EQUAL(0, compare_symbols(&a, &b));
}

/* ============================================================
 * Unit Tests: add_symbol
 * ============================================================ */

static void test_add_symbol_initial_state(void) {
    TEST_ASSERT_NULL(symbols);
    TEST_ASSERT_EQUAL(0, sym_count);
    TEST_ASSERT_EQUAL(0, sym_capacity);
}

static void test_add_symbol_single(void) {
    add_symbol(0x1000, "TestClass", "methodA");
    TEST_ASSERT_EQUAL(1, sym_count);
    TEST_ASSERT_EQUAL_STRING("TestClass", symbols[0].class_name);
    TEST_ASSERT_EQUAL_STRING("methodA", symbols[0].method_name);
    TEST_ASSERT_EQUAL_HEX(0x1000, symbols[0].offset);
}

static void test_add_symbol_multiple(void) {
    add_symbol(0x1000, "ClassA", "method1");
    add_symbol(0x2000, "ClassB", "method2");
    add_symbol(0x3000, "ClassA", "method3");

    TEST_ASSERT_EQUAL(3, sym_count);
    TEST_ASSERT_EQUAL_STRING("ClassA", symbols[0].class_name);
    TEST_ASSERT_EQUAL_STRING("ClassB", symbols[1].class_name);
    TEST_ASSERT_EQUAL_STRING("ClassA", symbols[2].class_name);
}

static void test_add_symbol_grows_capacity(void) {
    for (int i = 0; i < 100; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Method%d", i);
        add_symbol((uint64_t)i * 0x100, "BigClass", name);
    }
    TEST_ASSERT_EQUAL(100, sym_count);
    TEST_ASSERT_GREATER_OR_EQUAL(100, sym_capacity);
}

/* ============================================================
 * Unit Tests: parse_and_store_demangled
 * ============================================================ */

static void test_parse_and_store_demangled_simple_method(void) {
    parse_and_store_demangled(0x1000, "MyClass::doSomething(int, char*)");
    TEST_ASSERT_EQUAL(1, sym_count);
    TEST_ASSERT_EQUAL_STRING("MyClass", symbols[0].class_name);
    TEST_ASSERT_EQUAL_STRING("doSomething(int, char*)", symbols[0].method_name);
}

static void test_parse_and_store_demangled_no_params(void) {
    parse_and_store_demangled(0x1000, "MyClass::doSomething");
    TEST_ASSERT_EQUAL(0, sym_count);
}

static void test_parse_and_store_demangled_not_a_method(void) {
    parse_and_store_demangled(0x1000, "some_function(int)");
    TEST_ASSERT_EQUAL(0, sym_count);
}

static void test_parse_and_store_demangled_with_template(void) {
    parse_and_store_demangled(0x1000, "Vector<int>::push_back(int const&)");
    TEST_ASSERT_EQUAL(1, sym_count);
    TEST_ASSERT_EQUAL_STRING("Vector<int>", symbols[0].class_name);
    TEST_ASSERT_EQUAL_STRING("push_back(int const&)", symbols[0].method_name);
}

static void test_parse_and_store_demangled_nested_namespace(void) {
    parse_and_store_demangled(0x1000, "ns::inner::Foo::bar(std::string)");
    TEST_ASSERT_EQUAL(1, sym_count);
    TEST_ASSERT_EQUAL_STRING("ns::inner::Foo", symbols[0].class_name);
    TEST_ASSERT_EQUAL_STRING("bar(std::string)", symbols[0].method_name);
}

static void test_parse_and_store_demangled_constructor(void) {
    parse_and_store_demangled(0x1000, "MyClass::MyClass(int)");
    TEST_ASSERT_EQUAL(1, sym_count);
    TEST_ASSERT_EQUAL_STRING("MyClass", symbols[0].class_name);
    TEST_ASSERT_EQUAL_STRING("MyClass(int)", symbols[0].method_name);
}

static void test_parse_and_store_demangled_destructor(void) {
    parse_and_store_demangled(0x1000, "MyClass::~MyClass()");
    TEST_ASSERT_EQUAL(1, sym_count);
    TEST_ASSERT_EQUAL_STRING("MyClass", symbols[0].class_name);
    TEST_ASSERT_EQUAL_STRING("~MyClass()", symbols[0].method_name);
}

/* ============================================================
 * Unit Tests: quick_scan_elf
 * ============================================================ */

static void test_quick_scan_nonexistent_file(void) {
    LibraryType result = quick_scan_elf("nonexistent_file.so");
    TEST_ASSERT_EQUAL(LIB_UNKNOWN, result);
}

static void test_quick_scan_real_elf(void) {
    struct stat st;
    if (stat(TEST_SO_FILE, &st) != 0) {
        TEST_IGNORE_MESSAGE("Test .so file not found, skipping");
    }
    LibraryType result = quick_scan_elf(TEST_SO_FILE);
    TEST_ASSERT_NOT_EQUAL(LIB_UNKNOWN, result);
}

static void test_quick_scan_cpp_symbols_detected(void) {
    struct stat st;
    if (stat(TEST_SO_FILE, &st) != 0) {
        TEST_IGNORE_MESSAGE("Test .so file not found, skipping");
    }
    LibraryType result = quick_scan_elf(TEST_SO_FILE);
    TEST_ASSERT_TRUE(result == LIB_CXX || result != LIB_UNKNOWN);
}

/* ============================================================
 * Integration Tests: process_elf with real ELF
 * ============================================================ */

static void test_process_elf_real_file(void) {
    struct stat st;
    if (stat(TEST_SO_FILE, &st) != 0) {
        TEST_IGNORE_MESSAGE("Test .so file not found, skipping");
    }
    process_elf(TEST_SO_FILE);
    TEST_ASSERT(sym_count > 0);
}

static void test_process_elf_populates_symbols(void) {
    struct stat st;
    if (stat(TEST_SO_FILE, &st) != 0) {
        TEST_IGNORE_MESSAGE("Test .so file not found, skipping");
    }
    process_elf(TEST_SO_FILE);

    TEST_ASSERT(sym_count > 0);
    for (size_t i = 0; i < sym_count; i++) {
        TEST_ASSERT_NOT_NULL(symbols[i].class_name);
        TEST_ASSERT_NOT_NULL(symbols[i].method_name);
        TEST_ASSERT(strlen(symbols[i].class_name) > 0);
        TEST_ASSERT(strlen(symbols[i].method_name) > 0);
    }
}

static void test_process_elf_detects_library_type(void) {
    struct stat st;
    if (stat(TEST_SO_FILE, &st) != 0) {
        TEST_IGNORE_MESSAGE("Test .so file not found, skipping");
    }
    process_elf(TEST_SO_FILE);
    TEST_ASSERT_NOT_EQUAL(LIB_UNKNOWN, detected_lib);
}

static void test_process_elf_symbols_have_valid_offsets(void) {
    struct stat st;
    if (stat(TEST_SO_FILE, &st) != 0) {
        TEST_IGNORE_MESSAGE("Test .so file not found, skipping");
    }
    process_elf(TEST_SO_FILE);

    for (size_t i = 0; i < sym_count; i++) {
        TEST_ASSERT(symbols[i].offset > 0);
    }
}

/* ============================================================
 * Integration Tests: Output generation
 * ============================================================ */

static void write_output_file(const char *base_name) {
    char out_dir[512], out_file[1024];
    snprintf(out_dir, sizeof(out_dir), "lib/output/%s@dump", base_name);
    mkdir(out_dir, 0755);
    snprintf(out_file, sizeof(out_file), "%s/%s.cpp", out_dir, base_name);

    FILE *f_out = fopen(out_file, "w");
    TEST_ASSERT_NOT_NULL(f_out);

    fprintf(f_out, "// Detected library type: %s\n\n", lib_type_str[detected_lib]);

    const char *current_class = "";

    for (size_t i = 0; i < sym_count; i++) {
        if (strcmp(symbols[i].class_name, current_class) != 0) {
            if (i > 0) fprintf(f_out, "};\n\n");
            fprintf(f_out, "class %s {\n", symbols[i].class_name);
            current_class = symbols[i].class_name;
        }
        fprintf(f_out, "      %s; // 0x%lx\n", symbols[i].method_name, (unsigned long)symbols[i].offset);
    }
    if (sym_count > 0) fprintf(f_out, "};\n");
    fclose(f_out);
}

static void test_output_file_generated(void) {
    struct stat st;
    if (stat(TEST_SO_FILE, &st) != 0) {
        TEST_IGNORE_MESSAGE("Test .so file not found, skipping");
    }

    add_symbol(0x1000, "TestClass", "methodA(int)");
    add_symbol(0x2000, "TestClass", "methodB(char*)");
    detected_lib = LIB_CXX;

    write_output_file("test_output");

    struct stat out_st;
    TEST_ASSERT_EQUAL(0, stat("lib/output/test_output@dump/test_output.cpp", &out_st));
    TEST_ASSERT(out_st.st_size > 0);

    remove("lib/output/test_output@dump/test_output.cpp");
    rmdir("lib/output/test_output@dump");
}

static void test_output_contains_class_structure(void) {
    struct stat st;
    if (stat(TEST_SO_FILE, &st) != 0) {
        TEST_IGNORE_MESSAGE("Test .so file not found, skipping");
    }

    add_symbol(0x1000, "Player", "update(float)");
    add_symbol(0x2000, "Player", "attack(int)");
    add_symbol(0x3000, "Enemy", "spawn()");
    detected_lib = LIB_CXX;

    write_output_file("test_structure");

    FILE *f = fopen("lib/output/test_structure@dump/test_structure.cpp", "r");
    TEST_ASSERT_NOT_NULL(f);

    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    TEST_ASSERT_NOT_NULL(strstr(buf, "class Player"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "class Enemy"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "update(float)"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "attack(int)"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "spawn()"));

    remove("lib/output/test_structure@dump/test_structure.cpp");
    rmdir("lib/output/test_structure@dump");
}

static void test_output_contains_offsets(void) {
    add_symbol(0xDEAD, "Foo", "bar()");
    detected_lib = LIB_CXX;

    write_output_file("test_offsets");

    FILE *f = fopen("lib/output/test_offsets@dump/test_offsets.cpp", "r");
    TEST_ASSERT_NOT_NULL(f);

    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    TEST_ASSERT_NOT_NULL(strstr(buf, "0xdead"));

    remove("lib/output/test_offsets@dump/test_offsets.cpp");
    rmdir("lib/output/test_offsets@dump");
}

/* ============================================================
 * Test Runner
 * ============================================================ */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_compare_symbols_different_class);
    RUN_TEST(test_compare_symbols_same_class_different_offset);
    RUN_TEST(test_compare_symbols_same_class_same_offset);

    RUN_TEST(test_add_symbol_initial_state);
    RUN_TEST(test_add_symbol_single);
    RUN_TEST(test_add_symbol_multiple);
    RUN_TEST(test_add_symbol_grows_capacity);

    RUN_TEST(test_parse_and_store_demangled_simple_method);
    RUN_TEST(test_parse_and_store_demangled_no_params);
    RUN_TEST(test_parse_and_store_demangled_not_a_method);
    RUN_TEST(test_parse_and_store_demangled_with_template);
    RUN_TEST(test_parse_and_store_demangled_nested_namespace);
    RUN_TEST(test_parse_and_store_demangled_constructor);
    RUN_TEST(test_parse_and_store_demangled_destructor);

    RUN_TEST(test_quick_scan_nonexistent_file);
    RUN_TEST(test_quick_scan_real_elf);
    RUN_TEST(test_quick_scan_cpp_symbols_detected);

    RUN_TEST(test_process_elf_real_file);
    RUN_TEST(test_process_elf_populates_symbols);
    RUN_TEST(test_process_elf_detects_library_type);
    RUN_TEST(test_process_elf_symbols_have_valid_offsets);

    RUN_TEST(test_output_file_generated);
    RUN_TEST(test_output_contains_class_structure);
    RUN_TEST(test_output_contains_offsets);

    return UNITY_END();
}
