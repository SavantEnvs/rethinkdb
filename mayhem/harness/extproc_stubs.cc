// The fuzz and oracle binaries run without extproc job workers: the JavaScript worker needs
// QuickJS (not vendored; upstream downloads it at build time) and the HTTP worker would make real
// network requests. Both job classes fail the way a worker that could not be started does
// (extproc_worker_exc_t), which is what r.js()/r.http() already handle. All code in front of the
// jobs (term parsing, argument checks, http_opts serialization) is real.
#include "extproc/http_job.hpp"
#include "extproc/js_job.hpp"

namespace {
extproc_pool_t *no_worker(extproc_pool_t *) {
    throw extproc_worker_exc_t("extproc workers are disabled in the Mayhem build");
}
}  // namespace

js_job_t::js_job_t(extproc_pool_t *pool, signal_t *interruptor,
                   const ql::configured_limits_t &_limits) :
    extproc_job(no_worker(pool), &worker_fn, interruptor), limits(_limits) { }

js_result_t js_job_t::eval(const std::string &) { throw extproc_worker_exc_t("disabled"); }
js_result_t js_job_t::call(js_id_t, const std::vector<ql::datum_t> &) {
    throw extproc_worker_exc_t("disabled");
}
void js_job_t::release(js_id_t) { }
void js_job_t::exit() { }
void js_job_t::worker_error() { }
bool js_job_t::worker_fn(read_stream_t *, write_stream_t *) { return false; }

http_job_t::http_job_t(extproc_pool_t *pool, signal_t *interruptor) :
    extproc_job(no_worker(pool), &worker_fn, interruptor) { }

void http_job_t::http(const http_opts_t &, http_result_t *) {
    throw extproc_worker_exc_t("disabled");
}
void http_job_t::worker_error() { }
bool http_job_t::worker_fn(read_stream_t *, write_stream_t *) { return false; }
