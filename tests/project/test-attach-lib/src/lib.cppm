export module fixture.test.attach;

import :internal;

export namespace fixture::attach
{

auto registrations() noexcept -> int {
    return internal::registrations;
}

} // namespace fixture::attach
