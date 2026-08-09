export module lito.frontend.static_name;

import rstd;

using namespace rstd::prelude;

export namespace lito::frontend
{

template<rstd::str_::fixed_string Name>
struct StaticName {
    static_assert(rstd::str_::VALID_UTF8_LITERAL<Name>);

    static constexpr auto name =
        ref<str>::from_raw_parts_unchecked(rstd::str_::BYTE_LITERAL_STORAGE<Name>.data(),
                                           usize(rstd::str_::BYTE_LITERAL_STORAGE<Name>.size()));
};

template<rstd::str_::fixed_string Name, typename HandlerType>
struct StaticNameWithHandler : StaticName<Name> {
    using Handler = HandlerType;
};

template<typename... Types>
struct StaticNameSet {
    template<typename Function>
    static constexpr auto for_each(Function&& function) -> void {
        (function(rstd::mtp::type_c<Types>), ...);
    }

    template<typename Function>
    static constexpr auto visit(ref<str> name, Function&& function) -> bool {
        return ((name == Types::name && (function(rstd::mtp::type_c<Types>), true)) || ...);
    }

    static constexpr auto contains(ref<str> name) -> bool { return ((name == Types::name) || ...); }

    template<typename... Additional>
    using With = StaticNameSet<Types..., Additional...>;
};

} // namespace lito::frontend
