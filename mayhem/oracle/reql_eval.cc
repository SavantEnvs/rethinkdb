// Functional oracle: known ReQL query -> known JSON response, through the same driver the fuzz
// harness uses, built with RethinkDB's normal (unsanitized) flags. Prints "PASS name"/"FAIL name".
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "mayhem/harness/reql_driver.hpp"

namespace {

struct kat_t {
    const char *name;
    std::string input;                      // NUL-separated queries
    std::vector<std::string> expected;      // exact response per query
};

std::string nul_join(std::initializer_list<const char *> qs) {
    std::string out;
    bool first = true;
    for (const char *q : qs) {
        if (!first) out.push_back('\0');
        out += q;
        first = false;
    }
    return out;
}

}  // namespace

int main(int argc, char **) {
    const bool print_only = argc > 1;
    const std::vector<kat_t> cats = {
        {"add_ints", nul_join({"[1,[24,[1,2]]]"}), {"{\"t\":1,\"r\":[3]}"}},
        {"mul_ints", nul_join({"[1,[26,[6,7]]]"}), {"{\"t\":1,\"r\":[42]}"}},
        {"sub_float", nul_join({"[1,[25,[1.5,0.25]]]"}), {"{\"t\":1,\"r\":[1.25]}"}},
        {"div_zero", nul_join({"[1,[27,[1,0]]]"}), {"{\"t\":18,\"e\":3000000,\"r\":[\"Cannot divide by zero.\"],\"b\":[]}"}},
        {"make_array", nul_join({"[1,[2,[1,2,3]]]"}), {"{\"t\":1,\"r\":[[1,2,3]]}"}},
        {"count_array", nul_join({"[1,[43,[[2,[5,6,7]]]]]"}), {"{\"t\":1,\"r\":[3]}"}},
        {"upcase", nul_join({"[1,[141,[\"rethink\"]]]"}), {"{\"t\":1,\"r\":[\"RETHINK\"]}"}},
        {"split", nul_join({"[1,[149,[\"a,b,c\",\",\"]]]"}), {"{\"t\":1,\"r\":[[\"a\",\"b\",\"c\"]]}"}},
        {"match", nul_join({"[1,[97,[\"foobar\",\"o+\"]]]"}), {"{\"t\":1,\"r\":[{\"end\":3,\"groups\":[],\"start\":1,\"str\":\"oo\"}]}"}},
        {"branch_true", nul_join({"[1,[65,[[17,[1,1]],\"yes\",\"no\"]]]"}), {"{\"t\":1,\"r\":[\"yes\"]}"}},
        {"branch_false", nul_join({"[1,[65,[[19,[2,1]],\"yes\",\"no\"]]]"}), {"{\"t\":1,\"r\":[\"no\"]}"}},
        {"get_field", nul_join({"[1,[31,[{\"a\":5},\"a\"]]]"}), {"{\"t\":1,\"r\":[5]}"}},
        {"type_of", nul_join({"[1,[52,[[2,[1]]]]]"}), {"{\"t\":1,\"r\":[\"ARRAY\"]}"}},
        {"range_small", nul_join({"[1,[173,[4]]]"}), {"{\"t\":2,\"r\":[0,1,2,3],\"n\":[]}"}},
        {"map_range", nul_join({"[1,[38,[[173,[4]],[69,[[2,[1]],[26,[[10,[1]],10]]]]]]]"}), {"{\"t\":2,\"r\":[0,10,20,30],\"n\":[]}"}},
        {"json", nul_join({"[1,[98,[\"{\\\"x\\\":[1,2]}\"]]]"}), {"{\"t\":1,\"r\":[{\"x\":[1,2]}]}"}},
        {"bad_json", nul_join({"[1,[24,[1,"}), {"{\"t\":16,\"r\":[\"Client is buggy (failed to deserialize query).\"],\"b\":[]}"}},
        {"bad_type", nul_join({"[9,[24,[1,2]]]"}), {"{\"t\":16,\"r\":[\"Server could not parse query: Unrecognized QueryType: 9.\"],\"b\":[]}"}},
        {"range_stream_continue", nul_join({"[1,[173,[10]],{\"max_batch_rows\":3}]", "[2]", "[2]", "[2]", "[2]"}), {"{\"t\":3,\"r\":[0],\"n\":[]}", "{\"t\":3,\"r\":[1,2,3],\"n\":[]}", "{\"t\":3,\"r\":[4,5,6],\"n\":[]}", "{\"t\":2,\"r\":[7,8,9],\"n\":[]}", "{\"t\":16,\"r\":[\"Token 1 not in stream cache.\"],\"b\":[]}"}},
        {"range_stream_stop", nul_join({"[1,[173,[10]],{\"max_batch_rows\":3}]", "[3]", "[2]"}), {"{\"t\":3,\"r\":[0],\"n\":[]}", "{\"t\":2,\"r\":[],\"n\":[]}", "{\"t\":16,\"r\":[\"Token 1 not in stream cache.\"],\"b\":[]}"}},
        {"two_queries_one_cache", nul_join({"[1,[24,[40,2]]]", "[1,[141,[\"ok\"]]]"}), {"{\"t\":1,\"r\":[42]}", "{\"t\":1,\"r\":[\"OK\"]}"}},
    };
    reql_driver_init();
    int passed = 0, failed = 0;
    for (const kat_t &k : cats) {
        std::vector<std::string> got;
        const bool ok = reql_driver_run(reinterpret_cast<const uint8_t *>(k.input.data()),
                                        k.input.size(), &got);
        if (print_only) {
            printf("--- %s (valid=%d)\n", k.name, ok);
            for (const std::string &r : got) printf("%s\n", r.c_str());
            continue;
        }
        const bool want_valid = strncmp(k.name, "bad_", 4) != 0;
        if (ok == want_valid && got == k.expected) {
            ++passed;
            printf("PASS %s\n", k.name);
        } else {
            ++failed;
            printf("FAIL %s: valid=%d (want %d), %zu responses\n", k.name, ok, want_valid, got.size());
            for (const std::string &r : got) printf("  got: %s\n", r.c_str());
            for (const std::string &r : k.expected) printf("  want: %s\n", r.c_str());
        }
    }
    if (!print_only) printf("RESULT passed=%d failed=%d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
