export module tenon.frontend.preprocessor:macro;

import rstd;
import tenon.frontend.lexical;

using namespace rstd::prelude;
using namespace rstd::literals;

using namespace tenon::frontend::lexical;

export namespace tenon::frontend::preprocessor {

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

using SharedMacroDefinition = rstd::rc::Rc<const MacroDefinition>;

class MacroTable {
public:
  static auto make() -> MacroTable { return MacroTable{}; }

  auto get(ref<str> name) const -> Option<SharedMacroDefinition> {
    auto found = values_.get(name);
    return found.is_some() ? Some((**found).clone())
                           : Option<SharedMacroDefinition>{};
  }
  auto contains(ref<str> name) const -> bool {
    return values_.contains_key(name);
  }

  auto define(MacroDefinition definition) -> Option<SharedMacroDefinition> {
    auto name = definition.name.clone();
    auto shared = rstd::rc::make_rc<MacroDefinition>(rstd::move(definition))
                      .to_const();
    return values_.insert(rstd::move(name), rstd::move(shared));
  }

  auto define_shared(SharedMacroDefinition definition)
      -> Option<SharedMacroDefinition> {
    auto name = definition.get()->name.clone();
    return values_.insert(rstd::move(name), rstd::move(definition));
  }

  auto undefine(ref<str> name) -> Option<SharedMacroDefinition> {
    return values_.remove(name);
  }

private:
  rstd::collections::HashMap<String, SharedMacroDefinition> values_;
};

auto clone_tokens(const Vec<Token> &input) -> Vec<Token> {
  auto result = Vec<Token>::with_capacity(input.len());
  for (const auto &token : input)
    result.push(token.clone());
  return result;
}

auto parse_macro_definition(const Vec<Token> &line)
    -> lexical::Result<MacroDefinition> {
  if (line.is_empty() || line[usize{}].kind != TokenKind::Identifier) {
    auto location = line.is_empty() ? SourceLocation{} : line[usize{}].expansion;
    return Err(lexical::Error::at(
        String::make("#define requires an identifier"_str), location));
  }
  auto macro = MacroDefinition{
      .name = line[usize{}].text.clone(),
      .location = line[usize{}].expansion,
  };
  auto index = usize(1);
  if (index < line.len() && line[index].text.as_str() == "("_str &&
      !line[index].leading_space) {
    auto parameters = Vec<String>::make();
    ++index;
    if (index < line.len() && line[index].text.as_str() == ")"_str) {
      ++index;
    } else {
      while (index < line.len()) {
        if (line[index].text.as_str() == "..."_str) {
          macro.variadic = true;
          macro.variadic_name = String::make("__VA_ARGS__"_str);
          ++index;
          break;
        }
        if (line[index].kind != TokenKind::Identifier) {
          return Err(lexical::Error::at(
              String::make("invalid macro parameter"_str),
              line[index].expansion));
        }
        auto name = line[index].text.clone();
        ++index;
        if (index < line.len() && line[index].text.as_str() == "..."_str) {
          macro.variadic = true;
          macro.variadic_name = rstd::move(name);
          ++index;
          break;
        }
        parameters.push(rstd::move(name));
        if (index < line.len() && line[index].text.as_str() == ","_str) {
          ++index;
          continue;
        }
        break;
      }
      if (index >= line.len() || line[index].text.as_str() != ")"_str) {
        auto location = index < line.len() ? line[index].expansion
                                           : macro.location;
        return Err(lexical::Error::at(
            String::make("unterminated macro parameter list"_str), location));
      }
      ++index;
    }
    macro.parameters = Some(rstd::move(parameters));
  }
  auto replacement = Vec<Token>::with_capacity(line.len() - index);
  for (; index < line.len(); ++index)
    replacement.push(line[index].clone());
  macro.replacement = rstd::move(replacement);
  return Ok(rstd::move(macro));
}

auto share_macro_definition(MacroDefinition definition)
    -> SharedMacroDefinition {
  return rstd::rc::make_rc<MacroDefinition>(rstd::move(definition)).to_const();
}

} // namespace tenon::frontend::preprocessor
