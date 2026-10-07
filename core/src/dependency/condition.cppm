export module lito.core:dependency.condition;

import rstd;
import :condition;

using namespace rstd::prelude;

export namespace lito::dependency
{

struct DependencyCondition {
    String                      source;
    lito::condition::Expression expression;

    auto clone() const -> DependencyCondition {
        return DependencyCondition {
            .source     = source.clone(),
            .expression = expression.clone(),
        };
    }
};

using ExternalDependencyCondition = DependencyCondition;

} // namespace lito::dependency
