#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.cpp;
import lito.driver;

using namespace rstd::prelude;
using namespace rstd::literals;

TEST(ProcMacroBuild, AggregateSelectionIdentityIncludesProviders) {
    auto dependencies = Vec<lito::cpp::ProcMacroDependencySpec>::make();
    dependencies.push(lito::cpp::ProcMacroDependencySpec {
        .package = String::make("example-macros"_str),
    });
    auto same = Vec<lito::cpp::ProcMacroDependencySpec>::make();
    same.push(dependencies[usize {}].clone());
    EXPECT_EQ(lito::proc_macro_aggregate_identity(dependencies).as_str(),
              lito::proc_macro_aggregate_identity(same).as_str());

    same[usize {}].package = String::make("other-macros"_str);
    EXPECT_NE(lito::proc_macro_aggregate_identity(dependencies).as_str(),
              lito::proc_macro_aggregate_identity(same).as_str());
}
