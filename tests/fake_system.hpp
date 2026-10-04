// A fake /proc + /sys tree in a temporary directory.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

class FakeSystem {
public:
    FakeSystem() {
        char pattern[] = "/tmp/rtdoctor-test-XXXXXX";
        root_ = mkdtemp(pattern);
    }
    ~FakeSystem() { std::filesystem::remove_all(root_); }

    FakeSystem& file(const std::string& path, const std::string& content) {
        const auto full = std::filesystem::path(root_) / path;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream(full) << content;
        return *this;
    }

    const std::string& root() const { return root_; }

private:
    std::string root_;
};
