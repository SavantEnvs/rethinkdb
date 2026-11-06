// Runs ReQL wire queries through the real query parser, query cache and term evaluator.
#include "mayhem/harness/reql_driver.hpp"

#include <stdlib.h>
#include <string.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include "errors.hpp"

#include "arch/runtime/starter.hpp"
#include "clustering/administration/auth/user_context.hpp"
#include "client_protocol/json.hpp"
#include "concurrency/cond_var.hpp"
#include "containers/scoped.hpp"
#include "rapidjson/stringbuffer.h"
#include "rdb_protocol/pseudo_time.hpp"
#include "rdb_protocol/query_cache.hpp"
#include "rdb_protocol/query_params.hpp"
#include "rdb_protocol/rdb_backtrace.hpp"
#include "rdb_protocol/ql2proto.hpp"
#include "rdb_protocol/response.hpp"
#include "unittest/rdb_env.hpp"
#include "utils.hpp"

namespace {

const size_t kTokenBytes = sizeof(int64_t);
const int64_t kToken = 1;
const size_t kMaxQueries = 16;

startup_shutdown_t *g_startup = nullptr;
unittest::test_rdb_env_t *g_env = nullptr;

// Mirrors rdb_query_server_t::run_query(), which needs a whole server to construct.
void run_query(ql::query_params_t *query_params,
               ql::response_t *response_out,
               signal_t *interruptor) {
    try {
        switch (query_params->type) {
        case Query::START: {
            scoped_ptr_t<ql::query_cache_t::ref_t> query_ref =
                query_params->query_cache->create(query_params, ql::pseudo::time_now(),
                                                  interruptor);
            query_ref->fill_response(response_out);
        } break;
        case Query::CONTINUE: {
            scoped_ptr_t<ql::query_cache_t::ref_t> query_ref =
                query_params->query_cache->get(query_params, interruptor);
            query_ref->fill_response(response_out);
        } break;
        case Query::STOP: {
            query_params->query_cache->stop_query(query_params, interruptor);
            response_out->set_type(Response::SUCCESS_SEQUENCE);
        } break;
        case Query::NOREPLY_WAIT: {
            query_params->query_cache->noreply_wait(*query_params, interruptor);
            response_out->set_type(Response::WAIT_COMPLETE);
        } break;
        default:
            response_out->fill_error(Response::CLIENT_ERROR, Response::INTERNAL,
                                     "Unsupported query type.",
                                     ql::backtrace_registry_t::EMPTY_BACKTRACE);
        }
    } catch (const ql::bt_exc_t &ex) {
        response_out->fill_error(ex.response_type, ex.error_type, ex.message,
                                 ex.bt_datum);
    } catch (const interrupted_exc_t &) {
        response_out->fill_error(Response::RUNTIME_ERROR, Response::OP_INDETERMINATE,
                                 "Interrupted.",
                                 ql::backtrace_registry_t::EMPTY_BACKTRACE);
    } catch (const std::exception &e) {
        response_out->fill_error(Response::RUNTIME_ERROR, Response::INTERNAL,
                                 strprintf("Unexpected exception: %s\n", e.what()),
                                 ql::backtrace_registry_t::EMPTY_BACKTRACE);
    }
}

void run_all(const uint8_t *data, size_t size, std::vector<std::string> *responses,
             bool *first_valid) {
    g_env->add_database("test");
    scoped_ptr_t<unittest::test_rdb_env_t::instance_t> instance = g_env->make_env();

    ql::query_cache_t cache(
        instance->get_rdb_context(),
        ip_and_port_t(ip_address_t("127.0.0.1"), port_t(0)),
        ql::return_empty_normal_batches_t::NO,
        auth::user_context_t(
            auth::permissions_t(tribool::True, tribool::True, tribool::True, tribool::True)));
    cond_t interruptor;

    size_t pos = 0;
    for (size_t n = 0; n < kMaxQueries && pos <= size; ++n) {
        size_t end = pos;
        while (end < size && data[end] != '\0') {
            ++end;
        }
        const size_t len = end - pos;

        scoped_array_t<char> buffer(kTokenBytes + len + 1);
        memset(buffer.data(), 0, kTokenBytes);
        memcpy(buffer.data() + kTokenBytes, data + pos, len);
        buffer[kTokenBytes + len] = '\0';

        ql::response_t response;
        scoped_ptr_t<ql::query_params_t> query = json_protocol_t::parse_query_from_buffer(
            std::move(buffer), kTokenBytes, &cache, kToken, &response);
        if (n == 0) {
            *first_valid = query.has();
        }
        if (query.has()) {
            run_query(query.get(), &response, &interruptor);
        }

        rapidjson::StringBuffer out;
        json_protocol_t::write_response_to_buffer(&response, &out);
        if (responses != nullptr) {
            responses->emplace_back(out.GetString(), out.GetSize());
        }
        pos = end + 1;
    }
}

struct job_t {
    const uint8_t *data;
    size_t size;
    std::vector<std::string> *responses;
    bool first_valid;
};

std::mutex g_mutex;
std::condition_variable g_cv;
job_t *g_pending = nullptr;
bool g_started = false;
bool g_done = false;
bool g_quit = false;
std::thread *g_pool_thread = nullptr;

// RethinkDB's thread pool installs its own SIGINT/SIGTERM handlers and replaces them with SIG_IGN
// when it shuts down, so one pool is started for the life of the process on a helper thread and
// each input is handed to it. Between inputs the pool's only thread blocks here; nothing else is
// scheduled on it.
void pool_main() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_started = true;
    }
    g_cv.notify_all();
    for (;;) {
        job_t *job;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait(lock, [] { return g_pending != nullptr || g_quit; });
            if (g_quit) {
                return;
            }
            job = g_pending;
        }
        run_all(job->data, job->size, job->responses, &job->first_valid);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_pending = nullptr;
            g_done = true;
        }
        g_cv.notify_all();
    }
}

// The pool must be shut down before static destructors run, or RethinkDB's globals trip a
// "non-empty intrusive list destroyed" guarantee on a clean exit.
void stop_pool() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_quit = true;
    }
    g_cv.notify_all();
    g_pool_thread->join();
}

}  // namespace

void reql_driver_init() {
    g_startup = new startup_shutdown_t;
    g_env = new unittest::test_rdb_env_t;
    g_pool_thread = new std::thread([] { ::run_in_thread_pool(pool_main, 1); });
    {
        std::unique_lock<std::mutex> lock(g_mutex);
        g_cv.wait(lock, [] { return g_started; });
    }
    atexit(stop_pool);
}

bool reql_driver_run(const uint8_t *data, size_t size, std::vector<std::string> *responses) {
    job_t job = {data, size, responses, false};
    std::unique_lock<std::mutex> lock(g_mutex);
    g_done = false;
    g_pending = &job;
    g_cv.notify_all();
    g_cv.wait(lock, [] { return g_done; });
    return job.first_valid;
}
