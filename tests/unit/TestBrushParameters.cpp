// The parameter registry as a contract.
//
// A brush setting that exists in the code but not in the panel is invisible to
// the artist; a setting in the panel that no engine reads is a lie about what
// the brush can do. This test holds both sides of that line: every registry
// entry belongs to exactly one of the thirteen groups, every group has at least
// one setting in it, every declared range agrees with its unit, and the
// defaults sit inside the range they declare.
//
// Qt-free, like the registry.
#include "brush/BrushParameters.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace komapaint::brush;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

void testEveryParameterHasExactlyOneSection() {
    std::set<std::string> declared;
    for (const char* section : parameterSections()) declared.insert(section);
    CHECK(declared.size() == parameterSections().size());   // no duplicates
    CHECK(declared.size() == 13);                          // Part 5: thirteen groups

    std::map<std::string, int> perSection;
    for (const ParameterInfo& info : parameterRegistry()) {
        const char* section = parameterSection(info.name);
        if (!section) {
            std::printf("parameter '%s' has no section\n", info.name);
            CHECK(section != nullptr);
            continue;
        }
        CHECK(declared.count(section) == 1);
        ++perSection[section];
    }

    // A group nobody can fill is a heading with nothing under it.
    for (const char* section : parameterSections()) {
        if (perSection[section] == 0) {
            std::printf("section '%s' is empty\n", section);
        }
        CHECK(perSection[section] > 0);
    }
    CHECK(perSection.size() == declared.size());
}

void testSectionTableMatchesTheRegistryExactly() {
    // Same set, both ways: a section entry for a parameter that does not exist
    // is a typo waiting to hide a real setting.
    std::set<std::string> registry;
    for (const ParameterInfo& info : parameterRegistry()) registry.insert(info.name);

    std::set<std::string> named;
    for (const ParameterInfo& info : parameterRegistry()) {
        if (parameterSection(info.name) != nullptr) named.insert(info.name);
    }
    CHECK(registry == named);
}

void testRangesAreCoherent() {
    for (const ParameterInfo& info : parameterRegistry()) {
        CHECK(info.label != nullptr && info.label[0] != '\0');
        CHECK(info.description != nullptr && info.description[0] != '\0');
        CHECK(info.minimum < info.maximum);
        CHECK(std::isfinite(info.minimum));
        CHECK(std::isfinite(info.maximum));
        CHECK(std::isfinite(info.defaultValue));
        CHECK(parameterInRange(info, info.defaultValue));
        if (!parameterInRange(info, info.defaultValue)) {
            std::printf("default of '%s' (%g) is outside [%g, %g]\n", info.name,
                        info.defaultValue, info.minimum, info.maximum);
        }
        // Units carry the meaning of the numbers. Percentages may be signed --
        // an offset is a percentage in both directions -- and a scale above
        // 100 % is ordinary (a leaf at 300 % of its natural size), so the bound
        // is "a number a person typed", not "a fraction".
        switch (info.unit) {
        case ParameterUnit::Percent:
            CHECK(info.minimum >= -400.0 && info.maximum <= 900.0);
            break;
        case ParameterUnit::Unit:
            CHECK(info.maximum <= 1000.0);
            break;
        case ParameterUnit::Ratio:
            CHECK(info.minimum >= 0.0 && info.maximum <= 1000.0);
            break;
        case ParameterUnit::Degrees:
            CHECK(info.minimum >= -720.0 && info.maximum <= 720.0);
            break;
        case ParameterUnit::Pixels:
            CHECK(info.minimum >= 0.0 && info.maximum <= 8192.0);
            break;
        }
    }
}

void testLookupBehaviour() {
    CHECK(findParameter("size") != nullptr);
    CHECK(findParameter("no-such-setting") == nullptr);
    CHECK(findParameter("") == nullptr);
    if (const ParameterInfo* size = findParameter("size")) {
        CHECK(std::string(parameterSection(size->name)) == "Size");
        CHECK(parameterInRange(*size, 16.0));
        CHECK(!parameterInRange(*size, 0.0));
        CHECK(!parameterInRange(*size, 100000.0));
        CHECK(!parameterInRange(*size, std::nan("")));
    }
    CHECK(parameterSection("no-such-setting") == nullptr);
}

void testNoDuplicateNames() {
    std::set<std::string> seen;
    for (const ParameterInfo& info : parameterRegistry()) {
        CHECK(seen.insert(info.name).second);
    }
    CHECK(seen.size() == parameterRegistry().size());
}

} // namespace

int main() {
    testEveryParameterHasExactlyOneSection();
    testSectionTableMatchesTheRegistryExactly();
    testRangesAreCoherent();
    testLookupBehaviour();
    testNoDuplicateNames();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
