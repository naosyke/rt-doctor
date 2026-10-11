#!/bin/sh
# Installs the latest rt-doctor release for this machine:
#   curl -fsSL https://github.com/naosyke/rt-doctor/releases/latest/download/install.sh | sh
# Options (environment): PREFIX=/usr/local (binary goes to $PREFIX/bin),
# VERSION=v0.1.0 (default: latest), RTDOCTOR_BASE_URL (a mirror of the
# release files).
set -eu

repo=naosyke/rt-doctor
prefix=${PREFIX:-/usr/local}

[ "$(uname -s)" = Linux ] || { echo "rt-doctor runs on Linux only." >&2; exit 1; }
case "$(uname -m)" in
    x86_64 | amd64) arch=x86_64 ;;
    aarch64 | arm64) arch=arm64 ;;
    *) echo "No rt-doctor build for $(uname -m) (available: x86_64, arm64)." >&2; exit 1 ;;
esac

if [ -n "${RTDOCTOR_BASE_URL:-}" ]; then
    base=$RTDOCTOR_BASE_URL
elif [ -n "${VERSION:-}" ]; then
    base=https://github.com/$repo/releases/download/$VERSION
else
    base=https://github.com/$repo/releases/latest/download
fi
name=rtdoctor-linux-$arch

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fetch() {
    if command -v curl >/dev/null 2>&1; then curl -fsSL -o "$2" "$1"; else wget -qO "$2" "$1"; fi
}
echo "Downloading $name.tar.gz ..."
fetch "$base/$name.tar.gz" "$tmp/$name.tar.gz"
fetch "$base/SHA256SUMS" "$tmp/SHA256SUMS"
(cd "$tmp" && grep " $name.tar.gz\$" SHA256SUMS | sha256sum -c - >/dev/null) || {
    echo "Checksum mismatch for $name.tar.gz; not installing." >&2
    exit 1
}
tar -C "$tmp" -xzf "$tmp/$name.tar.gz"

sudo=""
if [ ! -w "$prefix/bin" ] && [ "$(id -u)" != 0 ]; then sudo=sudo; fi
$sudo mkdir -p "$prefix/bin"
$sudo install -m 755 "$tmp/$name/rtdoctor" "$prefix/bin/rtdoctor"
echo "Installed $("$prefix/bin/rtdoctor" --version) to $prefix/bin/rtdoctor"
echo "Try: sudo rtdoctor check"
