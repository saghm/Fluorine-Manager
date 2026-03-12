{ pkgs ? import <nixpkgs> {} }:

let
  python = pkgs.python313;
  pythonWithPkgs = python.withPackages (ps: [
    ps.sip
    ps.pyqt6
  ]);
in
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    pkg-config
    git
    rustc
    cargo
  ];

  buildInputs = with pkgs; [
    # Qt 6
    qt6.qtbase
    qt6.wrapQtAppsHook
    qt6.qtwebengine
    qt6.qtwebsockets
    qt6.qtsvg
    qt6.qtwayland

    # Python
    pythonWithPkgs

    # Libraries
    boost
    sqlite
    tinyxml-2
    fontconfig
    spdlog
    fuse3
    libcap
    lz4
    zlib
    zstd
    bzip2
    xz
    openssl
    curl
    tomlplusplus
    fmt
  ];

  shellHook = ''
    # Create a temporary venv to install pinned pybind11 (nixpkgs version is too new)
    if [ ! -d .nix-venv ]; then
      python3 -m venv .nix-venv --system-site-packages
      .nix-venv/bin/pip install --quiet pybind11==2.13.6
    fi
    export PATH="$PWD/.nix-venv/bin:$PATH"
    export PYTHONPATH="$PWD/.nix-venv/lib/python3.13/site-packages:$PYTHONPATH"
    export CMAKE_PREFIX_PATH="${pkgs.qt6.qtbase}:${pkgs.qt6.qtwebengine}:${pkgs.qt6.qtwebsockets}:${pkgs.qt6.qtsvg}:$CMAKE_PREFIX_PATH"
  '';
}
