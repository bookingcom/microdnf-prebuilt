// Module spec parsing through libdnf5::module::Nsvcap.
//
// dnf5's NSVCAP regexes were POSIX extended, which libc++ aborts on, and are ECMAScript here (the
// libdnf5__module__nsvcap.cpp patch). The expectations are upstream's: the first block repeats the cases of
// test/libdnf5/module/test_module.cpp (test_nsvcap) for the 18 forms, the others follow the grammar in
// libdnf5/module/nsvcap.cpp: names, streams, contexts, arches and profiles are [glob letters digits +._-]+, versions
// are [glob digits -]+, the glob characters are ] [ * ? !, and the forms must match the whole string.

#include <gtest/gtest.h>
#include <libdnf5/module/nsvcap.hpp>

#include <ostream>
#include <string>
#include <vector>

namespace {

using libdnf5::module::Nsvcap;
using Form = Nsvcap::Form;

struct Fields {
    std::string name, stream, version, context, arch, profile;
    bool operator==(const Fields &) const = default;
};

std::ostream & operator<<(std::ostream & os, const Fields & f) {
    return os << "{" << f.name << "|" << f.stream << "|" << f.version << "|" << f.context << "|" << f.arch << "|"
              << f.profile << "}";
}

Fields fields_of(const Nsvcap & n) {
    return {n.get_name(), n.get_stream(), n.get_version(), n.get_context(), n.get_arch(), n.get_profile()};
}

struct FormCase {
    std::string spec;
    Form form;
    Fields want;  // ignored unless ok
    bool ok = true;
};

std::ostream & operator<<(std::ostream & os, const FormCase & c) {
    return os << c.spec << " as form " << static_cast<int>(c.form);
}

class NsvcapForm : public testing::TestWithParam<FormCase> {};

TEST_P(NsvcapForm, Parse) {
    const auto & c = GetParam();
    Nsvcap n;
    ASSERT_EQ(c.ok, n.parse(c.spec, c.form)) << c;
    if (c.ok) {
        EXPECT_EQ(c.want, fields_of(n)) << c;
    } else {
        // A failed parse leaves the attributes unchanged.
        EXPECT_EQ(Fields{}, fields_of(n)) << c;
    }
}

const std::string M = "meson", S = "master", V = "20180816151613", C = "06d0a27d";

INSTANTIATE_TEST_SUITE_P(
    Upstream,
    NsvcapForm,
    testing::Values(
        FormCase{M + ":" + S + ":" + V + ":" + C + ":x86_64/default", Form::NSVCAP, {M, S, V, C, "x86_64", "default"}},
        FormCase{M + ":" + S + ":" + V + ":" + C + ":x86_64", Form::NSVCA, {M, S, V, C, "x86_64", ""}},
        FormCase{M + ":" + S + ":" + V + "::x86_64/default", Form::NSVAP, {M, S, V, "", "x86_64", "default"}},
        FormCase{M + ":" + S + ":" + V + "::x86_64", Form::NSVA, {M, S, V, "", "x86_64", ""}},
        FormCase{M + ":" + S + "::x86_64/default", Form::NSAP, {M, S, "", "", "x86_64", "default"}},
        FormCase{M + ":" + S + "::x86_64", Form::NSA, {M, S, "", "", "x86_64", ""}},
        FormCase{M + ":" + S + ":" + V + ":" + C + "/default", Form::NSVCP, {M, S, V, C, "", "default"}},
        FormCase{M + ":" + S + ":" + V + ":" + C, Form::NSVC, {M, S, V, C, "", ""}},
        FormCase{M + ":" + S + ":" + V + "/default", Form::NSVP, {M, S, V, "", "", "default"}},
        FormCase{M + ":" + S + ":" + V, Form::NSV, {M, S, V, "", "", ""}},
        FormCase{M + ":" + S + "/default", Form::NSP, {M, S, "", "", "", "default"}},
        FormCase{M + ":" + S, Form::NS, {M, S, "", "", "", ""}},
        FormCase{M + "::x86_64/default", Form::NAP, {M, "", "", "", "x86_64", "default"}},
        FormCase{M + "::x86_64", Form::NA, {M, "", "", "", "x86_64", ""}},
        FormCase{M + "/default", Form::NP, {M, "", "", "", "", "default"}},
        FormCase{M, Form::N, {M, "", "", "", "", ""}},
        FormCase{M + ":" + S + ":" + C + "/default", Form::NSCP, {M, S, "", C, "", "default"}},
        FormCase{M + ":" + S + ":" + C, Form::NSC, {M, S, "", C, "", ""}},
        // An additional ':' before the architecture is allowed even in the full form.
        FormCase{M + ":" + S + ":" + V + ":" + C + "::x86_64/default", Form::NSVCAP, {M, S, V, C, "x86_64", "default"}},
        FormCase{M + ":" + S + ":" + V + ":" + C + "::x86_64", Form::NSVCA, {M, S, V, C, "x86_64", ""}},
        // A trailing '/' is allowed.
        FormCase{M + "/", Form::N, {M, "", "", "", "", ""}},
        // The form must match exactly, and its fields are not empty.
        FormCase{M + ":" + S, Form::N, {}, false},
        FormCase{M + ":" + S + ":" + V + ":" + C + "/default", Form::NSVC, {}, false},
        FormCase{M + ":" + S + ":" + V + ":" + C + ":", Form::NSVC, {}, false},
        FormCase{M + ":" + S + "::" + C, Form::NSVC, {}, false},
        // 2 KB is the longest string parsed.
        FormCase{std::string(2048, 'a'), Form::N, {std::string(2048, 'a'), "", "", "", "", ""}},
        FormCase{std::string(2049, 'a'), Form::N, {}, false}));

INSTANTIATE_TEST_SUITE_P(
    Characters,
    NsvcapForm,
    testing::Values(
        // Dashes, dots, underscores and pluses in names, streams and profiles.
        FormCase{"nodejs-devel.v8_x+y", Form::N, {"nodejs-devel.v8_x+y", "", "", "", "", ""}},
        FormCase{"python3.11:3.11-beta/common-dev", Form::NSP, {"python3.11", "3.11-beta", "", "", "", "common-dev"}},
        // The version is digits, dashes and glob characters only.
        FormCase{"mod:s:-1", Form::NSV, {"mod", "s", "-1", "", "", ""}},
        FormCase{"mod:s:2018abc", Form::NSV, {}, false},
        // Globs in every field, including the brackets and '!' that the patch had to escape.
        FormCase{"pyth*", Form::N, {"pyth*", "", "", "", "", ""}},
        FormCase{"php:7.[89]", Form::NS, {"php", "7.[89]", "", "", "", ""}},
        FormCase{"mod?:s*:2018*", Form::NSV, {"mod?", "s*", "2018*", "", "", ""}},
        FormCase{"!mod:[a-z]x", Form::NS, {"!mod", "[a-z]x", "", "", "", ""}},
        FormCase{"*:*:*:*:*/*", Form::NSVCAP, {"*", "*", "*", "*", "*", "*"}},
        // Invalid input.
        FormCase{"", Form::N, {}, false},
        FormCase{"", Form::NS, {}, false},
        FormCase{"meson:", Form::NS, {}, false},
        FormCase{":master", Form::NS, {}, false},
        FormCase{"/default", Form::NP, {}, false},
        FormCase{"me son", Form::N, {}, false},
        FormCase{"meson@1", Form::N, {}, false},
        FormCase{"mes\xc3\xb3n", Form::N, {}, false},
        FormCase{"meson/def/ault", Form::NP, {}, false},
        FormCase{"meson:master/def ault", Form::NSP, {}, false},
        FormCase{"meson:master:1:ctx:x86_64:extra", Form::NSVCA, {}, false}));

struct SpecCase {
    std::string spec;
    std::vector<Fields> want;  // in the order of get_default_module_spec_forms()
};

class NsvcapSpec : public testing::TestWithParam<SpecCase> {};

TEST_P(NsvcapSpec, ParseAllForms) {
    const auto & c = GetParam();
    std::vector<Fields> got;
    for (const auto & n : Nsvcap::parse(c.spec)) {
        got.push_back(fields_of(n));
    }
    EXPECT_EQ(c.want, got) << c.spec;
}

INSTANTIATE_TEST_SUITE_P(
    DefaultForms,
    NsvcapSpec,
    testing::Values(
        SpecCase{"meson", {{"meson", "", "", "", "", ""}}},
        SpecCase{"meson:master", {{"meson", "master", "", "", "", ""}}},
        SpecCase{"meson/default", {{"meson", "", "", "", "", "default"}}},
        // Digits are a valid version and a valid context: NSC comes before NSV in the default order. NSCP is
        // upstream's "N:S:C" followed by an optional '/' and a profile (nsvcap.cpp, the last but one regex), so it
        // also matches a context cut anywhere, here before the last digit. That is upstream's grammar, not the patch's:
        // POSIX extended gives the same split.
        SpecCase{"meson:master:20180816151613", {{"meson", "master", "", "20180816151613", "", ""}, {"meson", "master", "", "2018081615161", "", "3"}, {"meson", "master", "20180816151613", "", "", ""}}},
        SpecCase{"meson:master:20180816151613:06d0a27d:x86_64/default", {{"meson", "master", "20180816151613", "06d0a27d", "x86_64", "default"}}},
        SpecCase{"a:b:c:d", {}},
        SpecCase{"", {}},
        SpecCase{"meson:master:1:ctx:x86_64:extra", {}}));

}  // namespace
