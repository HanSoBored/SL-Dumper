#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <elf.h>
#include <time.h>
#include <errno.h>
// Native demangling (linked with -lstdc++)
extern char* __cxa_demangle(const char* mangled_name, char* output_buffer, size_t* length, int* status);

// --- UI / UX Colors ---
#define C_RST   "\x1b[0m"
#define C_CYAN  "\x1b[36m"
#define C_PINK  "\x1b[95m"
#define C_GREEN "\x1b[32m"
#define C_DIM   "\x1b[2m"
#define C_BOLD  "\x1b[1m"
#define C_ERR   "\x1b[31;1m"

// --- UI Text ---
#define UI_SELECT_LIB     "Select Library to dump:"
#define UI_ENTER_NUMBER   "Enter number"
#define UI_EXITING        "Exiting..."
#define UI_NO_SO_FILES    "No .so files found in this directory."
#define UI_OPEN_FAILED    "Failed to open %s"
#define UI_NOT_VALID_ELF  "File is not a valid ELF format."
#define UI_NO_SYMBOLS     "No symbol table (.dynsym) found in this ELF."
#define UI_PROCESSING     "Processing"
#define UI_DUMP_COMPLETE  "Dump completed at native speed!"
#define UI_CLASSES_FOUND  "Classes found"
#define UI_METHODS_FOUND  "Methods found"
#define UI_SAVED_AT       "Saved at"
#define UI_EXEC_TIME      "Execution time"

// --- Data Structures ---
typedef enum {
    LIB_UNKNOWN,
    LIB_CXX,
    LIB_RUST,
    LIB_GO,
    LIB_SWIFT
} LibraryType;

// Pre-declare quick_scan_elf for forward reference
LibraryType quick_scan_elf(const char *lib_path);

typedef struct {
    uint64_t offset;
    char *class_name;
    char *method_name;
} Symbol;

Symbol *symbols = NULL;
size_t sym_count = 0;
size_t sym_capacity = 0;

LibraryType detected_lib = LIB_UNKNOWN;

// Library type detection counters
static int lib_counts[5] = {0, 0, 0, 0, 0};

// Library type string mapping
static const char *lib_type_str[] = {"Unknown", "C++", "Rust", "Go", "Swift"};

// Shared ELF section parsing result
typedef struct {
    const uint8_t *symtab;
    const char *strtab;
    uint64_t sym_size;
    uint64_t sym_ent;
    uint64_t strtab_size;
    bool is_64;
    bool found;
} ElfSections;

void clear_screen() {
    printf("\x1b[1;1H\x1b[2J");
    fflush(stdout);
}

// Shared ELF section header parsing (M1: deduplicated)
static bool elf_find_sections(const uint8_t *mem, size_t file_size, ElfSections *out) {
    memset(out, 0, sizeof(ElfSections));

    bool is_64 = (mem[4] == ELFCLASS64);
    out->is_64 = is_64;

    Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)mem;
    Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)mem;

    uint64_t shoff = is_64 ? ehdr64->e_shoff : ehdr32->e_shoff;
    uint16_t shnum = is_64 ? ehdr64->e_shnum : ehdr32->e_shnum;

    // H5: Section header bounds validation
    size_t shdr_size = is_64 ? sizeof(Elf64_Shdr) : sizeof(Elf32_Shdr);
    if (shoff + (uint64_t)shnum * shdr_size > (uint64_t)file_size) {
        return false;
    }

    // Find Dynamic Symbols Table (.dynsym) and String Table (.dynstr)
    for (int i = 0; i < shnum; i++) {
        uint32_t sh_type = is_64
            ? ((Elf64_Shdr *)(mem + shoff + i * sizeof(Elf64_Shdr)))->sh_type
            : ((Elf32_Shdr *)(mem + shoff + i * sizeof(Elf32_Shdr)))->sh_type;

        if (sh_type == SHT_DYNSYM) {
            out->symtab = mem + (is_64
                ? ((Elf64_Shdr *)(mem + shoff + i * sizeof(Elf64_Shdr)))->sh_offset
                : ((Elf32_Shdr *)(mem + shoff + i * sizeof(Elf32_Shdr)))->sh_offset);
            out->sym_size = is_64
                ? ((Elf64_Shdr *)(mem + shoff + i * sizeof(Elf64_Shdr)))->sh_size
                : ((Elf32_Shdr *)(mem + shoff + i * sizeof(Elf32_Shdr)))->sh_size;
            out->sym_ent = is_64
                ? ((Elf64_Shdr *)(mem + shoff + i * sizeof(Elf64_Shdr)))->sh_entsize
                : ((Elf32_Shdr *)(mem + shoff + i * sizeof(Elf32_Shdr)))->sh_entsize;

            uint32_t link = is_64
                ? ((Elf64_Shdr *)(mem + shoff + i * sizeof(Elf64_Shdr)))->sh_link
                : ((Elf32_Shdr *)(mem + shoff + i * sizeof(Elf32_Shdr)))->sh_link;

            // N1: Validate link index bounds (SHN_UNDEF = 0, or malformed ELF)
            if (link == 0 || link >= shnum) return false;

            out->strtab = (const char *)(mem + (is_64
                ? ((Elf64_Shdr *)(mem + shoff + link * sizeof(Elf64_Shdr)))->sh_offset
                : ((Elf32_Shdr *)(mem + shoff + link * sizeof(Elf32_Shdr)))->sh_offset));

            // H4: Capture strtab_size for bounds checking
            out->strtab_size = is_64
                ? ((Elf64_Shdr *)(mem + shoff + link * sizeof(Elf64_Shdr)))->sh_size
                : ((Elf32_Shdr *)(mem + shoff + link * sizeof(Elf32_Shdr)))->sh_size;

            out->found = true;
            break;
        }
    }

    return out->found;
}

void add_symbol(uint64_t offset, const char *class_name, const char *method_name) {
    if (sym_count >= sym_capacity) {
        sym_capacity = sym_capacity == 0 ? 64 : sym_capacity * 2;
        // H1: Safe realloc with NULL check
        Symbol *tmp = realloc(symbols, sym_capacity * sizeof(Symbol));
        if (!tmp) {
            fprintf(stderr, "Error: Out of memory\n");
            exit(1);
        }
        symbols = tmp;
    }
    // N2: Check strdup returns for OOM safety
    char *cn = strdup(class_name);
    char *mn = strdup(method_name);
    if (!cn || !mn) {
        free(cn);
        free(mn);
        fprintf(stderr, "Error: Out of memory\n");
        exit(1);
    }
    symbols[sym_count].offset = offset;
    symbols[sym_count].class_name = cn;
    symbols[sym_count].method_name = mn;
    sym_count++;
}

// Comparison function for qsort (Sorting by Class Name -> Offset)
int compare_symbols(const void *a, const void *b) {
    Symbol *sa = (Symbol *)a;
    Symbol *sb = (Symbol *)b;
    int cmp = strcmp(sa->class_name, sb->class_name);
    if (cmp == 0) {
        if (sa->offset < sb->offset) return -1;
        if (sa->offset > sb->offset) return 1;
        return 0;
    }
    return cmp;
}

// Parse C++ signature that has been demangled
void parse_and_store_demangled(uint64_t offset, const char *demangled) {
    int depth = 0;
    const char *sig_start = NULL;

    // Cari parameter pertama '(' di luar template '<>'
    for (int i = 0; demangled[i] != '\0'; i++) {
        if (demangled[i] == '<') depth++;
        else if (demangled[i] == '>') depth--;
        else if (demangled[i] == '(' && depth == 0) {
            sig_start = demangled + i;
            break;
        }
    }

    if (!sig_start) return; // Bukan method dengan parameter

    // Mundur dari '(' untuk mencari '::'
    const char *method_start = NULL;
    for (const char *p = sig_start - 2; p >= demangled; p--) {
        if (p >= demangled + 1 && p[0] == ':' && p[-1] == ':') {
            method_start = p + 1;
            break;
        }
    }

    if (method_start) {
        size_t class_len = method_start - 2 - demangled;
        char class_name[256];
        if (class_len >= sizeof(class_name)) class_len = sizeof(class_name) - 1;
        strncpy(class_name, demangled, class_len);
        class_name[class_len] = '\0';

        add_symbol(offset, class_name, method_start);
    }
}

// Dissect ELF file natively into memory
void process_elf(const char *lib_path) {
    int fd = open(lib_path, O_RDONLY);
    if (fd < 0) {
        printf(C_ERR " " UI_OPEN_FAILED "\n" C_RST, lib_path);
        exit(1);
    }

    struct stat st;
    // M6: Check fstat return value
    if (fstat(fd, &st) != 0) {
        perror("fstat failed");
        close(fd);
        exit(1);
    }

    uint8_t *mem = (uint8_t *)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    // H3: Check mmap result FIRST before dereferencing
    if (mem == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    // PE/Windows DLL detection (after mmap check)
    if (mem[0] == 'M' && mem[1] == 'Z') {
        printf(C_ERR " Error: PE/DLL file detected. This tool supports ELF only.\n" C_RST);
        munmap(mem, st.st_size);
        exit(1);
    }

    if (mem[0] != 0x7f || mem[1] != 'E' || mem[2] != 'L' || mem[3] != 'F') {
        printf(C_ERR " " UI_NOT_VALID_ELF "\n" C_RST);
        munmap(mem, st.st_size);
        exit(1);
    }

    ElfSections sections;
    if (!elf_find_sections(mem, st.st_size, &sections)) {
        printf(C_ERR " " UI_NO_SYMBOLS "\n" C_RST);
        munmap(mem, st.st_size);
        exit(1);
    }

    // H5: Division-by-zero guard for sym_ent
    if (sections.sym_ent == 0) {
        printf(C_ERR " " UI_NO_SYMBOLS "\n" C_RST);
        munmap(mem, st.st_size);
        exit(1);
    }

    // Iterate symbols
    int count = sections.sym_size / sections.sym_ent;
    for (int i = 0; i < count; i++) {
        uint64_t st_value;
        uint32_t st_name;
        unsigned char st_info;

        if (sections.is_64) {
            Elf64_Sym *sym = (Elf64_Sym *)(sections.symtab + i * sections.sym_ent);
            st_value = sym->st_value;
            st_name = sym->st_name;
            st_info = sym->st_info;
        } else {
            Elf32_Sym *sym = (Elf32_Sym *)(sections.symtab + i * sections.sym_ent);
            st_value = sym->st_value;
            st_name = sym->st_name;
            st_info = sym->st_info;
        }

        if (st_value == 0) continue;

        // H2: Use correct ST_TYPE macro for 32/64-bit
        unsigned char sym_type = sections.is_64
            ? ELF64_ST_TYPE(st_info)
            : ELF32_ST_TYPE(st_info);
        if (sym_type != STT_FUNC && sym_type != STT_OBJECT) continue;

        // H4: strtab bounds check
        if (st_name >= sections.strtab_size) continue;

        const char *name = sections.strtab + st_name;

        // Library type detection based on symbol patterns
        if (strncmp(name, "_Z", 2) == 0) {
            lib_counts[LIB_CXX]++;  // C++ mangled symbol
        } else if (strncmp(name, "rust_", 5) == 0) {
            lib_counts[LIB_RUST]++;  // Rust symbol
            // TODO: Implement Rust symbol dumping (e.g., rust_oom_hook, core::, alloc::)
        } else if (strncmp(name, "runtime.", 8) == 0 || strncmp(name, "go.", 3) == 0) {
            lib_counts[LIB_GO]++;  // Go symbol
            // TODO: Implement Go symbol dumping (e.g., runtime.makeslice, go.buildid)
        } else if (strncmp(name, "$s", 2) == 0 || strncmp(name, "swift_", 6) == 0) {
            lib_counts[LIB_SWIFT]++;  // Swift symbol
            // TODO: Implement Swift symbol dumping (e.g., $s4main, swift_autoreleasePool)
        }

        // Demangle and store C++ symbols
        if (strncmp(name, "_Z", 2) == 0) {
            int status = -1;
            char *demangled = __cxa_demangle(name, NULL, NULL, &status);
            if (status == 0 && demangled) {
                parse_and_store_demangled(st_value, demangled);
                free(demangled);
            }
        }
    }

    // Determine final library type based on highest count
    int max_idx = 0;
    for (int i = 1; i < 5; i++) {
        if (lib_counts[i] > lib_counts[max_idx]) {
            max_idx = i;
        }
    }
    if (lib_counts[max_idx] > 0) {
        detected_lib = (LibraryType)max_idx;
    }

    munmap(mem, st.st_size);
}

// Quick scan ELF to detect library type (scans first ~100 symbols only)
LibraryType quick_scan_elf(const char *lib_path) {
    int fd = open(lib_path, O_RDONLY);
    if (fd < 0) return LIB_UNKNOWN;

    struct stat st;
    // M6: Check fstat return value
    if (fstat(fd, &st) != 0) {
        close(fd);
        return LIB_UNKNOWN;
    }

    uint8_t *mem = (uint8_t *)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    // H3: Check mmap result FIRST before dereferencing
    if (mem == MAP_FAILED) {
        return LIB_UNKNOWN;
    }

    // ELF magic check (PE files already rejected here — no separate check needed)
    if (mem[0] != 0x7f || mem[1] != 'E' || mem[2] != 'L' || mem[3] != 'F') {
        munmap(mem, st.st_size);
        return LIB_UNKNOWN;
    }

    ElfSections sections;
    if (!elf_find_sections(mem, st.st_size, &sections)) {
        munmap(mem, st.st_size);
        return LIB_UNKNOWN;
    }

    // Avoid division by zero
    if (sections.sym_ent == 0) {
        munmap(mem, st.st_size);
        return LIB_UNKNOWN;
    }

    // Quick scan only first 100 symbols
    int scan_count = sections.sym_size / sections.sym_ent;
    if (scan_count > 100) scan_count = 100;

    int quick_counts[5] = {0, 0, 0, 0, 0};

    for (int i = 0; i < scan_count; i++) {
        uint64_t st_value;
        uint32_t st_name;
        unsigned char st_info;

        if (sections.is_64) {
            Elf64_Sym *sym = (Elf64_Sym *)(sections.symtab + i * sections.sym_ent);
            st_value = sym->st_value;
            st_name = sym->st_name;
            st_info = sym->st_info;
        } else {
            Elf32_Sym *sym = (Elf32_Sym *)(sections.symtab + i * sections.sym_ent);
            st_value = sym->st_value;
            st_name = sym->st_name;
            st_info = sym->st_info;
        }

        if (st_value == 0) continue;

        // H2: Use correct ST_TYPE macro for 32/64-bit
        unsigned char sym_type = sections.is_64
            ? ELF64_ST_TYPE(st_info)
            : ELF32_ST_TYPE(st_info);
        if (sym_type != STT_FUNC && sym_type != STT_OBJECT) continue;

        // H4: strtab bounds check
        if (st_name >= sections.strtab_size) continue;

        const char *name = sections.strtab + st_name;

        // Library type detection based on symbol patterns
        if (strncmp(name, "_Z", 2) == 0) {
            quick_counts[LIB_CXX]++;
        } else if (strncmp(name, "rust_", 5) == 0) {
            quick_counts[LIB_RUST]++;
        } else if (strncmp(name, "runtime.", 8) == 0 || strncmp(name, "go.", 3) == 0) {
            quick_counts[LIB_GO]++;
        } else if (strncmp(name, "$s", 2) == 0 || strncmp(name, "swift_", 6) == 0) {
            quick_counts[LIB_SWIFT]++;
        }
    }

    // Determine final library type based on highest count
    int max_idx = 0;
    for (int i = 1; i < 5; i++) {
        if (quick_counts[i] > quick_counts[max_idx]) {
            max_idx = i;
        }
    }

    munmap(mem, st.st_size);

    if (quick_counts[max_idx] > 0) {
        return (LibraryType)max_idx;
    }
    return LIB_UNKNOWN;
}

void print_banner() {
    printf(C_CYAN "\n╔════════════════════════════════════════╗\n");
    printf("║   SL-Dumper - Fast ELF Library Parser  ║\n");
    printf("╚════════════════════════════════════════╝\n\n" C_RST);
}

#ifndef UNITY_TEST

// Check if root directory contains .so files
static int has_so_in_root(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strstr(dir->d_name, ".so") != NULL) {
            closedir(d);
            return 1;
        }
    }
    closedir(d);
    return 0;
}

int main() {
    print_banner();

    // Reject .so files in root directory
    if (has_so_in_root(".")) {
        printf(C_ERR " Error: .so files found in project root.\n" C_RST);
        printf(C_DIM " Please move them to lib/ directory:\n" C_RST);
        printf(C_DIM "   mkdir -p lib && mv *.so lib/\n" C_RST);
        return 1;
    }

    // 1. Cari File .so di lib/ directory
    // M3: Dynamic allocation instead of fixed array
    char **so_files = NULL;
    LibraryType *lib_types = NULL;
    int file_capacity = 0;
    int file_count = 0;

    DIR *d = opendir("lib");
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".so") != NULL) {
                if (file_count >= file_capacity) {
                    file_capacity = file_capacity == 0 ? 64 : file_capacity * 2;
                    char **tmp_files = realloc(so_files, file_capacity * sizeof(char *));
                    if (!tmp_files) {
                        fprintf(stderr, "Error: Out of memory\n");
                        for (int i = 0; i < file_count; i++) free(so_files[i]);
                        free(so_files);
                        free(lib_types);
                        closedir(d);
                        return 1;
                    }
                    so_files = tmp_files;
                    LibraryType *tmp_types = realloc(lib_types, file_capacity * sizeof(LibraryType));
                    if (!tmp_types) {
                        fprintf(stderr, "Error: Out of memory\n");
                        for (int i = 0; i < file_count; i++) free(so_files[i]);
                        free(so_files);
                        closedir(d);
                        return 1;
                    }
                    lib_types = tmp_types;
                }
                so_files[file_count] = strdup(dir->d_name);
                lib_types[file_count] = LIB_UNKNOWN;
                file_count++;
            }
        }
        closedir(d);
    }

    if (file_count == 0) {
        printf(C_ERR " " UI_NO_SO_FILES "\n" C_RST);
        printf(C_DIM " Place .so files in lib/ directory.\n" C_RST);
        free(so_files);
        free(lib_types);
        return 1;
    }

    // Quick scan all files to detect library types before showing menu
    for (int i = 0; i < file_count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "lib/%s", so_files[i]);
        lib_types[i] = quick_scan_elf(path);
    }

    printf(C_DIM " " UI_SELECT_LIB "\n" C_RST);
    for (int i = 0; i < file_count; i++) {
        printf(C_CYAN "%d" C_RST " %s (" C_PINK "%s" C_RST ")\n", i + 1, so_files[i], lib_type_str[lib_types[i]]);
    }

    printf("\n" C_PINK " ➔ " UI_ENTER_NUMBER " " C_DIM "(0 to exit): " C_RST);
    int choice;
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > file_count) {
        printf(C_DIM "\n" UI_EXITING "\n" C_RST);
        for (int i = 0; i < file_count; i++) free(so_files[i]);
        free(so_files);
        free(lib_types);
        return 0;
    }

    char lib_path[512];
    snprintf(lib_path, sizeof(lib_path), "lib/%s", so_files[choice - 1]);

    // Membersihkan string untuk nama folder/file
    char base_name[256];
    strncpy(base_name, lib_path, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *ext = strrchr(base_name, '.');
    if (ext) *ext = '\0';
    // L2: Fix memmove length calculation
    if (strncmp(base_name, "lib", 3) == 0) {
        memmove(base_name, base_name + 3, strlen(base_name + 3) + 1);
    }

    print_banner();
    printf(C_CYAN "" C_RST " " UI_PROCESSING " " C_BOLD "%s" C_RST "...\n", lib_path);

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // 2. Execute Native Parser (Super Fast)
    memset(lib_counts, 0, sizeof(lib_counts));
    process_elf(lib_path);

    // 3. Sort results by Class Name
    qsort(symbols, sym_count, sizeof(Symbol), compare_symbols);

    // 4. Generate Output File
    char out_dir[512], out_file[1024];
    snprintf(out_dir, sizeof(out_dir), "%s@dump", base_name);
    // M5 + L3: Check mkdir return value with proper permissions
    if (mkdir(out_dir, 0755) != 0 && errno != EEXIST) {
        perror("Failed to create output directory");
        // Cleanup
        for (size_t i = 0; i < sym_count; i++) {
            free(symbols[i].class_name);
            free(symbols[i].method_name);
        }
        free(symbols);
        for (int i = 0; i < file_count; i++) free(so_files[i]);
        free(so_files);
        free(lib_types);
        return 1;
    }
    snprintf(out_file, sizeof(out_file), "%s/%s.cpp", out_dir, base_name);

    FILE *f_out = fopen(out_file, "w");
    if (!f_out) {
        printf(C_ERR " Failed to create output file.\n" C_RST);
        // Cleanup
        for (size_t i = 0; i < sym_count; i++) {
            free(symbols[i].class_name);
            free(symbols[i].method_name);
        }
        free(symbols);
        for (int i = 0; i < file_count; i++) free(so_files[i]);
        free(so_files);
        free(lib_types);
        return 1;
    }

    // Write detected library type comment
    fprintf(f_out, "// Detected library type: %s\n\n", lib_type_str[detected_lib]);

    const char *current_class = "";
    int class_count = 0;

    for (size_t i = 0; i < sym_count; i++) {
        if (strcmp(symbols[i].class_name, current_class) != 0) {
            if (i > 0) fprintf(f_out, "};\n\n");
            fprintf(f_out, "class %s {\n", symbols[i].class_name);
            current_class = symbols[i].class_name;
            class_count++;
        }
        fprintf(f_out, "      %s; // 0x%lx\n", symbols[i].method_name, (unsigned long)symbols[i].offset);
    }
    if (sym_count > 0) fprintf(f_out, "};\n");
    fclose(f_out);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    // 5. UX - Result Summary
    printf(C_GREEN "" C_RST " " UI_DUMP_COMPLETE "\n\n");
    printf(C_DIM " ├─" C_RST " " UI_CLASSES_FOUND "  : " C_PINK "%d\n" C_RST, class_count);
    printf(C_DIM " ├─" C_RST " " UI_METHODS_FOUND " : " C_CYAN "%zu\n" C_RST, sym_count);
    printf(C_DIM " ├─" C_RST " " UI_SAVED_AT "      : " C_BOLD "%s\n" C_RST, out_file);
    printf(C_DIM " └─" C_RST " " UI_EXEC_TIME "    : " C_GREEN "%.3f seconds\n" C_RST, elapsed);

    // Cleanup memory
    for (size_t i = 0; i < sym_count; i++) {
        free(symbols[i].class_name);
        free(symbols[i].method_name);
    }
    free(symbols);

    // Cleanup so_files
    for (int i = 0; i < file_count; i++) free(so_files[i]);
    free(so_files);
    free(lib_types);

    return 0;
}
#endif
