// Compiles upstream's src/rdb_protocol/terms/string.cc unmodified, except that RE2's match type no
// longer has StringPiece::as_string() (RE2 now uses absl::string_view, which converts explicitly).
// Every header string.cc includes is pulled in first so the macro only touches string.cc's own code.
#include "rdb_protocol/terms/terms.hpp"

#include <re2/re2.h>

#include <algorithm>

#include "parsing/utf8.hpp"
#include "rdb_protocol/datum_string.hpp"
#include "rdb_protocol/error.hpp"
#include "rdb_protocol/minidriver.hpp"
#include "rdb_protocol/op.hpp"

#define as_string() operator std::string()
#include "rdb_protocol/terms/string.cc"
