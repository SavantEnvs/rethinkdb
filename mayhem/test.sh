#!/usr/bin/env bash
#
# mayhem/test.sh — runs the known-answer oracle that mayhem/build.sh built with RethinkDB's normal
# flags: ReQL wire queries go through the real parser, query cache and term evaluator, and each
# JSON response is compared with the exact expected value (arithmetic, strings/regex, objects,
# branching, streaming START/CONTINUE/STOP, error responses).
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

RUNNER=build/oracle/reql_eval
if [ ! -x "$RUNNER" ]; then
  echo "missing $RUNNER: mayhem/build.sh did not build the oracle" >&2
  emit_ctrf "reql-oracle" 0 1 0
  exit 1
fi

out=$(timeout 300 "$RUNNER" 2>&1); rc=$?
printf '%s\n' "$out"
passed=$(printf '%s\n' "$out" | grep -c '^PASS ')
failed=$(printf '%s\n' "$out" | grep -c '^FAIL ')
# A crash or a missing RESULT line counts as a failure even if no FAIL line was printed.
if [ "$rc" -ne 0 ] && [ "$failed" -eq 0 ]; then failed=1; fi
if ! printf '%s\n' "$out" | grep -q '^RESULT '; then failed=$(( failed + 1 )); fi
if [ "$passed" -eq 0 ]; then failed=$(( failed + 1 )); fi
emit_ctrf "reql-oracle" "$passed" "$failed" 0
