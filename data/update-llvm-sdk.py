#!/usr/bin/env python3

import json
import os
import re
import sys
import urllib.error
import urllib.request


VERSION_PATTERN = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")


def fail(message: str) -> None:
    print(f"update-llvm-sdk: {message}", file=sys.stderr)
    raise SystemExit(1)


def release(version: str) -> dict:
    tag = f"llvmorg-{version}"
    request = urllib.request.Request(
        f"https://api.github.com/repos/llvm/llvm-project/releases/tags/{tag}",
        headers={
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "lito-llvm-sdk-catalog-maintainer",
        },
    )
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(request) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        fail(f"GitHub API returned HTTP {error.code} for {tag}")
    except urllib.error.URLError as error:
        fail(f"GitHub API request failed: {error.reason}")


def catalog_candidate(version: str, document: dict) -> dict:
    tag = f"llvmorg-{version}"
    if document.get("tag_name") != tag:
        fail("GitHub API returned a different release tag")
    if document.get("draft") or document.get("prerelease"):
        fail(f"{tag} is not a final release")

    approved = {
        f"LLVM-{version}-Linux-X64.tar.xz": {
            "os": "linux",
            "architecture": "x86_64",
        },
        f"clang+llvm-{version}-x86_64-pc-windows-msvc.tar.xz": {
            "os": "windows",
            "architecture": "x86_64",
        },
        f"clang+llvm-{version}-aarch64-pc-windows-msvc.tar.xz": {
            "os": "windows",
            "architecture": "aarch64",
        },
    }
    selected = {}
    for asset in document.get("assets", []):
        name = asset.get("name")
        if name not in approved:
            continue
        if name in selected:
            fail(f"release contains duplicate approved asset {name!r}")
        digest = asset.get("digest")
        if not isinstance(digest, str) or re.fullmatch(r"sha256:[0-9a-f]{64}", digest) is None:
            fail(f"approved asset {name!r} has no lowercase SHA-256 digest")
        size = asset.get("size")
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            fail(f"approved asset {name!r} has an invalid size")
        url = asset.get("browser_download_url")
        if not isinstance(url, str) or not url.startswith("https://") or "#" in url:
            fail(f"approved asset {name!r} has an invalid download URL")
        selected[name] = {
            "host": approved[name],
            "asset": name,
            "url": url,
            "sha256": digest.removeprefix("sha256:"),
            "size": size,
        }

    missing = [name for name in approved if name not in selected]
    if missing:
        fail(f"release is missing approved asset {missing[0]!r}")
    return {
        "schema": 1,
        "kind": "lito-llvm-sdk-catalog-candidate",
        "version": version,
        "upstream-tag": tag,
        "artifacts": [selected[name] for name in sorted(selected)],
    }


def main() -> None:
    if len(sys.argv) != 2 or VERSION_PATTERN.fullmatch(sys.argv[1]) is None:
        fail("usage: update-llvm-sdk MAJOR.MINOR.PATCH")
    version = sys.argv[1]
    candidate = catalog_candidate(version, release(version))
    json.dump(candidate, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
