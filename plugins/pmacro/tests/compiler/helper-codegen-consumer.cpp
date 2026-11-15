import helper_codegen;

static_assert(helper_codegen::helper_fields_checked && helper_codegen::helper_second_checked);

auto main() -> int {
    return helper_codegen::check() != 210 || helper_codegen::Mask {}.allows() != 42 ||
           helper_codegen::Outer::Nested {}.read() != 42 ||
           helper_codegen::Box<long> {}.read() != 42 || helper_codegen::Snapshot {}.read() != 42;
}
