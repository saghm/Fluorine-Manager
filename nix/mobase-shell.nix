{ pkgs ? import <nixpkgs> {} }:

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

    # Python + pybind11
    python313
    python313Packages.pybind11
    python313Packages.sip
    python313Packages.pyqt6

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
    export PYTHONPATH="${pkgs.python313Packages.pybind11}/${pkgs.python313.sitePackages}:$PYTHONPATH"
    export CMAKE_PREFIX_PATH="${pkgs.qt6.qtbase}:${pkgs.qt6.qtwebengine}:${pkgs.qt6.qtwebsockets}:${pkgs.qt6.qtsvg}:$CMAKE_PREFIX_PATH"
  '';
}
