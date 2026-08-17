export module lito.core:dependency.visibility;

export namespace lito::dependency
{

enum class DependencyVisibility
{
    Public,
    Private,
    LinkOnly,
};

} // namespace lito::dependency
