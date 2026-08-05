#if 0
/// Inactive declaration documentation.
export int inactive_documentation();
#endif

//! Fixture module overview.
export module fixture.doc.basic;

export import :child;

/// Public fixture API.
export namespace fixture::nested
{

/// \name Arithmetic
/// @{
/// Forward declaration documentation.
int add(int left, int right);
/**
 * Adds two values with `int` arithmetic.
 * \param left The left value.
 * @param right The right value.
 * \return The sum.
 */
int add(int left, int right) {
    return left + right;
}
/// @}

/// Constructs a value after a requires-expression.
export template<typename T, typename... Args>
    requires requires { T(Args {}...); }
T make(Args&&... args);

/// @cond undocumented
/// This exported declaration is hidden from generated documentation.
export int doxygen_hidden();
/// @endcond

/** A documented *record* with escaped <script> markup and [docs](https://example.com). */
struct Widget {
    /// The public value.
    int value;

private:
    /// This private member is not part of the published API.
    int secret;
};

/// A documented template record used to verify record scope parsing.
template<typename T>
struct Box {
    /// The contained value.
    T value;

    /// Returns whether the box contains a value.
    explicit operator bool() const { return true; }
};

/// A fixture execution mode.
enum class Mode
{
    Fast,
    Safe,
};

/// A type accepted by the fixture API.
template<typename T>
concept Value = true;

/// An alias for the fixture record.
using WidgetAlias = Widget;

/// The fixture answer.
inline constexpr int answer = 42;

} // namespace fixture::nested
