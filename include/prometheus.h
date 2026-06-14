#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <sstream>

namespace haihunhou {

class Counter {
public:
    explicit Counter(const std::string& name, const std::string& help = "")
        : name_(name), help_(help), value_(0) {}

    void inc(double v = 1.0) { value_.fetch_add(v); }
    double value() const { return value_.load(); }
    const std::string& name() const { return name_; }
    const std::string& help() const { return help_; }

private:
    std::string name_;
    std::string help_;
    std::atomic<double> value_;
};

class Gauge {
public:
    explicit Gauge(const std::string& name, const std::string& help = "")
        : name_(name), help_(help), value_(0) {}

    void set(double v) { value_.store(v); }
    void inc(double v = 1.0) { value_.fetch_add(v); }
    void dec(double v = 1.0) { value_.fetch_sub(v); }
    double value() const { return value_.load(); }
    const std::string& name() const { return name_; }
    const std::string& help() const { return help_; }

private:
    std::string name_;
    std::string help_;
    std::atomic<double> value_;
};

class Histogram {
public:
    Histogram(const std::string& name, const std::string& help,
              const std::vector<double>& buckets)
        : name_(name), help_(help), buckets_(buckets), bucket_counts_(buckets.size() + 1, 0),
          sum_(0), count_(0) {}

    void observe(double v) {
        std::lock_guard<std::mutex> lock(mutex_);
        sum_ += v;
        count_++;
        for (size_t i = 0; i < buckets_.size(); ++i) {
            if (v <= buckets_[i]) {
                bucket_counts_[i]++;
            }
        }
        bucket_counts_[buckets_.size()]++; // +Inf bucket
    }

    std::string name() const { return name_; }
    std::string help() const { return help_; }
    std::vector<double> buckets() const { return buckets_; }
    std::vector<double> bucketCounts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bucket_counts_;
    }
    double sum() const { std::lock_guard<std::mutex> lock(mutex_); return sum_; }
    uint64_t count() const { std::lock_guard<std::mutex> lock(mutex_); return count_; }

private:
    std::string name_;
    std::string help_;
    std::vector<double> buckets_;
    mutable std::mutex mutex_;
    std::vector<double> bucket_counts_;
    double sum_;
    uint64_t count_;
};

class PrometheusRegistry {
public:
    static PrometheusRegistry& instance();

    void registerCounter(Counter* counter);
    void registerGauge(Gauge* gauge);
    void registerHistogram(Histogram* histogram);

    std::string collect() const;

private:
    PrometheusRegistry() = default;
    ~PrometheusRegistry() = default;

    static std::string escape(const std::string& s);

    mutable std::mutex mutex_;
    std::vector<Counter*> counters_;
    std::vector<Gauge*> gauges_;
    std::vector<Histogram*> histograms_;
};

#define METRIC_COUNTER(name, help) \
    static haihunhou::Counter _metric_counter_##name(#name, help); \
    struct _reg_##name { \
        _reg_##name() { haihunhou::PrometheusRegistry::instance().registerCounter(&_metric_counter_##name); } \
    }; \
    static _reg_##name _reg_inst_##name;

#define METRIC_GAUGE(name, help) \
    static haihunhou::Gauge _metric_gauge_##name(#name, help); \
    struct _reg_g_##name { \
        _reg_g_##name() { haihunhou::PrometheusRegistry::instance().registerGauge(&_metric_gauge_##name); } \
    }; \
    static _reg_g_##name _reg_g_inst_##name;

}
