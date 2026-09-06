export module lito.core:dependency.consumption;

import rstd;

using namespace rstd::prelude;

export namespace lito::dependency
{

enum class DependencyUsageFacet
{
    Compile,
    Link,
    Runtime,
};

class DependencyUsage {
    unsigned char bits_ {};

    explicit constexpr DependencyUsage(unsigned char bits) noexcept: bits_(bits) {}

    static constexpr auto compile_bit = static_cast<unsigned char>(1U << 0U);
    static constexpr auto link_bit    = static_cast<unsigned char>(1U << 1U);
    static constexpr auto runtime_bit = static_cast<unsigned char>(1U << 2U);

public:
    static constexpr auto compile_only() noexcept -> DependencyUsage {
        return DependencyUsage(compile_bit);
    }

    static constexpr auto link_only() noexcept -> DependencyUsage {
        return DependencyUsage(link_bit);
    }

    static constexpr auto runtime_only() noexcept -> DependencyUsage {
        return DependencyUsage(runtime_bit);
    }

    static constexpr auto compile_and_link() noexcept -> DependencyUsage {
        return DependencyUsage(static_cast<unsigned char>(compile_bit | link_bit));
    }

    static auto from_facets(bool compile, bool link, bool runtime) noexcept
        -> Option<DependencyUsage> {
        if (runtime) {
            if (compile || link) return None<DependencyUsage>();
            return Some(runtime_only());
        }
        if (compile && link) return Some(compile_and_link());
        if (compile) return Some(compile_only());
        if (link) return Some(link_only());
        return None<DependencyUsage>();
    }

    constexpr auto contains(DependencyUsageFacet facet) const noexcept -> bool {
        auto bit = compile_bit;
        if (facet == DependencyUsageFacet::Link) bit = link_bit;
        if (facet == DependencyUsageFacet::Runtime) bit = runtime_bit;
        return (bits_ & bit) != 0U;
    }

    constexpr auto uses_compile() const noexcept -> bool {
        return contains(DependencyUsageFacet::Compile);
    }

    constexpr auto uses_link() const noexcept -> bool {
        return contains(DependencyUsageFacet::Link);
    }

    constexpr auto uses_runtime() const noexcept -> bool {
        return contains(DependencyUsageFacet::Runtime);
    }

    constexpr auto len() const noexcept -> usize {
        auto result = usize {};
        if (uses_compile()) ++result;
        if (uses_link()) ++result;
        if (uses_runtime()) ++result;
        return result;
    }

    constexpr auto operator==(const DependencyUsage&) const noexcept -> bool = default;
};

struct DependencyConsumption {
    DependencyUsage usage { DependencyUsage::compile_and_link() };
    bool            is_public {};

    constexpr auto operator==(const DependencyConsumption&) const noexcept -> bool = default;
};

} // namespace lito::dependency
