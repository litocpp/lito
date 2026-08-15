export module lito.frontend.static_name;

import rstd;

using namespace rstd::prelude;

export namespace lito::frontend
{

inline constexpr uint64_t COMPARABLE_NAME_HASH_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t COMPARABLE_NAME_HASH_PRIME  = 1099511628211ull;

constexpr auto comparable_name_hash(ref<str> name) noexcept -> uint64_t {
    auto hash = COMPARABLE_NAME_HASH_OFFSET;
    for (auto value : name) {
        hash ^= value.to_primitive();
        hash *= COMPARABLE_NAME_HASH_PRIME;
    }
    return hash;
}

template<rstd::str_::fixed_string Name>
struct StaticName {
    static_assert(rstd::str_::VALID_UTF8_LITERAL<Name>);

    static constexpr auto name =
        ref<str>::from_raw_parts_unchecked(rstd::str_::BYTE_LITERAL_STORAGE<Name>.data(),
                                           usize(rstd::str_::BYTE_LITERAL_STORAGE<Name>.size()));
    static constexpr auto hash = comparable_name_hash(name);
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
        return visit(comparable_name_hash(name), name, function);
    }

    template<typename Function>
    static constexpr auto visit(uint64_t hash, ref<str> name, Function&& function) -> bool {
        return (((hash == Types::hash && name == Types::name) &&
                 (function(rstd::mtp::type_c<Types>), true)) ||
                ...);
    }

    static constexpr auto contains(ref<str> name) -> bool {
        return contains(comparable_name_hash(name), name);
    }

    static constexpr auto contains(uint64_t hash, ref<str> name) -> bool {
        return (((hash == Types::hash) && name == Types::name) || ...);
    }

    template<typename... Additional>
    using With = StaticNameSet<Types..., Additional...>;
};

} // namespace lito::frontend
