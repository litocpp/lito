lito.install({
    artifacts = {
        {
            package = "another-package",
            target = { kind = "bin", name = "tool" },
            destination = "bin/tool",
        },
    },
})
