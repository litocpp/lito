{
  description = "Module-first C++ builder";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };

        litoToml = builtins.fromTOML (builtins.readFile ./lito.toml);
        pname = litoToml.workspace.name;
        version = litoToml.workspace.package.version;

        llvmPkgs = pkgs.llvmPackages;

        rstdSrc = pkgs.fetchFromGitHub {
          owner = "litocpp";
          repo = "rstd";
          rev = "02cd6e8f83a8374527985a8f461bfd3a56ce0bec";
          hash = "sha256-TEH/t2tMjBnKBIQuC//9q0W3XMXQWzred6e5MRwBId4=";
        };

        luatoSrc = pkgs.fetchFromGitHub {
          owner = "litocpp";
          repo = "luato";
          rev = "9ad07ca2604022319c0178b7f5543220baf87050";
          hash = "sha256-C1DlycFz5z+e+A5FsL18ePc4KQYscn2z4cJKPBgkj8w=";
        };

        licryptoSrc = pkgs.fetchFromGitHub {
          owner = "litocpp";
          repo = "licrypto";
          rev = "18345239cc68869646a6522e6e258a4eba3dec20";
          hash = "sha256-UVk3BeTA8+cBvB9XFXKNo1ua4rBflpKif7NF+9a9l/Q=";
        };

        zstdSrc = pkgs.fetchFromGitHub {
          owner = "facebook";
          repo = "zstd";
          rev = "f8745da6ff1ad1e7bab384bd1f9d742439278e99";
          hash = "sha256-tNFWIT9ydfozB8dWcmTMuZLCQmQudTFJIkSr0aG7S44=";
        };

        luaSrc = pkgs.fetchzip {
          url = "https://www.lua.org/ftp/lua-5.5.1.tar.gz";
          hash = "sha256-vb3Nt5dMPL/G6L1MmJPGQnQT3F8p6iK6Gu2F/cG00ho=";
        };

        lito = llvmPkgs.libcxxStdenv.mkDerivation {
          inherit pname version;

          src = self;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            llvmPkgs.lld
          ];

          buildInputs = with pkgs; [
            zstd
            openssl
          ];

          cmakeFlags = [
            "-DCMAKE_C_COMPILER=clang"
            "-DCMAKE_CXX_COMPILER=clang++"
            "-DCMAKE_AR=${llvmPkgs.llvm}/bin/llvm-ar"
            "-DCMAKE_RANLIB=${llvmPkgs.llvm}/bin/llvm-ranlib"
            "-DCMAKE_CXX_COMPILER_AR=${llvmPkgs.llvm}/bin/llvm-ar"
            "-DCMAKE_C_COMPILER_AR=${llvmPkgs.llvm}/bin/llvm-ar"
            "-DCMAKE_CXX_COMPILER_RANLIB=${llvmPkgs.llvm}/bin/llvm-ranlib"
            "-DCMAKE_C_COMPILER_RANLIB=${llvmPkgs.llvm}/bin/llvm-ranlib"
            "-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES=${llvmPkgs.libcxx.dev}/include/c++/v1;${llvmPkgs.libcxx.dev}/include;${pkgs.glibc.dev}/include"
            "-DFETCHCONTENT_SOURCE_DIR_RSTD=${rstdSrc}"
            "-DFETCHCONTENT_SOURCE_DIR_LUATO=${luatoSrc}"
            "-DFETCHCONTENT_SOURCE_DIR_LICRYPTO=${licryptoSrc}"
            "-DFETCHCONTENT_SOURCE_DIR_ZSTD=${zstdSrc}"
            "-DFETCHCONTENT_SOURCE_DIR_LUA=${luaSrc}"
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          ];

          NIX_CFLAGS_COMPILE = "-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0";

          meta = with pkgs.lib; {
            description = "Module-first C++ builder";
            homepage = "https://github.com/litocpp/lito";
            license = with licenses; [
              mit
              asl20
            ];
            mainProgram = "lito";
            platforms = platforms.unix;
          };
        };

        devShell = pkgs.mkShell.override { stdenv = llvmPkgs.libcxxStdenv; } {
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            llvmPkgs.lld
          ];

          buildInputs = with pkgs; [
            zstd
            openssl
          ];
        };
      in
      {
        packages = {
          default = lito;
          inherit lito;
        };

        devShells.default = devShell;
      }
    );
}
