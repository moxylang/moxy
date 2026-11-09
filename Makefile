# Builds the single `moxy` executable. The compiler is written in Moxy (src/*.mxy) and
# bootstraps from bootstrap/moxy.c, the committed C output of an earlier build:
#
#   stage0   bootstrap/moxy.c compiled with $(HOSTCC)
#   stage1   src/ compiled by stage0, then $(HOSTCC)
#   moxy     src/ compiled by stage1, then $(CC)    -> build/moxy
#
# `make verify` checks that build/moxy reproduces its own C; `make seed` refreshes the
# bootstrap. goose (libs/goose) is linked in as a library.

CC      ?= cc
AR      ?= ar
HOSTCC  ?= cc
HOSTAR  ?= ar
GOOSE   ?= libs/goose
PREFIX  ?= /usr/local

CFLAGS      ?= -O2
HOST_CFLAGS ?= -O2
WARN      = -Wall -Wextra -std=c11
INC       = -I$(GOOSE)/include -I$(GOOSE)/src
YAML_DEFS = -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5 -DYAML_VERSION_STRING='"0.2.5"'

BUILD = build
BIN   = $(BUILD)/moxy
SEED  = bootstrap/moxy.c
S0    = $(BUILD)/stage0/moxy
S1    = $(BUILD)/stage1/moxy

EMBED    = src/embed/data.mxy
EMBED_IN = VERSION $(wildcard runtime/*.h) $(wildcard std/*.mxy) scripts/genembed.sh
SRC      = $(filter-out $(EMBED), $(shell find src -name '*.mxy')) $(EMBED)

GOOSE_SRC = $(filter-out $(GOOSE)/src/main.c, $(wildcard $(GOOSE)/src/*.c)) \
            $(wildcard $(GOOSE)/src/cmd/*.c) $(wildcard $(GOOSE)/src/cc/*.c)
YAML_SRC  = $(wildcard $(GOOSE)/libs/libyaml/src/*.c)

# goose for the target ($(CC)) and for the bootstrap stages ($(HOSTCC))
GOOSE_LIB   = $(BUILD)/libgoose.a
GOOSE_OBJS  = $(GOOSE_SRC:$(GOOSE)/%.c=$(BUILD)/obj/goose/%.o) $(YAML_SRC:$(GOOSE)/%.c=$(BUILD)/obj/goose/%.o)
HGOOSE_LIB  = $(BUILD)/host/libgoose.a
HGOOSE_OBJS = $(GOOSE_SRC:$(GOOSE)/%.c=$(BUILD)/host/obj/%.o) $(YAML_SRC:$(GOOSE)/%.c=$(BUILD)/host/obj/%.o)

.PHONY: all clean install uninstall test check-goose sanitize verify seed

all: check-goose $(BIN)

check-goose:
	@test -f $(GOOSE)/include/goose.h || { \
	  echo "goose sources not found in $(GOOSE)"; \
	  echo "run: git submodule update --init libs/goose"; exit 1; }

$(EMBED): $(EMBED_IN)
	sh scripts/genembed.sh

$(S0): $(SEED) $(HGOOSE_LIB)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -std=c11 -w $(INC) $(SEED) $(HGOOSE_LIB) -lm -o $@

$(S1).c: $(S0) $(SRC)
	@mkdir -p $(dir $@)
	$(S0) emit src/main.mxy > $@.tmp && mv $@.tmp $@

$(S1): $(S1).c $(HGOOSE_LIB)
	$(HOSTCC) $(HOST_CFLAGS) $(WARN) $(INC) $< $(HGOOSE_LIB) -lm -o $@

$(BIN).c: $(S1) $(SRC)
	$(S1) emit src/main.mxy > $@.tmp && mv $@.tmp $@

$(BIN): $(BIN).c $(GOOSE_LIB)
	$(CC) $(CFLAGS) $(WARN) $(INC) $< $(GOOSE_LIB) -lm -o $@ $(LDFLAGS)

$(GOOSE_LIB): $(GOOSE_OBJS)
	$(AR) rcs $@ $^

$(HGOOSE_LIB): $(HGOOSE_OBJS)
	$(HOSTAR) rcs $@ $^

$(BUILD)/obj/goose/libs/%.o: $(GOOSE)/libs/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w $(YAML_DEFS) -I$(GOOSE)/libs/libyaml/include -I$(GOOSE)/libs/libyaml/src -c $< -o $@

$(BUILD)/obj/goose/%.o: $(GOOSE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=c11 -w -I$(GOOSE)/src -I$(GOOSE)/libs/libyaml/include -c $< -o $@

$(BUILD)/host/obj/libs/%.o: $(GOOSE)/libs/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -w $(YAML_DEFS) -I$(GOOSE)/libs/libyaml/include -I$(GOOSE)/libs/libyaml/src -c $< -o $@

$(BUILD)/host/obj/%.o: $(GOOSE)/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -std=c11 -w -I$(GOOSE)/src -I$(GOOSE)/libs/libyaml/include -c $< -o $@

# build/moxy must generate exactly the C it was built from
verify: all
	./$(BIN) emit src/main.mxy > $(BUILD)/verify.c
	@cmp $(BIN).c $(BUILD)/verify.c || { echo "build/moxy does not reproduce itself"; exit 1; }
	@echo "  fixed point ok"

seed: verify
	cp $(BIN).c $(SEED)

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
