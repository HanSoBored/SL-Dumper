# SL-Dumper Makefile

CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lstdc++

BUILDDIR = build
TARGET = $(BUILDDIR)/sl-dumper
SRC = sl-dumper.c

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

TEST_SRC = tests/test_sl_dumper.c
TEST_UNITY = tests/vendor/unity/unity.c
TEST_BIN = $(BUILDDIR)/test_runner

.PHONY: all clean install uninstall test help

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

help:
	@echo "Available targets:"
	@echo "  build     - Compile sl-dumper (default)"
	@echo "  clean     - Remove build directory"
	@echo "  install   - Install to $(BINDIR) (requires sudo)"
	@echo "  uninstall - Remove from $(BINDIR)"
	@echo "  test      - Run test suite"
	@echo "  help      - Show this help message"
