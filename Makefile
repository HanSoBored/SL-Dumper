# SL-Dumper Makefile

CC = clang
CFLAGS = -std=c23 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wdouble-promotion \
         -Wnull-dereference -Wformat=2 -Wstrict-prototypes -Wold-style-definition \
         -Wmissing-prototypes -Wimplicit-fallthrough -Wvla -Werror=implicit-function-declaration \
         -Werror=int-conversion -fdiagnostics-format=msvc
LDFLAGS = -lstdc++

BUILDDIR = build
TARGET = $(BUILDDIR)/sl-dumper
SRC = src/sl-dumper.c

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

TEST_SRC = tests/test_sl_dumper.c
TEST_UNITY = tests/vendor/unity/unity.c
TEST_BIN = $(BUILDDIR)/test_runner

.PHONY: all clean install uninstall test help analyze sanitize-asan sanitize-ubsan cppcheck

all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(SRC) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -rf $(BUILDDIR)

install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	@if [ -d $(BINDIR) ] && [ -z "$$(ls -A $(BINDIR))" ]; then rmdir $(BINDIR); fi

test: $(TEST_BIN)
	@echo "Running tests..."
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(TEST_UNITY) $(TARGET) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $(TEST_BIN) $(TEST_SRC) $(TEST_UNITY) $(LDFLAGS)

analyze: $(SRC)
	clang --analyze -Xanalyzer -analyzer-output=text $(CFLAGS) $(SRC)

sanitize-asan: $(SRC) | $(BUILDDIR)
	$(CC) $(CFLAGS) -fsanitize=address -g -o $(BUILDDIR)/sl-dumper-asan $(SRC) $(LDFLAGS)

sanitize-ubsan: $(SRC) | $(BUILDDIR)
	$(CC) $(CFLAGS) -fsanitize=undefined -g -o $(BUILDDIR)/sl-dumper-ubsan $(SRC) $(LDFLAGS)

cppcheck: $(SRC)
	cppcheck --std=c23 --enable=all --suppress=missingIncludeSystem $(SRC)

help:
	@echo "Available targets:"
	@echo "  build          - Compile sl-dumper (default)"
	@echo "  clean          - Remove build directory"
	@echo "  install        - Install to $(BINDIR) (requires sudo)"
	@echo "  uninstall      - Remove from $(BINDIR)"
	@echo "  test           - Run test suite"
	@echo "  analyze        - Run Clang static analyzer"
	@echo "  cppcheck       - Run cppcheck static analysis"
	@echo "  sanitize-asan  - Build with AddressSanitizer"
	@echo "  sanitize-ubsan - Build with UBSan"
	@echo "  help           - Show this help message"
