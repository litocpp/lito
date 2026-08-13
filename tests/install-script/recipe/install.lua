local rendered = lito.render_template({
    input = "fragment.in",
    values = {
        NAME = lito.package_name,
        VERSION = lito.package_version,
    },
})

lito.install({
    artifacts = {
        {
            target = { kind = "bin", name = "producer" },
            destination = "bin/producer",
        },
    },
    external_assets = {
        {
            dependency = "runtime",
            set = "files",
            destination = "lib/runtime",
        },
    },
    files = {
        { source = "resource.txt", destination = "share/fixture/resource.txt" },
    },
    templates = {
        {
            input = "manifest.in",
            destination = "share/fixture/manifest.txt",
            values = {
                FRAGMENT = rendered,
                PROFILE = lito.profile,
                TARGET = lito.target,
                ARCH = lito.target_arch,
            },
        },
    },
    inventories = {
        {
            destination = "share/fixture/files.txt",
            relative_to = "share/fixture",
        },
    },
})
