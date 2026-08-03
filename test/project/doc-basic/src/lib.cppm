#if 0
/// Inactive declaration documentation.
export int inactive_documentation();
#endif

//! Fixture module overview.
export module fixture.doc.basic;

/// Public fixture API.
export namespace fixture::nested
{

/// \name Arithmetic
/// @{
/**
 * Adds two values with `int` arithmetic.
 * \param left The left value.
 * @param right The right value.
 * \return The sum.
 */
int add(int left, int right);
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

} // namespace fixture::nested
