{
  description = "Fluorine Manager — Mod Organizer 2 for Linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };

      version = "0.1.0";
      pythonVersion = "3.13";
      portablePythonVersion = "3.13.9";

      # ── libloot (not in nixpkgs — build from source) ──
      libloot = pkgs.stdenv.mkDerivation {
        pname = "libloot";
        version = "0.29.0";

        src = pkgs.fetchFromGitHub {
          owner = "loot";
          repo = "libloot";
          rev = "master";
          hash = "";  # nix will tell you the correct hash on first build
          fetchSubmodules = true;
        };

        nativeBuildInputs = with pkgs; [ cmake ninja ];

        cmakeDir = "../cpp";
        cmakeFlags = [
          "-DBUILD_SHARED_LIBS=ON"
          "-DLIBLOOT_INSTALL_DOCS=OFF"
          "-DBUILD_TESTING=OFF"
        ];

        postInstall = ''
          mkdir -p $out/lib/pkgconfig
          cat > $out/lib/pkgconfig/libloot.pc <<EOF
          prefix=$out
          exec_prefix=''${prefix}
          libdir=''${prefix}/lib
          includedir=''${prefix}/include

          Name: libloot
          Description: LOOT C++ API library
          Version: 0.29.0
          Libs: -L''${libdir} -lloot
          Cflags: -I''${includedir}
          EOF
        '';
      };

      # ── Rust FFI crates (built separately, then injected into cmake) ──

      # bsa_ffi is self-contained (no local path deps).
      bsa-ffi = pkgs.rustPlatform.buildRustPackage {
        pname = "bsa-ffi";
        inherit version;

        src = ./libs/bsa_ffi;
        cargoLock.lockFile = ./libs/bsa_ffi/Cargo.lock;

        # Output is a cdylib (.so).
        installPhase = ''
          mkdir -p $out/lib
          cp target/release/libbsa_ffi.so $out/lib/
        '';
      };

      # nak_ffi depends on nak_rust via `path = "../nak"`.  We give
      # rustPlatform the parent directory so both crates are visible,
      # and build only nak_ffi.
      nak-ffi = pkgs.rustPlatform.buildRustPackage {
        pname = "nak-ffi";
        inherit version;

        # Source must include both libs/nak and libs/nak_ffi.
        src = pkgs.lib.cleanSourceWith {
          src = ./libs;
          filter = path: type:
            let baseName = builtins.baseNameOf path; in
            # Include nak/ and nak_ffi/ directories (and their contents).
            builtins.match ".*/libs/(nak|nak_ffi)(/.*|$)" path != null
            # Also include the top-level libs/ dir itself.
            || type == "directory" && baseName == "libs";
        };

        sourceRoot = "source/nak_ffi";
        cargoLock.lockFile = ./libs/nak_ffi/Cargo.lock;

        installPhase = ''
          mkdir -p $out/lib
          cp target/release/libnak_ffi.so $out/lib/
        '';
      };

      # ── Portable Python runtime (bundled, not for compilation) ──
      portable-python = pkgs.fetchzip {
        url = "https://github.com/bjia56/portable-python/releases/download/cpython-v${portablePythonVersion}-build.0/python-headless-${portablePythonVersion}-linux-x86_64.zip";
        hash = "";  # nix will tell you the correct hash on first build
        stripRoot = false;
      };

      # ── Build Python with packages for compilation (pybind11, sip) ──
      buildPython = pkgs.python313.withPackages (ps: with ps; [
        pybind11
        sip
        psutil
      ]);

      # ── Build Fluorine from source ──
      fluorine-unwrapped = pkgs.stdenv.mkDerivation {
        pname = "fluorine-manager-unwrapped";
        inherit version;

        src = self;

        nativeBuildInputs = with pkgs; [
          cmake
          ninja
          pkg-config
          patchelf
          qt6.wrapQtAppsHook
        ];

        buildInputs = with pkgs; [
          # Qt 6
          qt6.qtbase
          qt6.qtwebengine
          qt6.qtwebsockets
          qt6.qtsvg
          qt6.qtwayland

          # Boost
          boost

          # Python (build-time)
          buildPython

          # System libraries
          sqlite
          tinyxml2
          spdlog
          fuse3
          lz4
          zlib
          zstd
          bzip2
          xz
          openssl
          curl
          tomlplusplus
          fontconfig
          freetype

          # libloot (built above)
          libloot
        ];

        # Disable Rust FFI builds in cmake — we built them separately.
        cmakeFlags = [
          "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
          "-DPython_EXECUTABLE=${buildPython}/bin/python3"
          "-Dpybind11_DIR=${buildPython}/lib/python${pythonVersion}/site-packages/pybind11/share/cmake/pybind11"
          "-DBUILD_PLUGIN_PYTHON=ON"
          "-DBUILD_NAK_FFI=OFF"
          "-DBUILD_BSA_FFI=OFF"
        ];

        # Replicate the staging logic from docker/build-inner.sh.
        installPhase = ''
          runHook preInstall

          local RUNDIR=src/src
          local PY_MM="${pythonVersion}"

          mkdir -p $out/opt/fluorine-manager/{plugins,dlls,lib,qt6plugins}

          # ── Main binary + helpers ──
          cp -f $RUNDIR/ModOrganizer $out/opt/fluorine-manager/ModOrganizer-core
          [ -f libs/lootcli/src/lootcli ] && cp -f libs/lootcli/src/lootcli $out/opt/fluorine-manager/

          # ── MO2 plugins (.so) ──
          find libs -type f \( \
            -name "libgame_*.so" -o \
            -name "libinstaller_*.so" -o \
            -name "libfomod_plus_*.so" -o \
            -name "libpreview_*.so" -o \
            -name "libdiagnose_*.so" -o \
            -name "libcheck_*.so" -o \
            -name "libtool_*.so" -o \
            -name "libinieditor.so" -o \
            -name "libinibakery.so" -o \
            -name "libbsa_extractor.so" -o \
            -name "libbsa_packer.so" -o \
            -name "libproxy.so" \
          \) -exec cp -f {} $out/opt/fluorine-manager/plugins/ \;

          # Python plugin payload.
          for f in libplugin_python.so lzokay.py winreg.py pyCfg.py \
                   DDSPreview.py Form43Checker.py ScriptExtenderPluginChecker.py; do
            [ -f "src/src/plugins/$f" ] && cp -f "src/src/plugins/$f" $out/opt/fluorine-manager/plugins/
          done
          for d in basic_games data libs dlls; do
            [ -d "src/src/plugins/$d" ] && cp -a "src/src/plugins/$d" $out/opt/fluorine-manager/plugins/
          done
          rm -f $out/opt/fluorine-manager/plugins/FNIS*.py

          # Source-tree Python plugins.
          for f in ../src/plugins/*.py; do
            [ -f "$f" ] && cp -f "$f" $out/opt/fluorine-manager/plugins/
          done

          # ── Stylesheets ──
          [ -d src/src/stylesheets ] && cp -a src/src/stylesheets $out/opt/fluorine-manager/

          # ── 7z runtime ──
          [ -f src/src/dlls/7z.so ] && cp -f src/src/dlls/7z.so $out/opt/fluorine-manager/dlls/

          # ── Project libraries ──
          cp -f libs/uibase/src/libuibase.so $out/opt/fluorine-manager/lib/
          cp -f libs/libbsarch/liblibbsarch.so $out/opt/fluorine-manager/lib/
          cp -f libs/archive/src/libarchive.so $out/opt/fluorine-manager/lib/
          cp -f libs/plugin_python/src/runner/librunner.so $out/opt/fluorine-manager/lib/

          # ── Pre-built Rust FFI libraries ──
          cp -f ${bsa-ffi}/lib/libbsa_ffi.so $out/opt/fluorine-manager/lib/
          cp -f ${nak-ffi}/lib/libnak_ffi.so $out/opt/fluorine-manager/lib/

          # ── libloot ──
          cp -Lf ${libloot}/lib/libloot.so* $out/opt/fluorine-manager/lib/ 2>/dev/null || true

          # ── Boost (from nix store) ──
          for boost_lib in ${pkgs.boost}/lib/libboost_program_options.so* \
                           ${pkgs.boost}/lib/libboost_thread.so*; do
            [ -f "$boost_lib" ] && cp -Lf "$boost_lib" $out/opt/fluorine-manager/lib/
          done

          # ── Qt6 plugins ──
          QT6_PLUGIN_DIR="${pkgs.qt6.qtbase}/lib/qt-6/plugins"
          for plugin_type in platforms tls networkinformation styles \
                            imageformats iconengines xcbglintegrations \
                            egldeviceintegrations; do
            [ -d "$QT6_PLUGIN_DIR/$plugin_type" ] && \
              cp -a "$QT6_PLUGIN_DIR/$plugin_type" $out/opt/fluorine-manager/qt6plugins/
          done
          # Wayland plugins.
          WAYLAND_PLUGIN_DIR="${pkgs.qt6.qtwayland}/lib/qt-6/plugins"
          for plugin_type in wayland-shell-integration \
                            wayland-decoration-client wayland-graphics-integration-client; do
            [ -d "$WAYLAND_PLUGIN_DIR/$plugin_type" ] && \
              cp -a "$WAYLAND_PLUGIN_DIR/$plugin_type" $out/opt/fluorine-manager/qt6plugins/
          done
          # SVG plugin.
          SVG_PLUGIN_DIR="${pkgs.qt6.qtsvg}/lib/qt-6/plugins"
          [ -d "$SVG_PLUGIN_DIR/imageformats" ] && \
            cp -a "$SVG_PLUGIN_DIR/imageformats"/. $out/opt/fluorine-manager/qt6plugins/imageformats/

          # ── Bundle shared library dependencies ──
          SKIP_PATTERN="linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|librt\.so|libpthread|libresolv|libnss|libgcc_s|libstdc\+\+"
          SKIP_PATTERN="$SKIP_PATTERN|libGL\.so|libEGL|libGLX|libGLdispatch|libdrm|libvulkan|libX11|libxcb|libwayland|libxkbcommon"
          SKIP_PATTERN="$SKIP_PATTERN|libpython"

          collect_and_bundle() {
            ldd "$1" 2>/dev/null | grep "=>" | awk '{print $3}' | grep "^/" | while read -r dep; do
              dep_name="$(basename "$dep")"
              echo "$dep_name" | grep -qE "$SKIP_PATTERN" && continue
              [ -f "$out/opt/fluorine-manager/lib/$dep_name" ] && continue
              cp -Lf "$dep" "$out/opt/fluorine-manager/lib/" 2>/dev/null || true
            done
          }
          collect_and_bundle "$out/opt/fluorine-manager/ModOrganizer-core"
          find "$out/opt/fluorine-manager/plugins" -name "*.so" -exec bash -c 'ldd "$1" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/" | while read dep; do
            dep_name="$(basename "$dep")"
            echo "$dep_name" | grep -qE "'"$SKIP_PATTERN"'" && continue
            [ -f "'"$out"'/opt/fluorine-manager/lib/$dep_name" ] && continue
            cp -Lf "$dep" "'"$out"'/opt/fluorine-manager/lib/" 2>/dev/null || true
          done' _ {} \;
          find "$out/opt/fluorine-manager/lib" -name "*.so*" -exec bash -c 'ldd "$1" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/" | while read dep; do
            dep_name="$(basename "$dep")"
            echo "$dep_name" | grep -qE "'"$SKIP_PATTERN"'" && continue
            [ -f "'"$out"'/opt/fluorine-manager/lib/$dep_name" ] && continue
            cp -Lf "$dep" "'"$out"'/opt/fluorine-manager/lib/" 2>/dev/null || true
          done' _ {} \;
          [ -f "$out/opt/fluorine-manager/lootcli" ] && collect_and_bundle "$out/opt/fluorine-manager/lootcli"
          rm -f "$out/opt/fluorine-manager/lib"/libpython*.so* 2>/dev/null || true

          # ── Portable Python runtime ──
          PP_SRC=$(echo ${portable-python}/python-headless-*)
          if [ ! -d "$PP_SRC" ]; then
            PP_SRC="${portable-python}"
          fi
          cp -a "$PP_SRC" $out/opt/fluorine-manager/python
          chmod -R u+w $out/opt/fluorine-manager/python

          # Trim portable Python.
          PP_STDLIB="$out/opt/fluorine-manager/python/lib/python$PY_MM"
          rm -rf "$PP_STDLIB/test" "$PP_STDLIB/idlelib" "$PP_STDLIB/tkinter" \
                 "$PP_STDLIB/turtledemo" "$out/opt/fluorine-manager/python/include" \
                 "$out/opt/fluorine-manager/python/share" 2>/dev/null || true
          find "$out/opt/fluorine-manager/python" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true

          # Patch portable Python SONAME.
          PP_LIBPY="$out/opt/fluorine-manager/python/lib/libpython$PY_MM.so"
          if [ -f "$PP_LIBPY" ]; then
            CURRENT_SONAME="$(readelf -d "$PP_LIBPY" 2>/dev/null | grep SONAME | sed 's/.*\[//' | sed 's/\]//')"
            if [ "$CURRENT_SONAME" = "libpython$PY_MM.so" ]; then
              patchelf --set-soname "libpython$PY_MM.so.1.0" "$PP_LIBPY"
              mv "$PP_LIBPY" "$PP_LIBPY.1.0"
              ln -sf "libpython$PY_MM.so.1.0" "$PP_LIBPY"
            fi
            [ ! -e "$PP_LIBPY.1.0" ] && ln -sf "libpython$PY_MM.so" "$PP_LIBPY.1.0"
          fi

          # Bundle PyQt6 into portable Python site-packages.
          PYSITE="$out/opt/fluorine-manager/python/lib/python$PY_MM/site-packages"
          mkdir -p "$PYSITE"
          for search_dir in ${pkgs.python313Packages.pyqt6}/lib/python${pythonVersion}/site-packages; do
            if [ -d "$search_dir/PyQt6" ]; then
              cp -a "$search_dir/PyQt6" "$PYSITE/"
              [ -d "$search_dir/PyQt6_sip" ] && cp -a "$search_dir/PyQt6_sip" "$PYSITE/"
              [ -d "$search_dir/sip" ] && cp -a "$search_dir/sip" "$PYSITE/"
              break
            fi
          done

          # Build-tree Python plugin payload.
          [ -d src/src/python ] && cp -a src/src/python/. $out/opt/fluorine-manager/python/

          # ── icoutils (for .exe icon extraction) ──
          cp -f ${pkgs.icoutils}/bin/wrestool $out/opt/fluorine-manager/ 2>/dev/null || true
          cp -f ${pkgs.icoutils}/bin/icotool $out/opt/fluorine-manager/ 2>/dev/null || true

          # ── Strip binaries ──
          strip --strip-unneeded $out/opt/fluorine-manager/ModOrganizer-core 2>/dev/null || true
          find $out/opt/fluorine-manager/plugins -name "*.so" -exec strip --strip-unneeded {} \; 2>/dev/null || true
          find $out/opt/fluorine-manager/lib -name "*.so" -exec strip --strip-unneeded {} \; 2>/dev/null || true
          [ -f "$out/opt/fluorine-manager/lootcli" ] && strip --strip-unneeded "$out/opt/fluorine-manager/lootcli" 2>/dev/null || true

          # ── Patch RPATH ──
          patchelf --force-rpath --set-rpath '$ORIGIN/lib:$ORIGIN/python/lib' \
            $out/opt/fluorine-manager/ModOrganizer-core
          [ -f "$out/opt/fluorine-manager/lootcli" ] && \
            patchelf --force-rpath --set-rpath '$ORIGIN/lib' $out/opt/fluorine-manager/lootcli
          find $out/opt/fluorine-manager/plugins -name "*.so" \
            -exec patchelf --force-rpath --set-rpath '$ORIGIN/../lib:$ORIGIN/../python/lib' {} \; 2>/dev/null || true
          find $out/opt/fluorine-manager/lib -name "*.so" \
            -exec patchelf --force-rpath --set-rpath '$ORIGIN:$ORIGIN/../python/lib' {} \; 2>/dev/null || true

          # ── Launcher script ──
          cat > $out/opt/fluorine-manager/fluorine-manager <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
SELF="$(readlink -f "$0")"
HERE="$(cd "$(dirname "$SELF")" && pwd)"

export FLUORINE_ORIG_LD_LIBRARY_PATH="''${LD_LIBRARY_PATH:-}"
export FLUORINE_ORIG_PATH="''${PATH}"
export FLUORINE_ORIG_XDG_DATA_DIRS="''${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export FLUORINE_ORIG_QT_PLUGIN_PATH="''${QT_PLUGIN_PATH:-}"

# Sync entire app to ~/.local/share/fluorine/bin/ so instance plugin
# symlinks always point to a stable location.
FLUORINE_DATA="''${HOME}/.local/share/fluorine"
BIN_DST="''${FLUORINE_DATA}/bin"

CURRENT_VER="$(stat -c '%i:%Y' "''${HERE}/ModOrganizer-core" 2>/dev/null || echo "unknown")"
MARKER="''${BIN_DST}/.version"

if [ ! -f "''${MARKER}" ] || [ "$(cat "''${MARKER}" 2>/dev/null)" != "''${CURRENT_VER}" ]; then
  echo "Syncing Fluorine to ''${BIN_DST}..." >&2
  rm -rf "''${BIN_DST}"
  mkdir -p "''${BIN_DST}"
  (cd "''${HERE}" && tar --exclude-vcs -cf - .) | (cd "''${BIN_DST}" && tar -xf -)
  echo "''${CURRENT_VER}" > "''${MARKER}"
fi

RUN="''${BIN_DST}"
PYTHON_DIR="''${RUN}/python"

export PATH="''${RUN}:''${PATH}"
export LD_LIBRARY_PATH="''${RUN}/lib:''${PYTHON_DIR}/lib:''${LD_LIBRARY_PATH:-}"
export MO2_BASE_DIR="''${RUN}"
export MO2_PLUGINS_DIR="''${RUN}/plugins"
export MO2_DLLS_DIR="''${RUN}/dlls"
export MO2_PYTHON_DIR="''${PYTHON_DIR}"
MO2_PYTHONHOME="''${PYTHON_DIR}"
unset PYTHONPATH PYTHONNOUSERSITE PYTHONHOME

export QT_PLUGIN_PATH="''${RUN}/qt6plugins"

cd "''${RUN}"
exec env PYTHONHOME="''${MO2_PYTHONHOME}" "''${RUN}/ModOrganizer-core" "$@"
LAUNCH
          chmod +x $out/opt/fluorine-manager/fluorine-manager

          # ── Desktop integration files ──
          cp -f ../data/com.fluorine.manager.desktop $out/opt/fluorine-manager/ 2>/dev/null || true
          cp -f ../data/com.fluorine.manager.png $out/opt/fluorine-manager/ 2>/dev/null || true
          cp -f ../data/com.fluorine.manager.metainfo.xml $out/opt/fluorine-manager/ 2>/dev/null || true

          mkdir -p $out/share/icons/hicolor/256x256/apps
          mkdir -p $out/share/metainfo
          cp $out/opt/fluorine-manager/com.fluorine.manager.png \
             $out/share/icons/hicolor/256x256/apps/ 2>/dev/null || true
          cp $out/opt/fluorine-manager/com.fluorine.manager.metainfo.xml \
             $out/share/metainfo/ 2>/dev/null || true

          runHook postInstall
        '';
      };

      desktopItem = pkgs.makeDesktopItem {
        name = "com.fluorine.manager";
        exec = "fluorine-manager";
        icon = "com.fluorine.manager";
        desktopName = "Fluorine Manager";
        genericName = "Mod Manager";
        comment = "Mod Organizer 2 for Linux — manage your game mods";
        categories = [ "Game" "Utility" ];
        mimeTypes = [ "x-scheme-handler/nxm" ];
        startupWMClass = "ModOrganizer";
        keywords = [ "mod" "organizer" "modding" "skyrim" "fallout" ];
      };

      # ── FHS wrapper ──
      fluorine-manager = pkgs.buildFHSEnv {
        pname = "fluorine-manager";
        inherit version;

        runScript = "${fluorine-unwrapped}/opt/fluorine-manager/fluorine-manager";

        targetPkgs = pkgs: with pkgs; [
          glibc
          stdenv.cc.cc.lib

          libGL
          libglvnd
          libdrm
          vulkan-loader
          mesa

          xorg.libX11
          xorg.libxcb
          xorg.libXext
          xorg.libXrandr
          xorg.libXcursor
          xorg.libXi
          xorg.libXfixes
          xorg.libXrender
          xorg.libXcomposite
          xorg.libXdamage
          xorg.libXtst
          xorg.libXScrnSaver
          xorg.libXinerama
          libxkbcommon

          wayland

          fuse3

          fontconfig
          freetype

          nss
          nspr

          zlib
          dbus
          glib
          udev
          libcap
          polkit
        ];

        multiPkgs = pkgs: with pkgs; [
          glibc
          libGL
          libglvnd
          vulkan-loader
          libdrm
          udev
          alsa-lib
          libpulseaudio
          dbus
          freetype
          fontconfig
          zlib
          glib
          xorg.libX11
          xorg.libxcb
          xorg.libXext
          xorg.libXrandr
          xorg.libXcursor
          xorg.libXi
          xorg.libXfixes
          xorg.libXrender
          xorg.libXcomposite
          xorg.libXdamage
          xorg.libXtst
          xorg.libXinerama
          wayland
          libxkbcommon
          gnutls
          SDL2
          ncurses
          cups
        ];

        multiArch = true;

        unshareIpc = false;
        unsharePid = false;

        extraInstallCommands = ''
          mkdir -p $out/share
          ln -s ${desktopItem}/share/applications $out/share/applications
          ln -s ${fluorine-unwrapped}/share/icons $out/share/icons
          ln -s ${fluorine-unwrapped}/share/metainfo $out/share/metainfo
        '';

        meta = with pkgs.lib; {
          description = "Mod Organizer 2 for Linux";
          homepage = "https://github.com/SulfurNitride/Fluorine-Manager";
          license = licenses.gpl3;
          platforms = [ "x86_64-linux" ];
          mainProgram = "fluorine-manager";
        };
      };

    in {
      packages.${system} = {
        default = fluorine-manager;
        inherit fluorine-manager fluorine-unwrapped libloot bsa-ffi nak-ffi;
      };

      apps.${system}.default = {
        type = "app";
        program = "${fluorine-manager}/bin/fluorine-manager";
      };

      # NixOS module for system-wide installation.
      nixosModules.default = { config, lib, ... }: {
        options.programs.fluorine-manager = {
          enable = lib.mkEnableOption "Fluorine Manager (Mod Organizer 2 for Linux)";

          fusePassthrough = lib.mkOption {
            type = lib.types.bool;
            default = false;
            description = ''
              Enable FUSE passthrough (kernel-direct I/O for mod files).
              Grants CAP_SYS_ADMIN to the Fluorine binary via a
              security wrapper.  Requires kernel 6.9+.
            '';
          };
        };

        config = lib.mkIf config.programs.fluorine-manager.enable {
          environment.systemPackages = [ self.packages.${system}.default ];

          programs.fuse.userAllowOther = true;

          security.wrappers = lib.mkIf config.programs.fluorine-manager.fusePassthrough {
            fluorine-manager = {
              source = "${self.packages.${system}.default}/bin/fluorine-manager";
              capabilities = "cap_sys_admin+ep";
              owner = "root";
              group = "root";
            };
          };
        };
      };
    };
}
