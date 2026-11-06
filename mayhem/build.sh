#!/usr/bin/env bash
#
# mayhem/build.sh — builds RethinkDB's server code with mayhem/rdb.mk (upstream's own ./configure +
# make fetches QuickJS from the network and probes the host), then:
#   /mayhem/fuzz_reql_query             libFuzzer harness, sanitized
#   /mayhem/fuzz_reql_query-standalone  same harness, run-once driver instead of libFuzzer
#   build/oracle/reql_eval              known-answer oracle, normal flags, only RUN by test.sh
# Runs offline: every dependency is an apt -dev package installed by mayhem/Dockerfile.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# `=` (not `:=`): an explicit EMPTY SANITIZER_FLAGS builds with no sanitizers.
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
mkdir -p build/fuzz build/normal build/oracle

# Checks relaxed for this target (see the PR):
#  - pointer-overflow: rapidjson's stack allocator does `ptr + 0` arithmetic on a null base on
#    every parse, so it fires on every input.
#  - ASan stack instrumentation (-asan-stack=0; heap, global and all UBSan checks stay on):
#    RethinkDB runs everything on its own coroutine stacks, which ASan cannot track (it logs
#    "ignoring requested __asan_handle_no_return"). Frame redzones/scope poison left behind by
#    unwound frames then produce stack-buffer-overflow / stack-use-after-scope reports on valid code.
SAN_RELAX="-fno-sanitize=pointer-overflow -mllvm -asan-stack=0"
COMPILE_FUZZ="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS $SAN_RELAX -fsanitize=fuzzer-no-link"
LIBS="-lre2 -lprotobuf -lssl -lcrypto -lz -lpthread -lm -ldl -lrt"
HARNESS_FLAGS="-std=gnu++14 -pthread -w -fno-strict-aliasing -DNDEBUG -DENABLE_TLS -DRAPIDJSON_HAS_STDSTRING -DRAPIDJSON_PARSE_DEFAULT_FLAGS=kParseFullPrecisionFlag -D__STDC_LIMIT_MACROS -D__STDC_FORMAT_MACROS -D_FILE_OFFSET_BITS=64 -Isrc -I."

# 1) The server code, sanitized.
make -f mayhem/rdb.mk CXX="$CXX" OUT=build/fuzz EXTRA_CXXFLAGS="$COMPILE_FUZZ" -j"$MAYHEM_JOBS"

# 2) Harness: fuzzer and standalone.
# shellcheck disable=SC2086
$CXX $HARNESS_FLAGS $COMPILE_FUZZ -c mayhem/harness/fuzz_reql_query.cc -o build/fuzz/fuzz_reql_query.o
# shellcheck disable=SC2086
$CXX $HARNESS_FLAGS $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o build/fuzz/lsan_off.o
# shellcheck disable=SC2086
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o build/fuzz/standalone_main.o
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE build/fuzz/fuzz_reql_query.o \
    build/fuzz/lsan_off.o build/fuzz/librethinkdb.a $LIBS -o /mayhem/fuzz_reql_query
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS build/fuzz/fuzz_reql_query.o build/fuzz/standalone_main.o \
    build/fuzz/lsan_off.o build/fuzz/librethinkdb.a $LIBS -o /mayhem/fuzz_reql_query-standalone

# 3) Test oracle with RethinkDB's normal flags (no sanitizers, no fuzzer instrumentation).
make -f mayhem/rdb.mk CXX="$CXX" OUT=build/normal EXTRA_CXXFLAGS="-O1 $COVERAGE_FLAGS" -j"$MAYHEM_JOBS"
# shellcheck disable=SC2086
$CXX $HARNESS_FLAGS -O1 $COVERAGE_FLAGS mayhem/oracle/reql_eval.cc build/normal/librethinkdb.a \
    $COVERAGE_FLAGS $LIBS -o build/oracle/reql_eval
