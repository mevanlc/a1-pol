CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
PREFIX ?= /usr/local

EXEEXT :=
ifeq ($(OS),Windows_NT)
	EXEEXT := .exe
	LDLIBS += -lbcrypt
endif

BIN := a1-pol-mem
SRC := src/a1-pol-mem.c
BUILD_DIR := build
TARGET := $(BUILD_DIR)/$(BIN)$(EXEEXT)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $@

install: $(TARGET)
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 0755 "$(TARGET)" "$(DESTDIR)$(PREFIX)/bin/$(BIN)"

clean:
	rm -rf "$(BUILD_DIR)"
