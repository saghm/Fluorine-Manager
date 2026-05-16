FROM ubuntu:25.10

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV RUSTUP_HOME=/opt/rust/rustup
ENV CARGO_HOME=/opt/rust/cargo
ENV PATH="/opt/rust/cargo/bin:${PATH}"

# ── System build tools + all library dependencies ──
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build essentials
    build-essential cmake ninja-build ccache \
    git curl wget file binutils patchelf pkg-config ca-certificates \
    # Library dependencies
    libboost-all-dev \
    libsqlite3-dev \
    libtinyxml2-dev \
    libfontconfig1-dev \
    libspdlog-dev \
    libfuse3-dev fuse3 libcap-dev \
    liblz4-dev zlib1g-dev libzstd-dev libbz2-dev liblzma-dev \
    libssl-dev libcurl4-openssl-dev \
    libtomlplusplus-dev \
    # Qt 6 runtime deps (aqtinstall provides Qt 6.10 headers/tools/libs for build)
    kde-style-breeze \
    libgl-dev libopengl-dev libvulkan-dev libxkbcommon-dev libfontconfig1-dev \
    libfreetype-dev libx11-dev libxext-dev libxfixes-dev \
    libxi-dev libxrender-dev libxcb1-dev libxcb-cursor-dev \
    libxcb-glx0-dev libxcb-keysyms1-dev libxcb-image0-dev \
    libxcb-shm0-dev libxcb-icccm4-dev libxcb-sync-dev \
    libxcb-xfixes0-dev libxcb-shape0-dev libxcb-randr0-dev \
    libxcb-render-util0-dev libxcb-xinerama0-dev \
    libxcb-xkb-dev libxkbcommon-x11-dev libatspi2.0-dev \
    libwayland-dev \
    # Python
    python3 python3-pip python3-pyqt6 \
    # Misc
    zip unzip \
    # Static analysis
    clang-tidy \
    # mingw-w64 cross-toolchain for fluorine_vfs.dll (PE-side VFS injector,
    # loaded into Wine processes via AppInit_DLLs).
    gcc-mingw-w64-x86-64-posix \
    && rm -rf /var/lib/apt/lists/*

# ── Qt 6.10 via aqtinstall ──
RUN pip3 install --break-system-packages aqtinstall && \
    aqt install-qt linux desktop 6.10.2 linux_gcc_64 \
      -m qtwebsockets qtwaylandcompositor qtnetworkauth \
      --outputdir /opt/qt6 && \
    ls /opt/qt6/6.10.2/gcc_64/bin/qmake && \
    (pip3 uninstall -y aqtinstall --break-system-packages 2>/dev/null || true)
ENV Qt6_DIR=/opt/qt6/6.10.2/gcc_64
ENV CMAKE_PREFIX_PATH="/opt/qt6/6.10.2/gcc_64:${CMAKE_PREFIX_PATH}"
ENV PATH="/opt/qt6/6.10.2/gcc_64/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/qt6/6.10.2/gcc_64/lib:${LD_LIBRARY_PATH}"

# ── python-build-standalone 3.12 (build-time + bundled runtime) ──
# Single Python for both compiling pybind11 modules and shipping in the tarball.
# The install_only_stripped tarball is ~28MB; we strip it further in build-inner.sh.
ARG PBS_VERSION=3.12.13
ARG PBS_DATE=20260310
RUN curl -fL --retry 3 -o /tmp/pbs-python.tar.gz \
      "https://github.com/astral-sh/python-build-standalone/releases/download/${PBS_DATE}/cpython-${PBS_VERSION}%2B${PBS_DATE}-x86_64-unknown-linux-gnu-install_only.tar.gz" && \
    tar xzf /tmp/pbs-python.tar.gz -C /opt/ && \
    mv /opt/python /opt/python-bundled && \
    rm /tmp/pbs-python.tar.gz
ENV BUILD_PYTHON=/opt/python-bundled/bin/python3
ENV PATH="/opt/python-bundled/bin:${PATH}"

# ── Build-time Python packages ──
# pybind11/sip/etc. for compiling mobase.so; PyQt6 is staged into the distribution.
RUN /opt/python-bundled/bin/pip3 install --no-cache-dir \
      pybind11==2.13.6 sip psutil larian-formats==0.8.1 vdf "PyQt6>=6.10,<6.11"

# ── Rust toolchain ──
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain stable --profile minimal

WORKDIR /build
