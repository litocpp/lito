export module lito.core:dependency.condition;

import rstd;
import :condition;

using namespace rstd::prelude;

export namespace lito::dependency
{

struct ExternalDependencyCondition {
    String                      source;
    lito::condition::Expression expression;

    auto clone() const -> ExternalDependencyCondition {
        return ExternalDependencyCondition {
            .source     = source.clone(),
            .expression = expression.clone(),
        };
    }
};

} // namespace lito::dependency
