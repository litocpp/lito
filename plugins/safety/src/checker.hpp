#pragma once

namespace safety_plugin
{
auto make_consumer(clang::CompilerInstance&) -> std::unique_ptr<clang::ASTConsumer>;
}
