export module tenon.preprocessor:macro;

import rstd;
import :token;

using namespace rstd::prelude;

export namespace tenon::preprocessor {

struct MacroDefinition {
  String name;
  Option<Vec<String>> parameters;
  bool variadic{false};
  String variadic_name;
  Vec<Token> replacement;
  SourceLocation location;

  auto clone() const -> MacroDefinition {
    auto copied_parameters = Option<Vec<String>>{};
    if (parameters.is_some()) {
      auto values = Vec<String>::with_capacity(parameters->len());
      for (const auto &parameter : *parameters)
        values.push(parameter.clone());
      copied_parameters = Some(rstd::move(values));
    }
    auto copied_replacement = Vec<Token>::with_capacity(replacement.len());
    for (const auto &token : replacement)
      copied_replacement.push(token.clone());
    return MacroDefinition{
        .name = name.clone(),
        .parameters = rstd::move(copied_parameters),
        .variadic = variadic,
        .variadic_name = variadic_name.clone(),
        .replacement = rstd::move(copied_replacement),
        .location = location,
    };
  }
};

class MacroTable {
public:
  static auto make() -> MacroTable { return MacroTable{}; }

  auto get(ref<str> name) const -> Option<ref<MacroDefinition>> {
    return values_.get(name);
  }
  auto contains(ref<str> name) const -> bool {
    return values_.contains_key(name);
  }

  auto define(MacroDefinition definition) -> Option<MacroDefinition> {
    auto name = definition.name.clone();
    return values_.insert(rstd::move(name), rstd::move(definition));
  }

  auto undefine(ref<str> name) -> Option<MacroDefinition> {
    return values_.remove(name);
  }

private:
  rstd::collections::BTreeMap<String, MacroDefinition> values_;
};

auto clone_tokens(const Vec<Token> &input) -> Vec<Token> {
  auto result = Vec<Token>::with_capacity(input.len());
  for (const auto &token : input)
    result.push(token.clone());
  return result;
}

} // namespace tenon::preprocessor
