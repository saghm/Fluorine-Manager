The preview regressions exercise the actual widgets, shaders and OpenGL resources.
Build `test_nif_preview` and `test_xrandrinstaller` with `BUILD_FLUORINE_TESTING=ON`,
then run them through CTest:

```sh
cmake --build build --target test_nif_preview test_xrandrinstaller
ctest --test-dir build -R '^test_(nif_preview|xrandrinstaller)$' --output-on-failure
```

NIF rendering tests need a desktop OpenGL display. With `QT_QPA_PLATFORM=offscreen`,
only its early-input regression runs; the GL cases explicitly skip. The xrandr
tests run headlessly, including an xz-compressed Debian payload with no `ar` in
`PATH`. They never download packages or modify an installed runtime.

DDS tests also need a desktop OpenGL display and PyQt6. CTest reports a skip if
either is unavailable. From the repository root, test the shipped bindings with:

```sh
env LD_LIBRARY_PATH="$PWD/build/fluorine-manager/lib" \
    PYTHONHOME="$PWD/build/fluorine-manager/python" \
    PYTHONPATH="$PWD/build/fluorine-manager/plugins/libs" \
    QT_PLUGIN_PATH="$PWD/build/fluorine-manager/qt6plugins" \
    QT_QPA_PLATFORM=xcb \
    build/fluorine-manager/python/bin/python3.12 src/tests/test_dds_preview.py
```

Use the matching bundle directory/interpreter for other builds, and `wayland`
instead of `xcb` when testing a native Wayland session. The DDS cases cover core
and compatibility contexts, integer and compressed textures, cubemaps, channel
selection, missing functions, shader failure, parent deletion and reparenting.
