"""Check package resource generation and its original inputs through the public CLI."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lito", type=Path, required=True)
    parser.add_argument("--embed", type=Path, required=True)
    parser.add_argument("--llvm", type=Path, default=Path("/nix/opt/llvm/22/bin"))
    parser.add_argument("--root", type=Path)
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[3]
    root = args.root.resolve() if args.root else Path(tempfile.mkdtemp(prefix="lito-package-embed-"))
    root.mkdir(parents=True, exist_ok=True)
    project = root / "project"
    fixture = root / "fixture"
    assert not project.exists() and not fixture.exists(), f"test directories already exist: {root}"
    print(f"package embed checks; logs: {root}", flush=True)

    def write(path, text):
        path = root / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    quote = json.dumps
    liresto = os.path.relpath(workspace / "liresto/core", project)
    write("project/lito.toml", f'''[package]
name = "package-embed-test"
version = "0.1.0"
[lib]
name = "package-embed-test"
module = "test.resources"
archive = "test.resources"
[[bin]]
name = "package-embed-test"
sources = ["main.cpp"]
link-stdlib = false
[dependencies]
liresto = {{ path = {quote(liresto)}, pub = true }}
rstd-nostd = {{ version = "0.1.9", usage = "link" }}
[build-tools.pack]
path = {quote(str(args.embed.resolve()))}
[external-sources.fixture]
path = "../fixture"
[profile.base]
exceptions = false
rtti = false
''')
    patches = "\n".join(
        f'{name} = {{ path = {quote(str(workspace / "rstd"))} }}'
        for name in (
            "rstd-basic", "rstd-runtime", "rstd-core", "rstd-alloc",
            "rstd-parse", "rstd-std", "rstd-nostd",
        )
    )
    write("project/.lito/config.toml", f'''[environment]
append-path = [{quote(str(args.llvm.resolve()))}]
[patch.litocpp]
{patches}
licrypto = {{ path = {quote(str(workspace / "licrypto"))} }}
''')
    write("project/src/lib.cppm", '''export module test.resources;
export import liresto;
export namespace test { auto resources() -> liresto::ResourceBundle; }
''')
    write("project/main.cpp", '''import test.resources;
using namespace rstd::prelude;
using namespace rstd::literals;

auto resource_bytes(ref<str> path) -> rstd::bytes::Bytes {
    auto bundle = test::resources();
    auto resource = bundle.get(path).unwrap().unwrap();
    return resource.bytes();
}

auto main() -> int {
    auto archive = resource_bytes("archive"_str);
    auto descriptor = resource_bytes("descriptor"_str);
    auto archive_path = rstd::path::PathBuf::from("archive.snapshot"_str);
    auto descriptor_path = rstd::path::PathBuf::from("descriptor.snapshot"_str);
    if (rstd::fs::write(archive_path.as_path(), archive.as_slice()).is_err()) return 1;
    if (rstd::fs::write(descriptor_path.as_path(), descriptor.as_slice()).is_err()) return 2;
    if (test::resources().get("missing"_str).unwrap().is_some()) return 3;
    return 0;
}
''')
    write("project/build.lua", '''local target = lito.target({kind="lib", name="package-embed-test"})
local source = lito.external_source(target, "fixture")
local generated = lito.run({
  tool=lito.tool("pack"), cwd=".", input_roots={source},
  args={
    "--root", "@INPUT_ROOT:1@", "--package", "embed-fixture",
    "--registry", "https://registry.example/",
    "--archive", "@OUTPUT:1@", "--descriptor", "@OUTPUT:2@",
    "--source", "@OUTPUT:3@", "--blob", "@OUTPUT:4@", "--depfile", "@OUTPUT:5@",
    "--module", "test.resources", "--namespace", "test", "--function", "resources",
  },
  inputs={source.file("package/lito.toml")},
  outputs={"embedded/package.tar.zst", "embedded/package.json", "embedded/resources.cpp",
    "embedded/resources.bin", "embedded/resources.d"},
  depfile={output=5},
})
lito.target_add_generated_source(target, generated.outputs[3])
''')
    write("fixture/lito.toml", '''[workspace]
name = "embed-fixture-workspace"
members = ["package"]
[workspace.package]
version = "0.1.0"
readme = "README.md"
''')
    write("fixture/README.md", "# Shared package README\n")
    write("fixture/package/lito.toml", '''[package]
name = "embed-fixture"
version.workspace = true
readme.workspace = true
[lib]
name = "embed-fixture"
module = "fixture.embedded"
archive = "fixture.embedded"
''')
    write("fixture/package/src/lib.cppm", "export module fixture.embedded;\n")
    write("fixture/package/assets/original.txt", "original\n")
    build = root / "build"
    generated = build / "generated/package-embed-test/embedded"
    executable = build / "bin/package-embed-test/package-embed-test"
    if os.name == "nt":
        executable = executable.with_suffix(".exe")
    snapshots = root / "snapshots"
    snapshots.mkdir()
    invocation = [
        str(args.lito.resolve()), "build", "--offline", "--profile", "release",
        "-j", "4", "--build-dir", str(build),
    ]
    previous = None

    def dep_name(path):
        return "".join(
            "$$" if character == "$" else "\\" + character
            if character in " \\#:\t" else character
            for character in str(path.resolve())
        )

    def check(name, version="0.1.0", count=None, reused=False):
        nonlocal previous
        result = subprocess.run(invocation, cwd=project, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (root / f"{name}.log").write_text(result.stdout, encoding="utf-8")
        assert result.returncode == 0, f"{name}:\n{result.stdout[-6000:]}"
        if reused:
            assert "0 compiled" in result.stdout, result.stdout
            assert "[build-tool-run]" not in result.stdout, result.stdout
        else:
            assert "[build-tool-run]" in result.stdout, result.stdout
        consumed = subprocess.run([str(executable)], cwd=snapshots, text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (root / f"{name}-consumer.log").write_text(consumed.stdout, encoding="utf-8")
        assert consumed.returncode == 0, f"resource consumer failed: {consumed.stdout}"
        archive = (generated / "package.tar.zst").read_bytes()
        descriptor_bytes = (generated / "package.json").read_bytes()
        assert (snapshots / "archive.snapshot").read_bytes() == archive
        assert (snapshots / "descriptor.snapshot").read_bytes() == descriptor_bytes
        descriptor = json.loads(descriptor_bytes)
        assert descriptor["package"] == "embed-fixture"
        assert descriptor["version"] == version
        assert descriptor["archive"]["checksum"] == hashlib.sha256(archive).hexdigest()
        assert int(descriptor["archive"]["size"]) == len(archive)
        if count is not None:
            assert int(descriptor["file_count"]) == count, descriptor
        if previous is not None:
            if reused:
                assert descriptor_bytes == previous
            else:
                assert descriptor_bytes != previous, f"{name}: package output did not change"
        previous = descriptor_bytes
        depfile = (generated / "resources.d").read_text(encoding="utf-8")
        target, separator, dependencies = depfile.partition(": ")
        assert separator and "resources.cpp" in target, depfile
        for output in generated.iterdir():
            assert dep_name(output) not in dependencies, f"output is its own dependency: {depfile}"
        for source in (fixture / "lito.toml", fixture / "README.md",
                       fixture / "package/lito.toml"):
            assert dep_name(source) in dependencies, f"missing original input {source}: {depfile}"
        print(f"{name}: passed", flush=True)
        return int(descriptor["file_count"])

    count = check("01-cold")
    check("02-warm", count=count, reused=True)
    write("fixture/package/assets/original.txt", "modified\n")
    check("03-modify", count=count)
    write("fixture/package/assets/added.txt", "added\n")
    check("04-add", count=count + 1)
    (fixture / "package/assets/added.txt").rename(fixture / "package/assets/renamed.txt")
    check("05-rename", count=count + 1)
    (fixture / "package/assets/renamed.txt").unlink()
    check("06-delete", count=count)
    inherited = fixture / "lito.toml"
    inherited.write_text(inherited.read_text().replace('version = "0.1.0"', 'version = "0.1.1"'))
    check("07-workspace-version", version="0.1.1", count=count)
    write("fixture/README.md", "# Updated shared package README\n")
    check("08-external-readme", version="0.1.1", count=count)
    check("09-final-warm", version="0.1.1", count=count, reused=True)
    print(f"package embed resource and invalidation checks passed; logs: {root}")


if __name__ == "__main__":
    main()
