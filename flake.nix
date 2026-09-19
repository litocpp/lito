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

        deps = builtins.fromJSON (builtins.readFile ./deps.json);
        fetchDependency =
          name:
          let
            dep = deps.${name};
          in
          pkgs.fetchFromGitHub {
            inherit (dep) owner repo rev;
            hash = dep.narHash;
          };

        rstdSrc = fetchDependency "rstd";
        luatoSrc = fetchDependency "luato";
        licryptoSrc = fetchDependency "licrypto";
        zstdSrc = fetchDependency "zstd";

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
