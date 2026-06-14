#include "test_stub.h"
#include "opcua/session_pool.h"
#include "thread_pool.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <set>

using namespace haihunhou;
using namespace haihunhou::opcua;

static void test_session_pool_basic() {
    std::cout << "\n--- SessionPool: Basic lifecycle ---" << std::endl;

    SessionConfig cfg;
    cfg.host = "localhost";
    cfg.port = 4840;
    cfg.timeout_ms = 5000;
    cfg.max_retries = 3;
    cfg.keepalive_interval_ms = 10000;
    cfg.application_uri = "urn:test";

    SessionPool pool(cfg, 2, 10);
    pool.start();

    CHECK(pool.available() == 2);
    CHECK(pool.total() == 2);
    CHECK(pool.borrowed() == 0);
    std::cout << "  Start: available=" << pool.available()
              << " total=" << pool.total() << " borrowed=" << pool.borrowed()
              << " [PASS]" << std::endl;

    auto s1 = pool.acquire(1000);
    CHECK(s1 != nullptr);
    CHECK(s1->isConnected());
    CHECK(s1->isValid());
    CHECK(pool.borrowed() == 1);
    CHECK(pool.available() == 1);
    std::cout << "  Acquire s1: id=" << s1->sessionId()
              << " borrowed=" << pool.borrowed() << " [PASS]" << std::endl;

    auto s2 = pool.acquire(1000);
    CHECK(s2 != nullptr);
    CHECK(s2->sessionId() != s1->sessionId());
    CHECK(pool.borrowed() == 2);
    std::cout << "  Acquire s2: id=" << s2->sessionId()
              << " borrowed=" << pool.borrowed() << " [PASS]" << std::endl;

    pool.release(s1);
    CHECK(pool.borrowed() == 1);
    CHECK(pool.available() == 1);
    std::cout << "  Release s1: borrowed=" << pool.borrowed()
              << " available=" << pool.available() << " [PASS]" << std::endl;

    auto s3 = pool.acquire(1000);
    CHECK(s3 != nullptr);
    CHECK(s3->sessionId() == s1->sessionId());
    CHECK(pool.borrowed() == 2);
    std::cout << "  Re-acquire s1 (reuse): id=" << s3->sessionId() << " [PASS]" << std::endl;

    pool.release(s2);
    pool.release(s3);
    CHECK(pool.borrowed() == 0);
    CHECK(pool.total() == 2);
    std::cout << "  All released: borrowed=" << pool.borrowed() << " [PASS]" << std::endl;

    pool.stop();
    CHECK(pool.total() == 0);
    CHECK(pool.available() == 0);
    std::cout << "  Stopped: total=" << pool.total() << " [PASS]" << std::endl;
}

static void test_session_pool_concurrent() {
    std::cout << "\n--- SessionPool: Concurrent access ---" << std::endl;

    SessionConfig cfg;
    cfg.host = "localhost";
    cfg.port = 4840;
    cfg.timeout_ms = 5000;
    cfg.max_retries = 3;
    cfg.keepalive_interval_ms = 10000;
    cfg.application_uri = "urn:test";

    SessionPool pool(cfg, 3, 15);
    pool.start();

    const int N_THREADS = 8;
    const int N_OPS = 50;
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};
    std::set<uint32_t> seen_ids;
    std::mutex seen_mutex;

    auto worker = [&]() {
        for (int i = 0; i < N_OPS; ++i) {
            auto s = pool.acquire(2000);
            if (s) {
                success_count++;
                {
                    std::lock_guard<std::mutex> lock(seen_mutex);
                    seen_ids.insert(s->sessionId());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                pool.release(s);
            } else {
                fail_count++;
            }
        }
    };

    std::vector<std::thread> threads;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N_THREADS; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();
    auto t1 = std::chrono::steady_clock::now();
    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    std::cout << "  " << N_THREADS << " threads x " << N_OPS << " ops completed in "
              << dur_ms << "ms" << std::endl;
    std::cout << "  Success=" << success_count.load()
              << " Fail=" << fail_count.load()
              << " Unique sessions=" << seen_ids.size() << std::endl;

    CHECK(success_count.load() > N_THREADS * N_OPS * 0.9);
    CHECK(pool.borrowed() == 0);
    CHECK(seen_ids.size() > 0 && seen_ids.size() <= 15);
    std::cout << "  Concurrent ops [PASS]" << std::endl;

    pool.stop();
}

static void test_session_pool_dynamic_scaling() {
    std::cout << "\n--- SessionPool: Dynamic scaling ---" << std::endl;

    SessionConfig cfg;
    cfg.host = "localhost";
    cfg.port = 4840;
    cfg.timeout_ms = 5000;
    cfg.max_retries = 3;
    cfg.keepalive_interval_ms = 10000;
    cfg.application_uri = "urn:test";

    SessionPool pool(cfg, 1, 20);
    pool.start();

    CHECK(pool.total() == 1);
    std::cout << "  Initial total=" << pool.total() << " [PASS]" << std::endl;

    std::vector<OpcUaSessionPtr> borrowed;
    for (int i = 0; i < 5; ++i) {
        auto s = pool.acquire(1000);
        CHECK(s != nullptr);
        borrowed.push_back(s);
    }
    CHECK(pool.total() == 5);
    CHECK(pool.borrowed() == 5);
    std::cout << "  Borrowed 5, auto-scaled total=" << pool.total() << " [PASS]" << std::endl;

    for (auto& s : borrowed) pool.release(s);
    CHECK(pool.total() == 5);
    CHECK(pool.available() == 5);
    std::cout << "  Released, total=" << pool.total() << " available=" << pool.available() << " [PASS]" << std::endl;

    pool.setMinPoolSize(2);
    pool.setMaxPoolSize(8);
    std::cout << "  Resized min=2 max=8 [PASS]" << std::endl;

    pool.stop();
}

static void test_session_pool_timeout() {
    std::cout << "\n--- SessionPool: Acquire timeout ---" << std::endl;

    SessionConfig cfg;
    cfg.host = "localhost";
    cfg.port = 4840;
    cfg.timeout_ms = 5000;
    cfg.max_retries = 3;
    cfg.keepalive_interval_ms = 10000;
    cfg.application_uri = "urn:test";

    SessionPool pool(cfg, 1, 1);
    pool.start();

    auto s1 = pool.acquire(1000);
    CHECK(s1 != nullptr);

    auto t0 = std::chrono::steady_clock::now();
    auto s2 = pool.acquire(200);
    auto t1 = std::chrono::steady_clock::now();
    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    CHECK(s2 == nullptr);
    CHECK(dur_ms >= 150 && dur_ms <= 500);
    std::cout << "  Timeout after " << dur_ms << "ms, null returned [PASS]" << std::endl;

    pool.release(s1);
    pool.stop();
}

static void test_session_stats() {
    std::cout << "\n--- SessionPool: Session stats ---" << std::endl;

    SessionConfig cfg;
    cfg.host = "localhost";
    cfg.port = 4840;
    cfg.timeout_ms = 5000;
    cfg.max_retries = 3;
    cfg.keepalive_interval_ms = 1000000;
    cfg.application_uri = "urn:test";

    SessionPool pool(cfg, 1, 5);
    pool.start();

    auto s = pool.acquire(500);
    CHECK(s != nullptr);
    CHECK(s->useCount() == 0);
    CHECK(s->createdAt() > 0);
    auto first_last = s->lastUsed();

    std::vector<SpectralData> sd;
    bool ok = s->readSpectralData(1, sd);
    CHECK(ok);
    CHECK(s->useCount() == 1);
    CHECK(s->lastUsed() >= first_last);
    std::cout << "  useCount=" << s->useCount()
              << " spectral_points=" << sd.size() << " [PASS]" << std::endl;

    std::vector<MicrobialData> md;
    ok = s->readMicrobialData(1, md);
    CHECK(ok);
    CHECK(s->useCount() == 2);
    CHECK(!md.empty());
    std::cout << "  useCount=" << s->useCount()
              << " microbial_points=" << md.size() << " [PASS]" << std::endl;

    pool.release(s);
    pool.stop();
}

int main() {
    std::cout << "========== test_session_pool ==========" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    test_session_pool_basic();
    test_session_pool_concurrent();
    test_session_pool_dynamic_scaling();
    test_session_pool_timeout();
    test_session_stats();

    std::cout << "\n========== test_session_pool: ALL PASSED ==========" << std::endl;
    return 0;
}
