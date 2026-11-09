# Builds the single `moxy` executable. goose (libs/goose) is linked in as a library.

CC      ?= cc
AR      ?= ar
GOOSE   ?= libs/goose
PREFIX  ?= /usr/local
VERSION := $(shell cat VERSION)

CFLAGS  ?= -O2
WARN     = -Wall -Wextra -std=c11
DEFS     = -DMOXY_VERSION='"$(VERSION)"'
INC      = -Isrc -I$(GOOSE)/include -I$(GOOSE)/src -I$(GOOSE)/libs/libyaml/include
YAML_DEFS = -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5 -DYAML_VERSION_STRING='"0.2.5"'

BUILD    = build
OBJ      = $(BUILD)/obj
BIN      = $(BUILD)/moxy

SRC      = $(shell find src -name '*.c' -not -path 'src/embed/*') src/embed/embed.c
OBJS     = $(SRC:src/%.c=$(OBJ)/%.o)

GOOSE_SRC  = $(filter-out $(GOOSE)/src/main.c, $(wildcard $(GOOSE)/src/*.c)) \
             $(wildcard $(GOOSE)/src/cmd/*.c) $(wildcard $(GOOSE)/src/cc/*.c)
YAML_SRC   = $(wildcard $(GOOSE)/libs/libyaml/src/*.c)
GOOSE_OBJS = $(GOOSE_SRC:$(GOOSE)/%.c=$(OBJ)/goose/%.o) $(YAML_SRC:$(GOOSE)/%.c=$(OBJ)/goose/%.o)
GOOSE_LIB  = $(BUILD)/libgoose.a

EMBED_IN = $(wildcard runtime/*.h) $(wildcard std/*.mxy) scripts/genembed.sh

.PHONY: all clean install uninstall test check-goose sanitize

all: check-goose $(BIN)

check-goose:
	@test -f $(GOOSE)/include/goose.h || { \
	  echo "goose sources not found in $(GOOSE)"; \
	  echo "run: git submodule update --init --remote libs/goose"; exit 1; }

$(BIN): $(OBJS) $(GOOSE_LIB)
	$(CC) $(OBJS) $(GOOSE_LIB) -o $@ $(LDFLAGS)

$(GOOSE_LIB): $(GOOSE_OBJS)
	$(AR) rcs $@ $^

src/embed/embed.c: $(EMBED_IN)
	sh scripts/genembed.sh

$(OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) $(DEFS) $(INC) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d)

$(OBJ)/goose/libs/%.o: $(GOOSE)/libs/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w $(YAML_DEFS) -I$(GOOSE)/libs/libyaml/include -I$(GOOSE)/libs/libyaml/src -c $< -o $@

$(OBJ)/goose/%.o: $(GOOSE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=c11 -w -I$(GOOSE)/src -I$(GOOSE)/libs/libyaml/include -c $< -o $@

sanitize:
	$(MAKE) BUILD=build/asan CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
	  LDFLAGS="-fsanitize=address,undefined"

test: all
	./$(BIN) test tests
	@for f in examples/*.mxy; do \
	  grep -qE "(void|int) main" $$f || continue; \
	  ./$(BIN) run $$f > /dev/null || exit 1; \
	done; echo "  examples ok"

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/moxy

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/moxy

clean:
	rm -rf $(BUILD)
