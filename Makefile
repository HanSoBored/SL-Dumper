# SL-Dumper Makefile

CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lstdc++

TARGET = sl-dumper
SRC = sl-dumper.c

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

test: $(TARGET)
	@echo "Running tests..."
	$(CC) -std=c99 -o tests/test tests/test.c
	./tests/test
	@echo "All tests passed."

help:
	@echo "Available targets:"
	@echo "  build     - Compile sl-dumper (default)"
	@echo "  clean     - Remove compiled binary"
	@echo "  install   - Install to $(BINDIR) (requires sudo)"
	@echo "  uninstall - Remove from $(BINDIR)"
	@echo "  test      - Run test suite"
	@echo "  help      - Show this help message"