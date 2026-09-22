#!/bin/bash
#
# Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
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
# Description:
#   Fix incomplete aliases{} in the lopper-generated intermediate device tree
#   (CONFIG_DTFILE, e.g. sw/yocto/build/conf/dts/<machine>/cortexa78-linux.dts)
#   BEFORE Yocto/bitbake ever reads it.
#
#   This patches the plain-text .dts directly -- no dtc/bootgen round-trip is
#   needed here, since Yocto's device-tree.bb recipe will dtc-compile and
#   xilinx-bootbin/edf-ospi will package the corrected tree on their own first
#   pass. This avoids needing to patch/rebuild BOOT.bin or edf-ospi.bin after
#   the fact.
#
#   dtc/bootgen are NOT required on PATH for this script.
#
#   TEMPORARY WORKAROUND, not tied to any specific build.cfg flag:
#   gen-machineconf's parse-sdt step (via lopper) sometimes fails to
#   auto-populate this board's PS aliases -- confirmed to reproduce with
#   MIPI=1 (large PL overlay), a known gen-machineconf/lopper
#   limitation. This script does NOT
#   check MIPI/NPU_FW or any other flag -- it only ever compares the tree's
#   *actual current* aliases{} content against the known-correct table below
#   and fills in whatever is genuinely missing. That makes it correct and
#   safe to run unconditionally on every build regardless of flag
#   combination: a build where lopper worked correctly (e.g. MIPI=0) finds
#   nothing to add and is a harmless no-op, and it will automatically become
#   a permanent no-op everywhere, with no code changes needed, once
#   the underlying issue is fixed upstream in gen-machineconf/lopper.
#

set -euo pipefail

###############################################################################
# Helpers
###############################################################################

log()  { echo "[fix-lopper-dts] $*"; }
die()  { echo "[fix-lopper-dts] ERROR: $*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: $(basename "$0") --dts-file FILE

Fix the incomplete aliases{} block in a lopper-generated intermediate device
tree (e.g. sw/yocto/build/conf/dts/<machine>/cortexa78-linux.dts), in place,
before Yocto/bitbake reads it.

Required arguments:
  -f, --dts-file  FILE   The plain-text .dts file to patch in place

Optional arguments:
  -h, --help              Show this help and exit
EOF
}

###############################################################################
# Known-correct aliases for this board (same table as fix_bootbin_aliases.sh).
#
# This is board-level, generic PS peripheral info -- NOT specific to MIPI=1
# or any other flag. Every target path below is a standard PS peripheral
# (versal2.dtsi, the chip-generic devicetree) present identically regardless
# of PL/MIPI/NPU_FW content. The table itself was derived by reference to a
# build where lopper generates aliases correctly (MIPI=0), i.e. it encodes
# "what this board's aliases{} should look like," not "what MIPI=1 needs."
# It is applied as a corrective patch wherever lopper fails to produce it.
###############################################################################

EXPECTED_ALIASES="\
serial1 /axi/serial@f1930000
serial2 /axi/coresight@f0800000
mmc0 /axi/mmc@f1040000
mmc1 /axi/mmc@f1050000
i2c1 /axi/i2c@f1950000
i2c2 /axi/i2c@f1960000
i2c3 /axi/i2c@f1970000
i2c4 /axi/i2c@f1980000
i2c5 /axi/i2c@f1990000
i2c6 /axi/i2c@f19a0000
i2c7 /axi/i2c@f19b0000
i2c8 /axi/i2c@f1000000
spi0 /axi/spi@f1010000
spi1 /axi/spi@f1030000
spi2 /axi/spi@f19c0000
spi3 /axi/spi@f19d0000
nvmem1 /axi/i2c@f1950000/i2c-arbitrator@72/i2c-arb/eeprom@54"

###############################################################################
# Argument parsing
###############################################################################

DTS_FILE=""

while [ $# -gt 0 ]; do
    case "$1" in
        -f|--dts-file) DTS_FILE="$2"; shift 2 ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

[ -n "${DTS_FILE}" ] || { echo "Missing required argument: --dts-file" >&2; usage; exit 2; }
[ -f "${DTS_FILE}" ] || die "dts file not found: ${DTS_FILE}"

TIMESTAMP="$(date +%Y%m%d%H%M%S)"
WORK_DIR="$(mktemp -d /tmp/fix_lopper_dts.XXXXXX)"
trap 'rm -rf "${WORK_DIR}"' EXIT

NEW_DTS="${WORK_DIR}/patched.dts"

log "Patching aliases in: ${DTS_FILE}"

###############################################################################
# Merge missing aliases into the plain-text dts (no dtc/bootgen needed).
###############################################################################

merge_rc=0
EXPECTED_ALIASES="${EXPECTED_ALIASES}" \
python3 - "${DTS_FILE}" "${NEW_DTS}" <<'PYEOF' || merge_rc=$?
import os
import re
import sys

cur_path, out_path = sys.argv[1], sys.argv[2]


def read(path):
    with open(path, "r") as fh:
        return fh.read()


def node_paths(text):
    """Return the set of absolute node paths present in the dts."""
    paths = set()
    stack = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("/*") or line.startswith("*"):
            continue
        if line.endswith("{"):
            name = line[:-1].strip()
            if ":" in name:
                name = name.split(":", 1)[1].strip()
            stack.append(name)
            if name == "/":
                paths.add("/")
            else:
                parts = [p for p in stack if p != "/"]
                paths.add("/" + "/".join(parts))
        elif line == "};" or line.endswith("};"):
            if stack:
                stack.pop()
    return paths


ALIAS_RE = re.compile(r'^\s*([\w-]+)\s*=\s*"([^"]+)"\s*;')

# NOTE: indentation-agnostic (\s*, not \t) -- unlike a dtc-decompiled dts
# (which always uses tabs), lopper's own dts output uses spaces.
ALIASES_OPEN_RE = re.compile(r'^\s*aliases\s*{')
ALIASES_OPEN_LABELED_RE = re.compile(r'^\s*\w+:\s*aliases\s*{')


def aliases(text):
    """Return {alias_name: target_path} from the aliases{} node of a dts."""
    result = {}
    order = []
    in_aliases = False
    depth = 0
    for raw in text.splitlines():
        line = raw.strip()
        if not in_aliases:
            if ALIASES_OPEN_RE.match(line) or ALIASES_OPEN_LABELED_RE.match(line):
                in_aliases = True
                depth = 1
            continue
        depth += line.count("{")
        depth -= line.count("}")
        m = ALIAS_RE.match(line)
        if m:
            name, target = m.group(1), m.group(2)
            if name not in result:
                result[name] = target
                order.append(name)
        if depth <= 0:
            break
    return result, order


# Parse the embedded known-correct alias table (name -> path), preserving order.
expected = []
for row in os.environ.get("EXPECTED_ALIASES", "").splitlines():
    row = row.strip()
    if not row:
        continue
    name, target = row.split(None, 1)
    expected.append((name, target.strip()))

cur_text = read(cur_path)

cur_nodes = node_paths(cur_text)
cur_alias, cur_order = aliases(cur_text)

# Build the merged alias set. Preserve the expected ordering, then append any
# current-only aliases that the expected table does not know about.
merged = {}
merged_order = []
added = []
skipped_missing_node = []

for name, target in expected:
    if name in cur_alias:
        merged[name] = cur_alias[name]
        merged_order.append(name)
        continue
    if target in cur_nodes:
        merged[name] = target
        merged_order.append(name)
        added.append((name, target))
    else:
        skipped_missing_node.append((name, target))

for name in cur_order:
    if name not in merged:
        merged[name] = cur_alias[name]
        merged_order.append(name)

# Render the new aliases block (tabs are fine functionally -- dtc ignores
# whitespace -- this only affects the file's cosmetic style).
alias_lines = ["\taliases {"]
for name in merged_order:
    alias_lines.append('\t\t%s = "%s";' % (name, merged[name]))
alias_lines.append("\t};")
new_block = "\n".join(alias_lines)

# Replace the existing aliases{} block (indentation-agnostic match), or
# insert one right after the root node opening line "/ {" if none exists.
lines = cur_text.splitlines()
out_lines = []
i = 0
replaced = False
n = len(lines)
while i < n:
    line = lines[i]
    if ALIASES_OPEN_RE.match(line) or ALIASES_OPEN_LABELED_RE.match(line):
        depth = line.count("{") - line.count("}")
        i += 1
        while i < n and depth > 0:
            depth += lines[i].count("{")
            depth -= lines[i].count("}")
            i += 1
        out_lines.append(new_block)
        replaced = True
        continue
    out_lines.append(line)
    i += 1

if not replaced:
    tmp = []
    inserted = False
    for line in out_lines:
        tmp.append(line)
        if not inserted and re.match(r'^/\s*{', line.strip()):
            tmp.append(new_block)
            inserted = True
    out_lines = tmp

with open(out_path, "w") as fh:
    fh.write("\n".join(out_lines) + "\n")

if added:
    print("ADDED %d missing alias(es):" % len(added))
    for name, target in added:
        print("  + %-10s = %s" % (name, target))
else:
    print("No missing aliases to add (device tree already complete).")

if skipped_missing_node:
    print("Skipped %d expected alias(es) with no matching node in this dts:"
          % len(skipped_missing_node))
    for name, target in skipped_missing_node:
        print("  - %-10s = %s" % (name, target))

# Exit code 10 => nothing changed, so the caller can short-circuit.
sys.exit(0 if added else 10)
PYEOF

if [ "${merge_rc}" -eq 10 ]; then
    log "Aliases already complete; nothing to fix. Exiting."
    exit 0
elif [ "${merge_rc}" -ne 0 ]; then
    die "Alias merge step failed (rc=${merge_rc})"
fi

###############################################################################
# Back up and replace the original dts in place.
###############################################################################

cp -f "${DTS_FILE}" "${DTS_FILE}.bak.${TIMESTAMP}"
log "Backed up original dts -> ${DTS_FILE}.bak.${TIMESTAMP}"

cp -f "${NEW_DTS}" "${DTS_FILE}"
log "Patched dts in place: ${DTS_FILE}"

log "Resulting aliases{} block:"
awk '/aliases {/,/};/' "${DTS_FILE}" | head -30

log "Done. ${DTS_FILE} now contains a device tree with the complete aliases{}."
