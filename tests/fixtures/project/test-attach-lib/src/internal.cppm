export module fixture.test.attach:internal;

namespace fixture::attach::internal
{

inline auto registrations = 0;

inline auto register_test() noexcept -> void {
    ++registrations;
}

} // namespace fixture::attach::internal
