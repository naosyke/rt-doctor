#!/bin/sh
# Builds the static rtdoctor binary, runs the tests and packs a release
# archive: dist/rtdoctor-linux-<arch>.tar.gz. Used by the release workflow;
# also works locally in the rtdoctor-base container.
set -eu

arch=$(uname -m)
case "$arch" in
    x86_64) arch=x86_64 ;;
    aarch64 | arm64) arch=arm64 ;;
    *) echo "unsupported architecture: $arch" >&2; exit 1 ;;
esac

rm -rf build-release
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DRTDOCTOR_STATIC=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure

# The binary must not need anything from the target system.
if ldd build-release/rtdoctor >/dev/null 2>&1; then
    echo "build-release/rtdoctor is not static" >&2
    exit 1
fi

# A release tag must match the version compiled in (v0.1.0 <-> "rt-doctor 0.1.0").
version=$(build-release/rtdoctor --version | awk '{print $2}')
if [ -n "${RELEASE_TAG:-}" ] && [ "$RELEASE_TAG" != "v$version" ]; then
    echo "tag $RELEASE_TAG does not match version $version (CMakeLists.txt)" >&2
    exit 1
fi

name=rtdoctor-linux-$arch
rm -rf "dist/$name"
mkdir -p "dist/$name"
cp build-release/rtdoctor README.md "dist/$name/"
[ -f LICENSE ] && cp LICENSE "dist/$name/"
strip "dist/$name/rtdoctor"
tar -C dist -czf "dist/$name.tar.gz" "$name"
rm -rf "dist/$name"
echo "dist/$name.tar.gz (rt-doctor $version)"
