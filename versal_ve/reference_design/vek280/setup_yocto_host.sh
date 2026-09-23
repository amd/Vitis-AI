#!/usr/bin/env bash
# ===========================================================
# Copyright 2026 Advanced Micro Devices Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# ===========================================================
#
# check_host_prereqs.sh — VEK280 EDF (Yocto 2026.1) build-host prerequisite check + install
#
# One pass: checks everything the bitbake EDF build (+ Vitis/WIC steps) needs on a
# Debian/Ubuntu host and installs whatever is missing (via sudo apt-get). Designed so a
# FRESH machine can run this once and then build.
#
#   ./check_host_prereqs.sh        # run as a user with sudo rights
#   sudo ./check_host_prereqs.sh   # or run the whole script as root
#
# Sources of the lists (version-exact for our 2026.1 tree):
#   - sources/poky/meta/classes-global/sanity.bbclass : SANITY_REQUIRED_UTILITIES
#   - sources/poky/meta/conf/bitbake.conf             : HOSTTOOLS (chrpath, diffstat, lz4c)
#   - Yocto Project "Required Packages for the Build Host (Ubuntu/Debian)" : python3-*, libacl1, ...
#   - AMD-EDF CI Dockerfile                           : WIC + recipe extras
#
# NOTE: Vivado/Vitis *system* libs (libtinfo5, X11/GTK, lib32*) are NOT handled here —
#       they are Vivado-install prerequisites (see AMD UG973) and are OS-version-specific
#       (libtinfo5 is not in Ubuntu 24.04; the AMD CI uses Ubuntu 22.04). Install Vivado/
#       Vitis per AMD's documented prerequisites separately.
#
# Exit: 0 = all REQUIRED present/installed ; 2 = a REQUIRED item could not be satisfied
#
set -u
export PATH="$PATH:/sbin:/usr/sbin"   # so mkfs.*/parted aren't false-flagged

# --- binary tools (checked with command -v) : "command|apt-package|class" -----------
TOOLS=(
  # REQUIRED (bitbake sanity / HOSTTOOLS)
  "chrpath|chrpath|required"      "diffstat|diffstat|required"   "lz4c|lz4|required"
  "patch|patch|required"          "git|git|required"             "bzip2|bzip2|required"
  "tar|tar|required"              "gzip|gzip|required"           "gawk|gawk|required"
  "wget|wget|required"            "cpio|cpio|required"           "perl|perl|required"
  "file|file|required"            "which|debianutils|required"   "gcc|build-essential|required"
  "g++|build-essential|required"  "make|build-essential|required" "python3|python3|required"
  "xz|xz-utils|required"          "unzip|unzip|required"
  # RECOMMENDED (EDF / WIC / recipes)
  "lz4|lz4|recommended"           "zstd|zstd|recommended"        "socat|socat|recommended"
  "xmllint|libxml2-utils|recommended" "expect|expect|recommended" "mtools|mtools|recommended"
  "mkfs.fat|dosfstools|recommended"   "mkfs.ext4|e2fsprogs|recommended" "parted|parted|recommended"
  "cmake|cmake|recommended"       "rsync|rsync|recommended"      "makeinfo|texinfo|recommended"
  "bison|bison|recommended"       "flex|flex|recommended"        "autoconf|autoconf|recommended"
  "automake|automake|recommended" "m4|m4|recommended"            "meson|meson|recommended"
  "ninja|ninja-build|recommended" "pkg-config|pkg-config|recommended" "git-lfs|git-lfs|recommended"
  "xsltproc|xsltproc|recommended" "gdisk|gdisk|recommended"      "mksquashfs|squashfs-tools|recommended"
  "bmaptool|bmap-tools|required"  "kpartx|kpartx|optional"
)

# --- library / python packages (checked with dpkg) : "apt-package|class" ------------
# (no usable binary to command-check; needed on a fresh machine)
PKGS=(
  "python3-jinja2|required"  "python3-pexpect|required" "python3-git|required"
  "python3-subunit|required" "python3-pip|required"     "python3-setuptools|recommended"
  "libacl1|required"         "locales|required"         "iputils-ping|recommended"
  "libtool|recommended"      "libxml-parser-perl|recommended" "tcl|recommended"
)

pkg_installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }

echo "==================================================================="
echo " VEK280 EDF build-host prerequisite check + install"
echo "==================================================================="

declare -A want_class           # pkg -> highest class seen (required wins)
missing_pkgs=()
add_missing() { # $1 pkg, $2 class
  local p="$1" c="$2"
  if [ "${want_class[$p]:-}" != "required" ]; then want_class[$p]="$c"; fi
  case " ${missing_pkgs[*]} " in *" $p "*) ;; *) missing_pkgs+=("$p");; esac
}

echo "-- binary tools ---------------------------------------------------"
for e in "${TOOLS[@]}"; do
  IFS='|' read -r cmd pkg cls <<< "$e"
  if command -v "$cmd" >/dev/null 2>&1; then printf "  [ OK ]  %-12s\n" "$cmd"
  else printf "  [MISS]  %-12s -> %s (%s)\n" "$cmd" "$pkg" "$cls"; add_missing "$pkg" "$cls"; fi
done
echo "-- library / python packages (dpkg) ------------------------------"
for e in "${PKGS[@]}"; do
  IFS='|' read -r pkg cls <<< "$e"
  if pkg_installed "$pkg"; then printf "  [ OK ]  %-20s\n" "$pkg"
  else printf "  [MISS]  %-20s (%s)\n" "$pkg" "$cls"; add_missing "$pkg" "$cls"; fi
done

# UTF-8 locale — bitbake aborts without one ("locale setting which supports UTF-8")
need_locale=0
echo "-- locale ---------------------------------------------------------"
if locale -a 2>/dev/null | grep -qiE "utf-?8"; then
  printf "  [ OK ]  UTF-8 locale present\n"
else
  printf "  [MISS]  no UTF-8 locale (bitbake requires one)\n"; need_locale=1
fi

echo "-------------------------------------------------------------------"
if [ ${#missing_pkgs[@]} -eq 0 ] && [ "$need_locale" -eq 0 ]; then
  echo "All prerequisites present. Nothing to install."
  echo "(Reminder: Vivado/Vitis system libs are separate — see AMD UG973 / use Ubuntu 22.04.)"
  exit 0
fi
[ ${#missing_pkgs[@]} -gt 0 ] && echo "Missing packages: ${missing_pkgs[*]}"
[ "$need_locale" -eq 1 ] && echo "Action needed: generate a UTF-8 locale (en_US.UTF-8)"

# privilege
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then SUDO="sudo"
  else echo "ERROR: not root and no sudo. Install manually:"; echo "    apt-get install -y ${missing_pkgs[*]}"; exit 2; fi
fi
command -v apt-get >/dev/null 2>&1 || { echo "ERROR: apt-get not found (Debian/Ubuntu only). Install: ${missing_pkgs[*]}"; exit 2; }

echo "-------------------------------------------------------------------"
if [ ${#missing_pkgs[@]} -gt 0 ]; then
  echo "Installing with ${SUDO:-root} apt-get ..."
  $SUDO apt-get update
  # batch first; if it fails (e.g. one unavailable pkg), retry per-package so the rest still install
  if ! $SUDO apt-get install -y "${missing_pkgs[@]}"; then
    echo "Batch install hit an issue — retrying per package..."
    for p in "${missing_pkgs[@]}"; do
      $SUDO apt-get install -y "$p" || echo "  WARN: could not install '$p' (${want_class[$p]:-?})"
    done
  fi
  # HOSTTOOLS wants lz4c; modern Ubuntu lz4 pkg may ship only lz4 -> symlink
  if ! command -v lz4c >/dev/null 2>&1 && command -v lz4 >/dev/null 2>&1; then
    $SUDO ln -sf "$(command -v lz4)" /usr/local/bin/lz4c && echo "Created lz4c -> lz4 symlink."
  fi
fi

# UTF-8 locale generation (bitbake requirement)
if [ "$need_locale" -eq 1 ]; then
  echo "Generating en_US.UTF-8 locale ..."
  if $SUDO locale-gen en_US.UTF-8 && $SUDO update-locale LANG=en_US.UTF-8; then
    echo "Locale generated. Add to your shell rc:  export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8"
  else
    echo "  WARN: locale-gen failed — generate a UTF-8 locale manually."
  fi
fi

echo "-------------------------------------------------------------------"
echo "Re-checking REQUIRED items ..."
fail=0
for e in "${TOOLS[@]}"; do
  IFS='|' read -r cmd pkg cls <<< "$e"
  [ "$cls" = "required" ] || continue
  command -v "$cmd" >/dev/null 2>&1 || { echo "  [STILL MISSING] $cmd ($pkg)"; fail=1; }
done
for e in "${PKGS[@]}"; do
  IFS='|' read -r pkg cls <<< "$e"
  [ "$cls" = "required" ] || continue
  pkg_installed "$pkg" || { echo "  [STILL MISSING] $pkg"; fail=1; }
done
locale -a 2>/dev/null | grep -qiE "utf-?8" || { echo "  [STILL MISSING] UTF-8 locale"; fail=1; }
if [ "$fail" -eq 0 ]; then
  echo "All REQUIRED prerequisites satisfied."
  echo "(Reminder: Vivado/Vitis system libs are separate — AMD UG973 / Ubuntu 22.04.)"
  exit 0
else
  echo "Some REQUIRED prerequisites are still missing (see above)."; exit 2
fi
