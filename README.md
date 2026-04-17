# 🧬 SL-Dumper

> Fast ELF symbol parser & class dumper for shared libraries (.so)

SL-Dumper (Shared Library Dumper) is a lightweight tool written in C that scans ELF binaries (`.so` files), demangles symbol names, and generates a structured class dump in a `.cpp` file. It auto-detects library type (C++, Rust, Go, Swift) and uses **native ELF parsing** for maximum speed.

---

## Features

- 🔍 **Native ELF parsing** – mmap‑based, no external dependencies.
- ⚡ **Blazing fast** – parses thousands of symbols in milliseconds.
- 🧩 **C++ name demangling** – uses `__cxa_demangle` to get readable class and method names.
- 📁 **Automatic grouping** – methods are grouped by class in the output file.
- 🖥️ **Interactive selection** – choose which `.so` file to dump from current directory.
- 📦 **Zero runtime overhead** – generates a clean `.cpp` file ready for inspection.

---

## Requirements

- Linux / Unix‑like operating system
- GCC / Clang (with `libstdc++` for demangling)
- Standard C library + POSIX

---

## Installation

Clone the repository:

```bash
git clone https://github.com/HanSoBored/SL-Dumper.git
cd SL-Dumper
```
Compile the program with:

```bash
make
```

Move the compiled binary to a global location:
```bash
sudo make install
```

---

## Usage

1. Navigate to a directory containing `.so` files and run:
   ```bash
   sl-dumper
   ```
2. You will see a list of available `.so` files:
   ```
   Select Library to dump:
   1 libexample.so (C++)
   2 libother.so (Rust)

   ➔ Enter number (0 to exit):
   ```
3. Enter the number of the library you want to dump.
4. The tool parses the ELF, demangles symbols, and writes the output to:
   ```
   <library_name_without_lib>@dump/<library_name_without_lib>.cpp
   ```
   Example: for `libexample.so` the output is `example@dump/example.cpp`.

---

## Example Output

Input library: `libgame.so`

Generated `game@dump/game.cpp`:

```cpp
class Player {
      update; // 0x1a30
      render; // 0x1b80
      getHealth; // 0x1c20
};
class Weapon {
      fire; // 0x2a10
      reload; // 0x2b40
};
```

Each method is listed with its **virtual memory offset** (hex) as found in the dynamic symbol table.

---

## Project Structure

```
.
├── sl-dumper          # Compiled executable
├── libexample.so     # Shared library to dump
├── libother.so       # Another library
└── example@dump/   # Output folder
    └── example.cpp # Dumped class definitions
```

---

## License

This project is open source and available under the **MIT License**.

---

> **Tip:** Use the generated `.cpp` file as a quick reference for reverse engineering, documentation, or understanding library interfaces.