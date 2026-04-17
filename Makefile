# cpp-dumper Makefile

CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lstdc++

TARGET = cpp-dumper
SRC = cpp-dumper.c

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

.PHONY: all build clean install uninstall test help

all: build

build: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	@if [ -d $(BINDIR) ] && [ -z "$$(ls -A $(BINDIR))" ]; then rmdir $(BINDIR); fi

test:
	@echo "Running tests..."
	@bash tests/test_build.sh && echo "Build test: PASSED" || (echo "Build test: FAILED" && exit 1)
	@echo "All tests passed."

help:
	@echo "Available targets:"
	@echo "  build     - Compile cpp-dumper (default)"
	@echo "  clean     - Remove compiled binary"
	@echo "  install   - Install to $(BINDIR) (requires sudo)"
	@echo "  uninstall - Remove from $(BINDIR)"
	@echo "  test      - Run test suite"
	@echo "  help      - Show this help message"