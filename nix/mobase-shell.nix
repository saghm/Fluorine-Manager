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

    # Python (pybind11 installed via pip in shellHook to pin version)
    python313
    python313Packages.pip
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
    # Pin pybind11 to 2.13.6 (must match Docker build for ABI compat)
    pip install --quiet pybind11==2.13.6 2>/dev/null || true
    export CMAKE_PREFIX_PATH="${pkgs.qt6.qtbase}:${pkgs.qt6.qtwebengine}:${pkgs.qt6.qtwebsockets}:${pkgs.qt6.qtsvg}:$CMAKE_PREFIX_PATH"
  '';
}
