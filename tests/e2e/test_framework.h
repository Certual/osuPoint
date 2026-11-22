#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <stdexcept>

enum class TestTier {
    Tier1_Feature,
    Tier2_Boundary,
    Tier3_CrossFeature,
    Tier4_RealWorld
};

inline const char* TierToString(TestTier tier) noexcept {
    switch (tier) {
        case TestTier::Tier1_Feature: return "Tier 1: Feature Coverage";
        case TestTier::Tier2_Boundary: return "Tier 2: Boundary & Corner Cases";
        case TestTier::Tier3_CrossFeature: return "Tier 3: Cross-Feature Combinations";
        case TestTier::Tier4_RealWorld: return "Tier 4: Real-World Workloads";
        default: return "Unknown";
    }
}

inline const char* TierToShortString(TestTier tier) noexcept {
    switch (tier) {
        case TestTier::Tier1_Feature: return "T1";
        case TestTier::Tier2_Boundary: return "T2";
        case TestTier::Tier3_CrossFeature: return "T3";
        case TestTier::Tier4_RealWorld: return "T4";
        default: return "??";
    }
}

class TestFailureException : public std::runtime_error {
public:
    TestFailureException(const std::string& file, int line, const std::string& msg)
        : std::runtime_error(file + ":" + std::to_string(line) + ": " + msg),
          m_file(file), m_line(line), m_detail(msg) {}

    const std::string& File() const noexcept { return m_file; }
    int Line() const noexcept { return m_line; }
    const std::string& Detail() const noexcept { return m_detail; }

private:
    std::string m_file;
    int m_line;
    std::string m_detail;
};

struct TestCase {
    std::string name;
    TestTier tier;
    std::function<void()> func;
};

class TestRegistry {
public:
    static TestRegistry& Instance() {
        static TestRegistry reg;
        return reg;
    }

    void Register(const std::string& name, TestTier tier, std::function<void()> func) {
        m_tests.push_back({name, tier, std::move(func)});
    }

    const std::vector<TestCase>& GetTests() const noexcept {
        return m_tests;
    }

private:
    std::vector<TestCase> m_tests;
};

struct TestRegistrar {
    TestRegistrar(const std::string& name, TestTier tier, std::function<void()> func) {
        TestRegistry::Instance().Register(name, tier, std::move(func));
    }
};

#define E2E_TEST(name, tier) \
    static void test_##name(); \
    static TestRegistrar reg_##name(#name, tier, test_##name); \
    static void test_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw TestFailureException(__FILE__, __LINE__, "Expected true: " #cond); \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            throw TestFailureException(__FILE__, __LINE__, "Expected false: " #cond); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (val_a != val_b) { \
            std::ostringstream oss; \
            oss << "Assertion failed (" #a " == " #b "): " << val_a << " != " << val_b; \
            throw TestFailureException(__FILE__, __LINE__, oss.str()); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (val_a == val_b) { \
            std::ostringstream oss; \
            oss << "Assertion failed (" #a " != " #b "): both equal " << val_a; \
            throw TestFailureException(__FILE__, __LINE__, oss.str()); \
        } \
    } while (0)

#define ASSERT_NEAR(a, b, eps) \
    do { \
        double val_a = static_cast<double>(a); \
        double val_b = static_cast<double>(b); \
        double diff = std::abs(val_a - val_b); \
        if (diff > static_cast<double>(eps)) { \
            std::ostringstream oss; \
            oss << "Assertion failed (| " #a " - " #b " | <= " #eps "): |" << val_a << " - " << val_b << "| = " << diff << " > " << eps; \
            throw TestFailureException(__FILE__, __LINE__, oss.str()); \
        } \
    } while (0)

#define ASSERT_LE(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (val_a > val_b) { \
            std::ostringstream oss; \
            oss << "Assertion failed (" #a " <= " #b "): " << val_a << " > " << val_b; \
            throw TestFailureException(__FILE__, __LINE__, oss.str()); \
        } \
    } while (0)

#define ASSERT_GE(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (val_a < val_b) { \
            std::ostringstream oss; \
            oss << "Assertion failed (" #a " >= " #b "): " << val_a << " < " << val_b; \
            throw TestFailureException(__FILE__, __LINE__, oss.str()); \
        } \
    } while (0)
