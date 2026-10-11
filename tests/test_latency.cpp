#include <gtest/gtest.h>

#include <sstream>

#include "rtdoctor/latency.hpp"
#include "rtdoctor/latency_report.hpp"

using namespace rtdoctor;

TEST(LatencyStats, SummarizesAndFindsPercentiles) {
    LatencyStats stats;
    for (int i = 0; i < 990; ++i) stats.add(5.4);   // Typical wake-ups.
    for (int i = 0; i < 9; ++i) stats.add(120.0);   // Occasional.
    stats.add(2500.0);                              // One spike.
    EXPECT_EQ(stats.samples(), 1000);
    EXPECT_DOUBLE_EQ(stats.min(), 5.4);
    EXPECT_DOUBLE_EQ(stats.max(), 2500.0);
    EXPECT_NEAR(stats.average(), (990 * 5.4 + 9 * 120 + 2500) / 1000, 1e-9);
    EXPECT_DOUBLE_EQ(stats.percentile(0.5), 6);      // Upper bound of the 5-6 us bin.
    EXPECT_DOUBLE_EQ(stats.percentile(0.99), 6);
    EXPECT_DOUBLE_EQ(stats.percentile(0.999), 121);
    EXPECT_DOUBLE_EQ(stats.percentile(1.0), 2500);
    EXPECT_EQ(stats.count_over(100), 10);
    EXPECT_EQ(stats.count_over(1000), 1);
}

TEST(LatencyStats, CoarseBinsAndOverflowStayBounded) {
    LatencyStats stats;
    stats.add(15000);   // 10-100 ms range.
    stats.add(250000);  // Beyond the histogram.
    EXPECT_DOUBLE_EQ(stats.percentile(0.5), 15100);
    EXPECT_DOUBLE_EQ(stats.percentile(0.9), 250000);  // Overflow bin: the real maximum.
}

TEST(Latency, VerdictTiers) {
    EXPECT_EQ(latency_verdict(30).rfind("excellent", 0), 0u);
    EXPECT_EQ(latency_verdict(100).rfind("good", 0), 0u);
    EXPECT_EQ(latency_verdict(800).rfind("fair", 0), 0u);
    EXPECT_EQ(latency_verdict(5000).rfind("poor", 0), 0u);
}

TEST(Latency, MeasuresOnThisMachine) {
    LatencyOptions options;
    options.cpus = {0};
    options.duration = 0.3;
    options.interval_us = 1000;
    const auto result = measure_latency(options);
    ASSERT_EQ(result.cpus.size(), 1u);
    EXPECT_EQ(result.cpus[0].cpu, 0);
    EXPECT_GT(result.cpus[0].stats.samples(), 150);  // ~300 expected; leave room for slow machines.
    EXPECT_GE(result.cpus[0].stats.min(), 0);
}

TEST(Latency, CausesAreSummedOverThreadsAndWorstWaitsKept) {
    ThreadReport a, b;
    a.delays = {{"task hog (pid 7)", 0.004, 3}, {"irq 11 arch_timer", 0.0001, 9}};
    b.delays = {{"task hog (pid 7)", 0.002, 1}};
    a.worst = {{0, 0.002, 0.0015, 0, {}}, {0, 0.001, 0.0002, 0, {}}};
    b.worst = {{0, 0.003, 0.0025, 1, {}}};
    const auto causes = latency_causes({a, b}, 0, 2);
    ASSERT_EQ(causes.delays.size(), 2u);
    EXPECT_EQ(causes.delays[0].who, "task hog (pid 7)");
    EXPECT_DOUBLE_EQ(causes.delays[0].seconds, 0.006);
    EXPECT_EQ(causes.delays[0].count, 4);
    ASSERT_EQ(causes.worst.size(), 2u);
    EXPECT_EQ(causes.worst[0].cpu, 1);
    EXPECT_DOUBLE_EQ(causes.worst[1].waited, 0.0015);

    LatencyResult result;
    result.options.interval_us = 1000;
    result.cpus.push_back({0, {}});
    result.cpus.push_back({1, {}});
    result.cpus[0].stats.add(10);
    result.cpus[1].stats.add(90);
    std::ostringstream out;
    print_latency_text(out, result, causes);
    EXPECT_NE(out.str().find("Worst: 90 us on CPU 1 -> good"), std::string::npos);
    EXPECT_NE(out.str().find("task hog (pid 7)"), std::string::npos);
}

TEST(Latency, OwnTraceReadingIsLabelled) {
    ThreadReport t;
    t.delays = {{"task rtdoctor (pid 42)", 0.002, 2}, {"task rtdoctor (pid 43)", 0.001, 1}};
    const auto causes = latency_causes({t}, 42);
    ASSERT_EQ(causes.delays.size(), 2u);
    EXPECT_EQ(causes.delays[0].who, "rtdoctor itself (reading the trace)");
    EXPECT_EQ(causes.delays[1].who, "task rtdoctor (pid 43)");  // Another rtdoctor: a real cause.
}
