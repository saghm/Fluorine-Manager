#!/usr/bin/env bash
set -euo pipefail
source_dir="${1:?FAudio source directory required}"
output_dir="${2:?Test output directory required}"
script_dir="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "${output_dir}"
read -ra sdl_cflags <<< "$(pkg-config --cflags sdl2)"
read -ra sdl_libs <<< "$(pkg-config --libs sdl2)"
for optimization in 0 2; do
    cc -std=c11 -O"${optimization}" -g -no-pie \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -ffunction-sections -fdata-sections \
        -DFAUDIO_DISABLE_DEBUGCONFIGURATION -DHAVE_WMADEC \
        -I"${source_dir}/include" -I"${source_dir}/src" "${sdl_cflags[@]}" \
        "${script_dir}/faudio-regression.c" "${source_dir}/src/FAudio.c" \
        -Wl,--gc-sections "${sdl_libs[@]}" -lm -o "${output_dir}/regression-O${optimization}"
    for probe in wma unaligned callback loopcallback padding offset; do
        ASAN_OPTIONS=detect_leaks=1 "${output_dir}/regression-O${optimization}" "${probe}"
    done
    cc -std=c11 -O"${optimization}" -g -no-pie \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -ffunction-sections -fdata-sections -DFAUDIO_DISABLE_DEBUGCONFIGURATION \
        -I"${source_dir}/include" -I"${source_dir}/src" "${sdl_cflags[@]}" \
        "${script_dir}/faudio-reverb-test.c" -Wl,--gc-sections \
        "${sdl_libs[@]}" -lm -o "${output_dir}/reverb-O${optimization}"
    ASAN_OPTIONS=detect_leaks=1 "${output_dir}/reverb-O${optimization}" all
    cc -std=c11 -O"${optimization}" -g -no-pie \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -ffunction-sections -fdata-sections -DFAUDIO_DISABLE_DEBUGCONFIGURATION \
        -I"${source_dir}/include" -I"${source_dir}/src" "${sdl_cflags[@]}" \
        "${script_dir}/faudio-mix-test.c" "${source_dir}/src/FAudio.c" \
        "${source_dir}/src/FAudio_internal_simd.c" -Wl,--gc-sections \
        "${sdl_libs[@]}" -lm -o "${output_dir}/mix-O${optimization}"
    for probe in source submix; do
        ASAN_OPTIONS=detect_leaks=1 "${output_dir}/mix-O${optimization}" "${probe}"
    done
done
