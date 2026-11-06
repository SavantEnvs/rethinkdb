# Builds RethinkDB's server code (everything except main(), the JS/HTTP extproc jobs and the unit
# tests) into $(OUT)/librethinkdb.a with the compiler flags given in $(EXTRA_CXXFLAGS). Upstream's
# own build (./configure + make) downloads QuickJS and probes the host; this avoids both.
#   make -f mayhem/rdb.mk OUT=build/fuzz EXTRA_CXXFLAGS="-O1 -fsanitize=address" -j8
CXX ?= clang++
AR ?= ar
OUT ?= build/mayhem
EXTRA_CXXFLAGS ?=

SRC_DIR := src
PROTO_DIR := $(OUT)/proto
OBJ_DIR := $(OUT)/obj

SOURCES := $(shell find $(SRC_DIR) -name '*.cc' -not -name '.*' | LC_ALL=C sort)
EXCLUDED := $(SRC_DIR)/main.cc $(SRC_DIR)/unittest/main.cc $(SRC_DIR)/extproc/js_job.cc \
            $(SRC_DIR)/extproc/http_job.cc $(SRC_DIR)/rdb_protocol/terms/string.cc
# Replacements/additions that are not upstream files, compiled like the rest.
MAYHEM_LIB_SOURCES := mayhem/harness/extproc_stubs.cc mayhem/harness/string_term.cc \
                      mayhem/harness/reql_driver.cc
UNITTEST_KEPT := $(SRC_DIR)/unittest/rdb_env.cc
SERVER_SOURCES := $(filter-out $(EXCLUDED),$(filter-out $(SRC_DIR)/unittest/%,$(SOURCES))) $(UNITTEST_KEPT)
OBJS := $(patsubst $(SRC_DIR)/%.cc,$(OBJ_DIR)/%.o,$(SERVER_SOURCES)) $(OBJ_DIR)/rdb_protocol/ql2.pb.o \
        $(patsubst mayhem/%.cc,$(OBJ_DIR)/mayhem/%.o,$(MAYHEM_LIB_SOURCES))

CXXFLAGS_RDB := -std=gnu++14 -pthread -w -fno-strict-aliasing \
    -DNDEBUG -DENABLE_TLS -DRAPIDJSON_HAS_STDSTRING \
    -DRAPIDJSON_PARSE_DEFAULT_FLAGS=kParseFullPrecisionFlag \
    -D__STDC_LIMIT_MACROS -D__STDC_FORMAT_MACROS -D_FILE_OFFSET_BITS=64 \
    -DPRODUCT_NAME=\"rethinkdb\" -DBUILD_MACHINE=\"mayhem\" \
    -DRETHINKDB_VERSION=\"2.4.4\" -DRETHINKDB_CODE_VERSION=\"2.4.4\" \
    -I$(SRC_DIR) -I$(PROTO_DIR) -I.

.PHONY: all
all: $(OUT)/librethinkdb.a

$(PROTO_DIR)/rdb_protocol/ql2.pb.h: $(SRC_DIR)/rdb_protocol/ql2.proto
	mkdir -p $(PROTO_DIR)/rdb_protocol
	protoc --proto_path=$(SRC_DIR) --cpp_out=$(PROTO_DIR) $<

$(PROTO_DIR)/rdb_protocol/ql2.pb.cc: $(PROTO_DIR)/rdb_protocol/ql2.pb.h

$(OBJ_DIR)/rdb_protocol/ql2.pb.o: $(PROTO_DIR)/rdb_protocol/ql2.pb.cc
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_RDB) $(EXTRA_CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/mayhem/%.o: mayhem/%.cc | $(PROTO_DIR)/rdb_protocol/ql2.pb.h
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_RDB) $(EXTRA_CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cc | $(PROTO_DIR)/rdb_protocol/ql2.pb.h
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_RDB) $(EXTRA_CXXFLAGS) -c $< -o $@

$(OUT)/librethinkdb.a: $(OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS)
