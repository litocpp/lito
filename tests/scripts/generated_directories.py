"""Check generated directory ownership and caching through the public CLI."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lito", type=Path, required=True)
    parser.add_argument("--llvm", default="/nix/opt/llvm/22/bin")
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix="lito-generated-directories-"))

    def write(name, text):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    write("lito.toml", f'''[package]
name = "directory-test"
version = "0.1.0"
[[bin]]
name = "directory-test"
sources = ["main.cpp"]
link-stdlib = false
[build-tools.python]
path = {json.dumps(sys.executable)}
''')
    write(".lito/config.toml", f'[environment]\nappend-path = [{json.dumps(args.llvm)}]\n')
    write("main.cpp", "int main() { return 0; }\n")
    write("produce.py", "import shutil, sys\nshutil.copytree(sys.argv[1], sys.argv[2], symlinks=True)\n")
    write("consume.py", "import pathlib, sys\np=pathlib.Path(sys.argv[1]); pathlib.Path(sys.argv[2]).write_text(''.join(sorted(str(x.relative_to(p)) for x in p.rglob('*'))))\n")
    script = '''local tool = lito.tool("python")
local site = lito.run({tool=tool, cwd=".",
  args={"@INPUT:1@", "@INPUT:2@", "@OUTPUT@"}, inputs={"produce.py", "assets"},
  outputs={{path="site", directory=true}}})
lito.run({tool=tool, cwd=".",
  args={"@INPUT:1@", "@INPUT:2@", "@OUTPUT@"}, inputs={"consume.py", site.outputs[1]},
  outputs={"listing.txt"}})
lito.run({tool=tool, cwd=".",
  args={"-c", "import pathlib,sys; pathlib.Path(sys.argv[1]).write_text('constant'); pathlib.Path(sys.argv[2]).write_text('constant.txt:')", "@OUTPUT:1@", "@OUTPUT:2@"},
  outputs={"constant.txt", "constant.d"}, depfile={output=2}})
'''
    write("build.lua", script)
    write("assets/index.html", "home")
    calls = 0

    def build(success=True):
        nonlocal calls
        calls += 1
        result = subprocess.run([str(args.lito.resolve()), "build", "--offline", "--profile", "release"],
                                cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        write(f"log-{calls}.txt", result.stdout)
        assert (result.returncode == 0) == success, result.stdout
        return result.stdout

    generated = root / "build/release/generated/directory-test"
    build()
    assert (generated / "site/index.html").read_text() == "home"
    assert "[build-tool-run]" not in build()
    write("assets/index.html", "changed")
    build()
    assert (generated / "site/index.html").read_text() == "changed"
    write("assets/nested/new.txt", "new")
    build()
    assert "nested/new.txt" in (generated / "listing.txt").read_text()
    (root / "assets/nested/new.txt").unlink()
    build()
    assert not (generated / "site/nested/new.txt").exists()
    assert (generated / "site/nested").is_dir()
    (root / "assets/nested").rmdir()
    build()
    assert not (generated / "site/nested").exists()
    write("produce.py", "import pathlib, sys\np=pathlib.Path(sys.argv[2]); p.mkdir(); (p/'partial').write_text('bad'); sys.exit(1)\n")
    build(False)
    assert (generated / "site/index.html").read_text() == "changed"
    assert not (generated / "site/partial").exists()
    write("build.lua", script + '''lito.run({tool=tool, cwd=".", args={"-c", "pass"},
  outputs={"site/conflict.txt"}})
''')
    build(False)
    write("build.lua", '''lito.run({tool=lito.tool("python"), cwd=".", args={"-c", "pass"},
  outputs={{path="site", directory=true}}, depfile={output=1}})
''')
    build(False)
    write("build.lua", '''lito.run({tool=lito.tool("python"), cwd=".", args={"-c", "pass"},
  outputs={{path="site", directory=true}}, output_cwd=1})
''')
    build(False)
    print(f"generated directory checks passed; logs: {root}")


if __name__ == "__main__":
    main()
