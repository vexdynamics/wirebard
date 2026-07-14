#!/bin/sh
# wirebard installer — downloads the latest release for this machine's
# architecture, verifies its checksum, installs to /usr/local/bin.
#
#   curl -fsSL https://raw.githubusercontent.com/kristiand00/wirebard/main/install.sh | sh
#
# Hand-written infrastructure, like everything else around wirebard. POSIX sh,
# no dependencies beyond curl + tar + sha256sum.
set -eu

REPO="kristiand00/wirebard"
BIN_DIR="${WIREBARD_BIN_DIR:-/usr/local/bin}"

case "$(uname -s)" in
Linux) ;;
*)
    echo "error: wirebard releases are Linux-only (server tool); build from source elsewhere" >&2
    exit 1
    ;;
esac

case "$(uname -m)" in
x86_64 | amd64) arch="x86_64" ;;
aarch64 | arm64) arch="aarch64" ;;
armv7l) arch="armhf" ;;
armv6l | armv5*) arch="armel" ;; # Pi 1/Zero: soft-float build, runs anywhere
*)
    echo "error: unsupported architecture '$(uname -m)'" >&2
    exit 1
    ;;
esac

tag=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" |
    grep -m1 '"tag_name"' | cut -d'"' -f4)
[ -n "$tag" ] || {
    echo "error: could not determine the latest release" >&2
    exit 1
}

tarball="wirebard-$tag-linux-$arch.tar.gz"
base="https://github.com/$REPO/releases/download/$tag"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "downloading $tarball ($tag)..."
curl -fsSL -o "$tmp/$tarball" "$base/$tarball"
curl -fsSL -o "$tmp/SHA256SUMS" "$base/SHA256SUMS"

(cd "$tmp" && grep " $tarball\$" SHA256SUMS | sha256sum -c - >/dev/null)
echo "checksum OK"

tar -C "$tmp" -xzf "$tmp/$tarball"

if [ -w "$BIN_DIR" ]; then
    install -m 755 "$tmp/wirebard" "$BIN_DIR/wirebard"
else
    echo "installing to $BIN_DIR (needs sudo)..."
    sudo install -m 755 "$tmp/wirebard" "$BIN_DIR/wirebard"
fi

echo "installed: $("$BIN_DIR/wirebard" --version)"
