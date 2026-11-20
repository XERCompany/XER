#!/usr/bin/env bash
# bootstrap-msys2.sh — run ONCE inside the toolchain image build.
# Replicates what InstallTools.ps1 + ci.yml do on a fresh MSYS2:
#   1. reorder pacman mirrors (mirror.msys2.org stalls on cloud hosts)
#   2. init the keyring (fresh base has none -> signature checks fail)
#   3. full -Syu in two passes (pacman kills its own shell mid-update)
#   4. install the package list from /tmp/msys2-packages.txt
#   5. junction-fix embedded clang64 prefixes (see Fix-Msys2EmbeddedPrefixes
#      in InstallTools.ps1): packages embed the build prefix, so imported
#      .cmake targets can point at a path that does not exist here.
set -uo pipefail

MSYS2_ROOT="${MSYS2_ROOT:-/c/tools/msys64}"
PACKAGES_FILE="${PACKAGES_FILE:-/tmp/msys2-packages.txt}"

log() { echo "==> $*"; }

# ------------------------------------------------------------- mirrors
mirrors=(
  "https://repo.msys2.org"
  "https://mirrors.tuna.tsinghua.edu.cn/msys2"
  "https://mirrors.ustc.edu.cn/msys2"
  "https://mirror.clarkson.edu/msys2"
  "https://ftp.nluug.nl/pub/os/windows/msys2/builds"
  "https://download.nus.edu.sg/mirror/msys2"
  "https://mirror.yandex.ru/mirrors/msys2"
)
for f in /etc/pacman.d/mirrorlist.*; do
  [ -f "$f" ] || continue
  suffix=$(sed -n 's|^Server = https://mirror\.msys2\.org/\(\S*\)|\1|p' "$f" | head -n1)
  [ -n "$suffix" ] || suffix=$(sed -n 's|^Server = https://repo\.msys2\.org/\(\S*\)|\1|p' "$f" | head -n1)
  [ -n "$suffix" ] || continue
  {
    for m in "${mirrors[@]}"; do echo "Server = $m/$suffix"; done
    grep '^Server' "$f" || true
  } > "$f.new" && mv "$f.new" "$f"
done
log "mirrors reordered"

# ------------------------------------------------------------- keyring+db
pacman-key --init || true
pacman-key --populate msys2 || true
pacman -Syy --noconfirm

# --------------------------------------------------------- system update
# -Syu kills the calling msys shell when msys2-runtime is replaced, so the
# first pass may exit non-zero even on success — run it twice.
pacman -Syu --noconfirm || true
pacman -Syu --noconfirm
log "system updated"

# ------------------------------------------------------------- packages
mapfile -t packages < <(grep -v -e '^\s*#' -e '^\s*$' "$PACKAGES_FILE")
log "installing ${#packages[@]} packages"
for attempt in 1 2 3; do
  if pacman -S --noconfirm --needed "${packages[@]}"; then
    break
  fi
  log "pacman install attempt $attempt failed — refreshing db and retrying"
  pacman -Syy --noconfirm
  if [ "$attempt" = 3 ]; then
    echo "ERROR: pacman package install failed after 3 attempts" >&2
    exit 1
  fi
done

# ------------------------------------------------- embedded prefix fixup
# clang64 cmake files embed the prefix the packages were built under
# (e.g. D:/a/_temp/msys64). Create directory junctions at each broken
# embedded root pointing at the real MSYS2 root.
win_root=$(cygpath -w "$MSYS2_ROOT" | sed 's|\\|/|g')
declare -A embedded=()
while IFS= read -r f; do
  while IFS= read -r p; do
    embedded["$p"]=1
  done < <(grep -oE '[A-Za-z]:/[A-Za-z0-9_./-]+/clang64/' "$f" | sed 's|/clang64/$||')
done < <(find "$MSYS2_ROOT/clang64/lib/cmake" -name '*.cmake' 2>/dev/null)
for p in "${!embedded[@]}"; do
  [ "${p,,}" = "${win_root,,}" ] && continue
  wp=$(cygpath -w "$p")
  [ -d "$wp" ] && continue
  cmd /c "if not exist \"$(cygpath -w "$(dirname "$p")")\" mkdir \"$(cygpath -w "$(dirname "$p")")\" && mklink /J \"$wp\" \"$(cygpath -w "$MSYS2_ROOT")\"" >/dev/null \
    && log "junction $wp -> $(cygpath -w "$MSYS2_ROOT")" \
    || echo "WARN: junction $wp failed" >&2
done

clang64/bin/clang++ --version | head -1
log "MSYS2 bootstrap complete"
