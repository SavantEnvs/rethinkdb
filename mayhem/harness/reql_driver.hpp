#ifndef MAYHEM_REQL_DRIVER_HPP_
#define MAYHEM_REQL_DRIVER_HPP_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

// Must run once, single-threaded, before the first reql_driver_run().
void reql_driver_init();

// `data` is a stream of NUL-separated JSON ReQL wire queries (`[type, term, optargs]`), run in order
// through one fresh query cache against a mock environment (database "test", no tables), exactly as
// the client-protocol server does. Every query uses token 1, so `[2]` (CONTINUE) and `[3]` (STOP)
// address the preceding `[1, ...]` (START) that left a partial stream open. Each query's JSON
// response is appended to `responses`. Returns false if the first query was not a valid ReQL query.
bool reql_driver_run(const uint8_t *data, size_t size, std::vector<std::string> *responses);

#endif
