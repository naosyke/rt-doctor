#include <gtest/gtest.h>

#include "fake_system.hpp"
#include "rtdoctor/system_view.hpp"

using rtdoctor::SystemView;

TEST(SystemView, ReadsFilesRelativeToRoot) {
    FakeSystem fake;
    fake.file("proc/cmdline", "quiet isolcpus=2-3\n").file("proc/irq/5/smp_affinity_list", "0-3\n");
    const SystemView system(fake.root());

    EXPECT_EQ(system.read_line("proc/cmdline"), "quiet isolcpus=2-3");
    EXPECT_FALSE(system.read("proc/missing").has_value());
    EXPECT_TRUE(system.exists("proc/irq/5"));
    EXPECT_EQ(system.list("proc/irq"), std::vector<std::string>{"5"});
    EXPECT_TRUE(system.list("nope").empty());
}

TEST(SystemView, CpuLists) {
    EXPECT_EQ(rtdoctor::parse_cpu_list("0-2,5"), (std::vector<int>{0, 1, 2, 5}));
    EXPECT_EQ(rtdoctor::parse_cpu_list("domain,managed_irq,2-3"), (std::vector<int>{2, 3}));
    EXPECT_EQ(rtdoctor::format_cpu_list({5, 0, 1, 2}), "0-2,5");
}
