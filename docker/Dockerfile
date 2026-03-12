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
    && rm -rf /var/lib/apt/lists/*

# ── Qt 6.10 via aqtinstall ──
RUN pip3 install --break-system-packages aqtinstall && \
    aqt install-qt linux desktop 6.10.2 linux_gcc_64 \
      -m qtwebsockets qtwaylandcompositor \
      --outputdir /opt/qt6 && \
    ls /opt/qt6/6.10.2/gcc_64/bin/qmake && \
    (pip3 uninstall -y aqtinstall --break-system-packages 2>/dev/null || true)
ENV Qt6_DIR=/opt/qt6/6.10.2/gcc_64
ENV CMAKE_PREFIX_PATH="/opt/qt6/6.10.2/gcc_64:${CMAKE_PREFIX_PATH}"
ENV PATH="/opt/qt6/6.10.2/gcc_64/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/qt6/6.10.2/gcc_64/lib:${LD_LIBRARY_PATH}"

# ── uv (Python package manager) ──
RUN curl -LsSf https://astral.sh/uv/install.sh | sh
ENV PATH="/root/.local/bin:${PATH}"

# ── Build-time Python venv (pybind11, sip, etc.) ──
RUN uv venv /opt/build-python --python 3.13 --seed && \
    uv pip install --python /opt/build-python/bin/python \
      pybind11==2.13.6 sip psutil vdf
ENV BUILD_PYTHON=/opt/build-python/bin/python
ENV PATH="/opt/build-python/bin:${PATH}"

# ── Rust toolchain ──
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain stable --profile minimal

# ── Build and install libloot ──
ARG LIBLOOT_REF=master
RUN git clone --depth 1 --branch ${LIBLOOT_REF} \
      https://github.com/loot/libloot.git /tmp/libloot && \
    cmake -S /tmp/libloot/cpp -B /tmp/libloot/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DLIBLOOT_INSTALL_DOCS=OFF \
      -DBUILD_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local && \
    cmake --build /tmp/libloot/build --parallel && \
    cmake --install /tmp/libloot/build && \
    # Create pkg-config metadata
    mkdir -p /usr/local/lib/pkgconfig && \
    printf '%s\n' \
      'prefix=/usr/local' \
      'exec_prefix=${prefix}' \
      'libdir=${prefix}/lib' \
      'includedir=${prefix}/include' \
      '' \
      'Name: libloot' \
      'Description: LOOT C++ API library' \
      'Version: 0.29.0' \
      'Libs: -L${libdir} -lloot' \
      'Cflags: -I${includedir}' \
      > /usr/local/lib/pkgconfig/libloot.pc && \
    ldconfig && \
    rm -rf /tmp/libloot

# ── Pre-download linuxdeploy tooling (optional, only for AppImage builds) ──
# Set BUILD_APPIMAGE=1 to include linuxdeploy in the image.
# Without it, only tarball and installer builds are available.
ARG BUILD_APPIMAGE=0
RUN if [ "${BUILD_APPIMAGE}" = "1" ]; then \
        mkdir -p /opt/linuxdeploy && \
        curl -L --retry 3 -o /opt/linuxdeploy/linuxdeploy-x86_64.AppImage \
          https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage && \
        curl -L --retry 3 -o /opt/linuxdeploy/linuxdeploy-plugin-qt-x86_64.AppImage \
          https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage && \
        chmod +x /opt/linuxdeploy/linuxdeploy-x86_64.AppImage \
                 /opt/linuxdeploy/linuxdeploy-plugin-qt-x86_64.AppImage && \
        cd /opt/linuxdeploy && \
        ./linuxdeploy-plugin-qt-x86_64.AppImage --appimage-extract >/dev/null && \
        mv squashfs-root linuxdeploy-plugin-qt.AppDir && \
        chmod +x linuxdeploy-plugin-qt.AppDir/AppRun && \
        ln -sf /opt/linuxdeploy/linuxdeploy-plugin-qt.AppDir/AppRun \
               /opt/linuxdeploy/linuxdeploy-plugin-qt && \
        chmod -x /opt/linuxdeploy/linuxdeploy-plugin-qt-x86_64.AppImage; \
    else \
        echo "Skipping linuxdeploy (BUILD_APPIMAGE not set)"; \
    fi

WORKDIR /build
