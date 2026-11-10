#!/bin/sh
# MIPS is a Rust tier-3 target: build std from the pinned rust-src component.
set -eu

source_dir=${1:?source directory required}
jobs=${2:-2}
toolchain=${QUARK_RUST_TOOLCHAIN:-1.94.0}
cc=${QUARK_CC:?QUARK_CC must name the MIPS musl C compiler}
ar=${QUARK_AR:?QUARK_AR must name the MIPS archiver}
case "$("$cc" -dumpmachine)" in
    mipsel*musl*) ;;
    *) echo 'quarkdrive-webdav requires a MIPS little-endian musl toolchain' >&2; exit 1 ;;
esac
sysroot=$(rustup run "$toolchain" rustc --print sysroot)
test -f "$sysroot/lib/rustlib/src/rust/library/Cargo.toml" || {
    echo "Install prerequisites: rustup toolchain install $toolchain --component rust-src" >&2
    exit 1
}
export RUSTC_BOOTSTRAP=1
export CC_mipsel_unknown_linux_musl="$cc"
export AR_mipsel_unknown_linux_musl="$ar"
export CARGO_TARGET_MIPSEL_UNKNOWN_LINUX_MUSL_LINKER="$cc"
export CARGO_TARGET_MIPSEL_UNKNOWN_LINUX_MUSL_RUSTFLAGS='-C target-cpu=mips32r2 -C target-feature=-crt-static'
export CARGO_PROFILE_RELEASE_PANIC=abort CARGO_PROFILE_RELEASE_STRIP=symbols
export CARGO_NET_RETRY=5 CARGO_HTTP_TIMEOUT=60 CARGO_HTTP_MULTIPLEXING=false
cd "$source_dir"
rustup run "$toolchain" cargo -Z build-std=std,panic_abort build \
    --locked --release --target mipsel-unknown-linux-musl -j "$jobs"
