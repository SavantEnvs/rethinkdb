// libFuzzer harness: ReQL wire queries (JSON) through RethinkDB's query parser, query cache and
// term evaluator, against the in-process mock environment used by the upstream unit tests.
#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "mayhem/harness/reql_driver.hpp"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    reql_driver_init();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return reql_driver_run(data, size, nullptr) ? 0 : -1;
}
