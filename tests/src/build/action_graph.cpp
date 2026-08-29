#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

auto domain() -> lito::ExecutionDomainId {
    return lito::ExecutionDomainId { .value = String::make("native"_str) };
}

auto artifact(lito::BuildActionGraph& graph,
              ref<str>                identity,
              lito::BuildArtifactKind kind,
              bool                    ready = false) -> lito::BuildArtifactId {
    return graph
        .add_artifact(lito::BuildArtifactSpec {
            .identity        = String::make(identity),
            .domain          = domain(),
            .kind            = kind,
            .initially_ready = ready,
        })
        .unwrap();
}

auto ids(lito::BuildArtifactId value) -> Vec<lito::BuildArtifactId> {
    auto result = Vec<lito::BuildArtifactId>::make();
    result.push(rstd::move(value));
    return result;
}

} // namespace

TEST(BuildActionGraph, PropagatesArtifactReadinessAcrossCompileArchiveAndLink) {
    auto graph      = lito::BuildActionGraph {};
    auto source     = artifact(graph, "source"_str, lito::BuildArtifactKind::Source, true);
    auto object     = artifact(graph, "object"_str, lito::BuildArtifactKind::Object);
    auto archive    = artifact(graph, "archive"_str, lito::BuildArtifactKind::Archive);
    auto executable = artifact(graph, "executable"_str, lito::BuildArtifactKind::Executable);
    auto compile    = graph
                          .add_action(lito::BuildActionSpec {
                              .identity = String::make("compile"_str),
                              .domain   = domain(),
                              .kind     = lito::BuildActionKind::Compile,
                              .inputs   = ids(source),
                              .outputs  = ids(object),
                          })
                          .unwrap();
    auto archived   = graph
                          .add_action(lito::BuildActionSpec {
                              .identity = String::make("archive"_str),
                              .domain   = domain(),
                              .kind     = lito::BuildActionKind::Archive,
                              .inputs   = ids(object),
                              .outputs  = ids(archive),
                          })
                          .unwrap();
    auto linked     = graph
                          .add_action(lito::BuildActionSpec {
                              .identity = String::make("link"_str),
                              .domain   = domain(),
                              .kind     = lito::BuildActionKind::Link,
                              .inputs   = ids(archive),
                              .outputs  = ids(executable),
                          })
                          .unwrap();

    ASSERT_TRUE(graph.validate().is_ok());
    ASSERT_EQ(graph.ready_actions().len(), usize(1));
    EXPECT_EQ(graph.ready_actions()[usize {}], compile);
    ASSERT_TRUE(graph.mark_running(compile).is_ok());
    ASSERT_TRUE(graph.mark_succeeded(compile).is_ok());
    ASSERT_EQ(graph.ready_actions().len(), usize(1));
    EXPECT_EQ(graph.ready_actions()[usize {}], archived);
    ASSERT_TRUE(graph.mark_succeeded(archived).is_ok());
    ASSERT_EQ(graph.ready_actions().len(), usize(1));
    EXPECT_EQ(graph.ready_actions()[usize {}], linked);
}

TEST(BuildActionGraph, LinksReadyClosureBeforeUnrelatedCompileCompletes) {
    auto graph         = lito::BuildActionGraph {};
    auto first_source  = artifact(graph, "first-source"_str, lito::BuildArtifactKind::Source, true);
    auto other_source  = artifact(graph, "other-source"_str, lito::BuildArtifactKind::Source, true);
    auto first_object  = artifact(graph, "first-object"_str, lito::BuildArtifactKind::Object);
    auto other_object  = artifact(graph, "other-object"_str, lito::BuildArtifactKind::Object);
    auto executable    = artifact(graph, "executable"_str, lito::BuildArtifactKind::Executable);
    auto first_compile = graph
                             .add_action(lito::BuildActionSpec {
                                 .identity = String::make("first-compile"_str),
                                 .domain   = domain(),
                                 .kind     = lito::BuildActionKind::Compile,
                                 .inputs   = ids(first_source),
                                 .outputs  = ids(first_object),
                             })
                             .unwrap();
    auto other_compile = graph
                             .add_action(lito::BuildActionSpec {
                                 .identity = String::make("other-compile"_str),
                                 .domain   = domain(),
                                 .kind     = lito::BuildActionKind::Compile,
                                 .inputs   = ids(other_source),
                                 .outputs  = ids(other_object),
                             })
                             .unwrap();
    auto link          = graph
                             .add_action(lito::BuildActionSpec {
                                 .identity = String::make("link"_str),
                                 .domain   = domain(),
                                 .kind     = lito::BuildActionKind::Link,
                                 .inputs   = ids(first_object),
                                 .outputs  = ids(executable),
                             })
                             .unwrap();

    ASSERT_TRUE(graph.mark_succeeded(first_compile).is_ok());
    EXPECT_EQ(graph.action(other_compile).unwrap()->state, lito::BuildActionState::Ready);
    EXPECT_EQ(graph.action(link).unwrap()->state, lito::BuildActionState::Ready);
}

TEST(BuildActionGraph, ConnectsConsumerDeclaredBeforeProducer) {
    auto graph   = lito::BuildActionGraph {};
    auto source  = artifact(graph, "source"_str, lito::BuildArtifactKind::Source, true);
    auto object  = artifact(graph, "object"_str, lito::BuildArtifactKind::Object);
    auto output  = artifact(graph, "output"_str, lito::BuildArtifactKind::Executable);
    auto link    = graph
                       .add_action(lito::BuildActionSpec {
                           .identity = String::make("link"_str),
                           .domain   = domain(),
                           .kind     = lito::BuildActionKind::Link,
                           .inputs   = ids(object),
                           .outputs  = ids(output),
                       })
                       .unwrap();
    auto compile = graph
                       .add_action(lito::BuildActionSpec {
                           .identity = String::make("compile"_str),
                           .domain   = domain(),
                           .kind     = lito::BuildActionKind::Compile,
                           .inputs   = ids(source),
                           .outputs  = ids(object),
                       })
                       .unwrap();

    ASSERT_TRUE(graph.validate().is_ok());
    ASSERT_EQ(graph.action(link).unwrap()->prerequisites.len(), usize(1));
    EXPECT_EQ(graph.action(link).unwrap()->prerequisites[usize {}], compile);
    ASSERT_EQ(graph.action(compile).unwrap()->dependents.len(), usize(1));
    EXPECT_EQ(graph.action(compile).unwrap()->dependents[usize {}], link);
}

TEST(BuildActionGraph, RejectsConflictingProducerAndBlocksDependents) {
    auto graph    = lito::BuildActionGraph {};
    auto source   = artifact(graph, "source"_str, lito::BuildArtifactKind::Source, true);
    auto object   = artifact(graph, "object"_str, lito::BuildArtifactKind::Object);
    auto output   = artifact(graph, "output"_str, lito::BuildArtifactKind::Executable);
    auto compile  = graph
                        .add_action(lito::BuildActionSpec {
                            .identity = String::make("compile"_str),
                            .domain   = domain(),
                            .kind     = lito::BuildActionKind::Compile,
                            .inputs   = ids(source),
                            .outputs  = ids(object),
                        })
                        .unwrap();
    auto conflict = graph.add_action(lito::BuildActionSpec {
        .identity = String::make("other-compile"_str),
        .domain   = domain(),
        .kind     = lito::BuildActionKind::Compile,
        .inputs   = ids(source),
        .outputs  = ids(object),
    });
    EXPECT_TRUE(conflict.is_err());
    auto link = graph
                    .add_action(lito::BuildActionSpec {
                        .identity = String::make("link"_str),
                        .domain   = domain(),
                        .kind     = lito::BuildActionKind::Link,
                        .inputs   = ids(object),
                        .outputs  = ids(output),
                    })
                    .unwrap();
    ASSERT_TRUE(graph.mark_failed(compile).is_ok());
    EXPECT_EQ(graph.action(compile).unwrap()->state, lito::BuildActionState::Failed);
    EXPECT_EQ(graph.action(link).unwrap()->state, lito::BuildActionState::Blocked);
}

TEST(BuildActionGraph, RejectsNativeInputsFromAnotherExecutionDomain) {
    auto graph   = lito::BuildActionGraph {};
    auto source  = graph
                       .add_artifact(lito::BuildArtifactSpec {
                           .identity = String::make("source"_str),
                           .domain =
                               lito::ExecutionDomainId {
                                   .value = String::make("host"_str),
                               },
                           .kind            = lito::BuildArtifactKind::Source,
                           .initially_ready = true,
                       })
                       .unwrap();
    auto object  = artifact(graph, "object"_str, lito::BuildArtifactKind::Object);
    auto compile = graph.add_action(lito::BuildActionSpec {
        .identity = String::make("compile"_str),
        .domain   = domain(),
        .kind     = lito::BuildActionKind::Compile,
        .inputs   = ids(source),
        .outputs  = ids(object),
    });
    EXPECT_TRUE(compile.is_err());
}

TEST(BuildActionGraph, KeepsSameLogicalArtifactSeparateAcrossExecutionDomains) {
    auto graph  = lito::BuildActionGraph {};
    auto host   = graph
                      .add_artifact(lito::BuildArtifactSpec {
                          .identity = String::make("shared-source"_str),
                          .domain =
                              lito::ExecutionDomainId {
                                  .value = String::make("host"_str),
                              },
                          .kind            = lito::BuildArtifactKind::Source,
                          .initially_ready = true,
                      })
                      .unwrap();
    auto target = graph
                      .add_artifact(lito::BuildArtifactSpec {
                          .identity        = String::make("shared-source"_str),
                          .domain          = domain(),
                          .kind            = lito::BuildArtifactKind::Source,
                          .initially_ready = true,
                      })
                      .unwrap();

    EXPECT_NE(host, target);
    EXPECT_EQ(graph.artifacts()->len(), usize(2));
}

TEST(BuildActionGraph, RejectsDependencyCycles) {
    auto graph  = lito::BuildActionGraph {};
    auto first  = artifact(graph, "first"_str, lito::BuildArtifactKind::Object);
    auto second = artifact(graph, "second"_str, lito::BuildArtifactKind::Object);
    static_cast<void>(graph
                          .add_action(lito::BuildActionSpec {
                              .identity = String::make("first-action"_str),
                              .domain   = domain(),
                              .kind     = lito::BuildActionKind::Compile,
                              .inputs   = ids(second),
                              .outputs  = ids(first),
                          })
                          .unwrap());
    static_cast<void>(graph
                          .add_action(lito::BuildActionSpec {
                              .identity = String::make("second-action"_str),
                              .domain   = domain(),
                              .kind     = lito::BuildActionKind::Compile,
                              .inputs   = ids(first),
                              .outputs  = ids(second),
                          })
                          .unwrap());

    EXPECT_TRUE(graph.validate().is_err());
}
