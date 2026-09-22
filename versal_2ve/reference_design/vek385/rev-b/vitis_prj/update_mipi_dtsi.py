#!/usr/bin/env python3
"""
Update pl.dtsi with ISP pipeline integration changes.

This script applies the following transformations to pl.dtsi:
  1. Updates firmware-name to "x_plus_ml.pdi"
  2. Removes MIPI CSI2 RX subsystem nodes
  3. Removes FMC IIC nodes (mipi_rx_ss_hier_fmc_iic_*)
  4. Removes AXIS broadcaster nodes
  5. Removes standalone vcap_preproc_frmbuf_accel nodes
  6. Adds memory-region and rproc to visp_mbox_rpu nodes
  7. Adds memory-region to visp_ss nodes, removes port@0, adds port@2
  8. Adds memory-region, second DMA port, and port@1 to vcap_visp_ss nodes
  9. Updates preprocess_accel remote-endpoints

When --xsa is provided, the ISP-to-preprocess-accel and preprocess-to-frmbuf
mappings are extracted from the hardware handoff (bd.hwh) in the XSA archive
rather than relying on ordinal position.

Usage:
    python3 update_pl_dtsi.py <input.dtsi> [--xsa <path.xsa>] [-o <output.dtsi>]

Arguments:
    input.dtsi          Path to the input pl.dtsi file (required).
    --xsa               Path to the linked XSA file (for extracting HW
                        connections). If not provided, ordinal-based mapping
                        is used as fallback.
    -o, --output        Path for the output file (optional).
                        If not specified, the input file is modified in-place.
"""

import argparse
import re
import sys
import os
import zipfile
import xml.etree.ElementTree as ET


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def read_file(path):
    if not os.path.isfile(path):
        fatal(f"File not found: {path}")
    with open(path, "r") as fh:
        return fh.readlines()


def write_file(path, lines):
    with open(path, "w") as fh:
        fh.writelines(lines)


def fatal(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def find_brace_end(lines, start):
    """Given that lines[start] contains an opening '{', return the index of the
    line that contains the matching closing '}'.  Raises on mismatch."""
    depth = 0
    for i in range(start, len(lines)):
        depth += lines[i].count("{")
        depth -= lines[i].count("}")
        if depth == 0:
            return i
    fatal(f"Unmatched braces starting at line {start + 1}")


def find_top_level_nodes(lines, label_re):
    """Return a list of (start, end) line-index pairs for every *top-level*
    node (direct child of ``&fpga {}``) whose opening line matches *label_re*.

    *start* is the index of the first line of the node (the line with the
    label / node-name), and *end* is the index of the line containing the
    matching ``};``.
    """
    results = []
    depth = 0
    for i, line in enumerate(lines):
        opens = line.count("{")
        closes = line.count("}")
        if depth == 1 and opens > 0 and re.search(label_re, line):
            end = find_brace_end(lines, i)
            results.append((i, end))
        depth += opens - closes
    return results


def find_sub_nodes(lines, start, end, label_re):
    """Within lines[start..end], find sub-nodes matching *label_re*.
    Returns list of (sub_start, sub_end) pairs (absolute line indices).
    """
    results = []
    depth = 0
    base_depth = None
    for i in range(start, end + 1):
        opens = lines[i].count("{")
        closes = lines[i].count("}")
        if base_depth is None and opens > 0:
            base_depth = 1
            depth = opens - closes
            continue
        if base_depth is not None:
            prev = depth
            if prev == 1 and opens > 0 and re.search(label_re, lines[i]):
                sub_end = find_brace_end(lines, i)
                results.append((i, sub_end))
            depth += opens - closes
    return results


def find_property_line(lines, start, end, prop_re):
    """Return the index of the first line in [start, end) matching *prop_re*,
    or None."""
    for i in range(start, end + 1):
        if re.search(prop_re, lines[i]):
            return i
    return None


def get_indent(line):
    """Return the leading whitespace of *line*."""
    return re.match(r"(\s*)", line).group(1)


def remove_block(lines, start, end):
    """Remove lines[start..end] (inclusive) and one adjacent blank line to
    avoid double-blank gaps, while keeping a single separator between the
    surrounding nodes."""
    has_blank_before = (start > 0 and lines[start - 1].strip() == "")
    has_blank_after = (end + 1 < len(lines) and lines[end + 1].strip() == "")
    if has_blank_before and not has_blank_after:
        start -= 1
    elif has_blank_after and not has_blank_before:
        end += 1
    elif has_blank_before and has_blank_after:
        start -= 1
    del lines[start : end + 1]
    return end - start + 1


def extract_property_value(lines, start, end, prop_name):
    """Extract the hex value of a simple property like ``xlnx,rpu = <0x6>;``."""
    for i in range(start, end + 1):
        m = re.search(rf"{re.escape(prop_name)}\s*=\s*<(0x[0-9a-fA-F]+)>", lines[i])
        if m:
            return int(m.group(1), 16)
    return None


def extract_property_string(lines, start, end, prop_re):
    """Extract a string property value matching prop_re."""
    for i in range(start, end + 1):
        m = re.search(prop_re, lines[i])
        if m:
            return m.group(1) if m.lastindex else lines[i]
    return None


# ---------------------------------------------------------------------------
# XSA / HWH Parsing
# ---------------------------------------------------------------------------

class HwConnections:
    """Hardware connections extracted from bd.hwh in the XSA archive.

    Attributes:
        tile_isp_to_preproc: dict mapping (tile, isp) -> preprocess_accel instance name
        preproc_to_frmbuf:   dict mapping preprocess_accel instance -> frmbuf_accel instance
        tile_isp_to_so_frmbuf: dict mapping (tile, isp) -> SO v_frmbuf_wr instance
    """
    def __init__(self):
        self.tile_isp_to_preproc = {}
        self.preproc_to_frmbuf = {}
        self.tile_isp_to_so_frmbuf = {}


def parse_xsa_connections(xsa_path):
    """Parse bd.hwh from XSA to extract ISP pipeline connections."""
    if not os.path.isfile(xsa_path):
        fatal(f"XSA file not found: {xsa_path}")

    with zipfile.ZipFile(xsa_path, 'r') as zf:
        hwh_name = None
        for name in zf.namelist():
            if name == 'bd.hwh' or name.endswith('/bd.hwh'):
                hwh_name = name
                break
        if hwh_name is None:
            fatal(f"bd.hwh not found in XSA: {xsa_path}")

        with zf.open(hwh_name) as hwh_file:
            tree = ET.parse(hwh_file)

    root = tree.getroot()
    conn = HwConnections()

    # Build a map: instance_name -> list of (busname, interface_name, type)
    # We only care about AXIS interfaces on visp_ss, preprocess_accel, frmbuf_accel
    for module in root.iter('MODULE'):
        inst_name = module.get('INSTANCE', '')
        for bi in module.iter('BUSINTERFACE'):
            busname = bi.get('BUSNAME', '')
            bi_name = bi.get('NAME', '')
            bi_type = bi.get('TYPE', '')
            vlnv = bi.get('VLNV', '')

            if 'axis' not in vlnv:
                continue

            # visp_ss VIDOUT_SO -> preprocess_accel (secondary output for preprocessing)
            m = re.match(r'.*_TILE(\d+)_ISP(\d+)_VIDOUT_SO$', busname)
            if m and bi_type == 'TARGET':
                tile = int(m.group(1))
                isp = int(m.group(2))
                conn.tile_isp_to_preproc[(tile, isp)] = inst_name

            # visp_ss VIDOUT_PO -> SO frmbuf (primary output for video capture)
            m = re.match(r'.*_TILE(\d+)_ISP(\d+)_VIDOUT_PO$', busname)
            if m and bi_type == 'TARGET':
                tile = int(m.group(1))
                isp = int(m.group(2))
                conn.tile_isp_to_so_frmbuf[(tile, isp)] = inst_name

            # preprocess_accel output -> frmbuf_accel input
            m = re.match(r'(.+_preprocess_accel_\d+)_m_axis_video$', busname)
            if m and bi_type == 'TARGET':
                preproc_inst = m.group(1)
                conn.preproc_to_frmbuf[preproc_inst] = inst_name

    if not conn.tile_isp_to_preproc:
        fatal("No VIDOUT_SO -> preprocess_accel connections found in HWH")

    return conn


def extract_preproc_index(inst_name):
    """Extract the numeric index from a preprocess_accel instance name."""
    m = re.search(r'preprocess_accel_(\d+)$', inst_name)
    if m:
        return int(m.group(1))
    fatal(f"Cannot extract index from preprocess_accel instance: {inst_name}")


def extract_frmbuf_index(inst_name):
    """Extract the numeric index from a frmbuf_accel instance name."""
    m = re.search(r'frmbuf_accel_(\d+)$', inst_name)
    if m:
        return int(m.group(1))
    fatal(f"Cannot extract index from frmbuf_accel instance: {inst_name}")


# ---------------------------------------------------------------------------
# Discovery
# ---------------------------------------------------------------------------

class VispInstance:
    """Collected metadata for one visp_ss ISP instance."""
    def __init__(self, suffix, index, rpu, tile, isp_id, tile_local_isp):
        self.suffix = suffix            # e.g. "00", "01", "12", "13"
        self.index = index              # ordinal (0, 1, 2, ...)
        self.rpu = rpu                  # RPU id (e.g. 6, 7)
        self.tile = tile                # tile id (first digit of suffix)
        self.isp_id = isp_id            # global isp_id from DTS (0, 1, 2, 3)
        self.tile_local_isp = tile_local_isp  # ISP index within tile (0 or 1)
        # These are set later by apply_hw_mapping():
        self.preproc_index = index      # default: ordinal
        self.frmbuf_index = index       # default: ordinal


def discover_visp_instances(lines):
    """Discover all visp_ss ISP instances and return a list of VispInstance
    objects, ordered by appearance."""
    nodes = find_top_level_nodes(lines, r"\bvisp_ss_\w+:\s+visp_ss@")
    if not nodes:
        fatal("No visp_ss ISP instances found in pl.dtsi")

    instances = []
    for idx, (start, end) in enumerate(nodes):
        m = re.search(r"visp_ss_(\w+):", lines[start])
        if not m:
            fatal(f"Cannot parse visp_ss label at line {start + 1}")
        suffix = m.group(1)
        tile = int(suffix[0])
        rpu = extract_property_value(lines, start, end, "xlnx,rpu")
        if rpu is None:
            fatal(f"visp_ss_{suffix}: cannot find xlnx,rpu property")

        # Extract isp_id property
        isp_id = extract_property_value(lines, start, end, "isp_id")
        if isp_id is None:
            fatal(f"visp_ss_{suffix}: cannot find isp_id property")

        # Determine tile-local ISP index from interrupt-names
        tile_local_isp = None
        irq_line = find_property_line(lines, start, end, r"interrupt-names")
        if irq_line is not None:
            irq_m = re.search(r'tile\d+_isp(\d+)_', lines[irq_line])
            if irq_m:
                tile_local_isp = int(irq_m.group(1))
        if tile_local_isp is None:
            # Fallback: derive from isp_id (even=0, odd=1 within tile)
            tile_local_isp = isp_id % 2

        instances.append(VispInstance(suffix, idx, rpu, tile, isp_id, tile_local_isp))
    return instances


def apply_hw_mapping(instances, hw_conn):
    """Apply hardware connection mapping from XSA to visp instances."""
    for inst in instances:
        key = (inst.tile, inst.tile_local_isp)
        if key in hw_conn.tile_isp_to_preproc:
            preproc_inst = hw_conn.tile_isp_to_preproc[key]
            inst.preproc_index = extract_preproc_index(preproc_inst)
            if preproc_inst in hw_conn.preproc_to_frmbuf:
                frmbuf_inst = hw_conn.preproc_to_frmbuf[preproc_inst]
                inst.frmbuf_index = extract_frmbuf_index(frmbuf_inst)
            else:
                inst.frmbuf_index = inst.preproc_index
        else:
            print(f"  WARNING: No HW connection found for tile={inst.tile}, "
                  f"isp={inst.tile_local_isp}; using ordinal index {inst.index}")
            inst.preproc_index = inst.index
            inst.frmbuf_index = inst.index


def discover_visp_mbox_rpus(lines):
    """Return list of (rpu_id, start, end) for visp_mbox_rpu nodes."""
    nodes = find_top_level_nodes(lines, r"\bvisp_mbox_rpu_\d+:")
    if not nodes:
        fatal("No visp_mbox_rpu nodes found in pl.dtsi")
    results = []
    for start, end in nodes:
        rpu = extract_property_value(lines, start, end, "rpu_id")
        if rpu is None:
            fatal(f"visp_mbox_rpu at line {start + 1}: cannot find rpu_id")
        results.append((rpu, start, end))
    return results


# ---------------------------------------------------------------------------
# Transformation helpers
# ---------------------------------------------------------------------------

def transform_firmware_name(lines):
    """Change firmware-name to x_plus_ml.pdi."""
    for i, line in enumerate(lines):
        if "firmware-name" in line and "x_plus_ml.pdi" in line:
            print("  firmware-name already set to x_plus_ml.pdi (skipped)")
            return
        if "firmware-name" in line and "vpl_gen_fixed_pld.pdi" in line:
            lines[i] = line.replace("vpl_gen_fixed_pld.pdi", "x_plus_ml.pdi")
            return
    fatal("Cannot find firmware-name property in pl.dtsi")


def remove_top_level_nodes(lines, label_re, description):
    """Remove all top-level nodes matching *label_re*.
    Processes in reverse to keep indices stable."""
    nodes = find_top_level_nodes(lines, label_re)
    if not nodes:
        print(f"  INFO: No {description} nodes found (nothing to remove)")
        return 0
    count = 0
    for start, end in reversed(nodes):
        remove_block(lines, start, end)
        count += 1
    print(f"  Removed {count} {description} node(s)")
    return count


def transform_visp_mbox(lines):
    """Add memory-region and rproc to visp_mbox_rpu nodes."""
    mboxes = discover_visp_mbox_rpus(lines)
    for rpu_id, start, end in reversed(mboxes):
        has_memregion = find_property_line(lines, start, end, r"memory-region")
        has_rproc = find_property_line(lines, start, end, r"\brproc\b")

        if has_memregion and has_rproc:
            continue

        rpu_line = find_property_line(lines, start, end, r"rpu_id\s*=")
        if rpu_line is None:
            fatal(f"visp_mbox_rpu at line {start + 1}: cannot find rpu_id line")

        indent = get_indent(lines[rpu_line])
        insertions = []
        if not has_memregion:
            insertions.append(f"{indent}memory-region = <&isp_mbox_buffer>;\n")
        insertions.append(lines[rpu_line])
        if not has_rproc:
            insertions.append(f"{indent}rproc = <&r52_{rpu_id}>;\n")

        lines[rpu_line : rpu_line + 1] = insertions

    print(f"  Updated {len(mboxes)} visp_mbox_rpu node(s)")


def transform_visp_ss(lines, instances):
    """For each visp_ss ISP node:
      - Add memory-region property (if absent)
      - Remove port@0 (axis broadcaster input port)
      - Add port@2 (preprocess output port)
    Processes in reverse order to keep indices stable.
    """
    for inst in reversed(instances):
        nodes = find_top_level_nodes(
            lines, rf"\bvisp_ss_{re.escape(inst.suffix)}:\s+visp_ss@"
        )
        if len(nodes) != 1:
            fatal(f"Expected 1 visp_ss_{inst.suffix} node, found {len(nodes)}")
        start, end = nodes[0]

        # --- Add memory-region before the ports block (if not present) ---
        mem_region = f"rproc_D_{inst.tile}_{inst.rpu}_calib_load"
        if not find_property_line(lines, start, end, r"memory-region"):
            compat_line = find_property_line(lines, start, end, r'compatible\s*=')
            if compat_line is None:
                fatal(f"visp_ss_{inst.suffix}: cannot find compatible property")
            indent = get_indent(lines[compat_line])
            lines.insert(compat_line + 1, f"{indent}memory-region = <&{mem_region}>;\n")
            end += 1

        # --- Remove port@0 (the one connecting to axis_broadcaster) ---
        # Find the ports sub-node
        ports_nodes = find_sub_nodes(lines, start, end,
                                     rf"ports{re.escape(inst.suffix)}:\s+ports")
        if not ports_nodes:
            fatal(f"visp_ss_{inst.suffix}: cannot find ports sub-node")
        ports_start, ports_end = ports_nodes[0]

        # Find port@0 within ports
        port0_nodes = find_sub_nodes(lines, ports_start, ports_end, r"port@0\s*\{")
        if port0_nodes:
            p0_start, p0_end = port0_nodes[0]
            removed = remove_block(lines, p0_start, p0_end)
            end -= removed
            # Re-find ports boundary after removal
            ports_nodes = find_sub_nodes(lines, start, end,
                                         rf"ports{re.escape(inst.suffix)}:\s+ports")
            ports_start, ports_end = ports_nodes[0]

        # --- Add port@2 (if not present) ---
        port2_nodes = find_sub_nodes(lines, ports_start, ports_end, r"port@2\s*\{")
        if not port2_nodes:
            # Find port@1 to insert after it
            port1_nodes = find_sub_nodes(lines, ports_start, ports_end, r"port@1\s*\{")
            if not port1_nodes:
                fatal(f"visp_ss_{inst.suffix}: cannot find port@1 in ports")
            p1_start, p1_end = port1_nodes[0]
            indent_port = get_indent(lines[p1_start])
            indent_prop = indent_port + "        "
            indent_ep = indent_prop + "        "

            # Endpoint label in preprocess_accel port@0 that this points to
            preproc_ep_label = (
                f"mipi_rx_ss_hier_preproc_hier_preprocess_accel_"
                f"{inst.preproc_index}mipi_rx_ss_hier_visp_ss_0"
            )

            port2_block = [
                "\n",
                f"{indent_port}visp_port2_po_{inst.index}: port@2 {{\n",
                f"{indent_prop}reg = <0x2>;\n",
                f"{indent_prop}direction = \"output\";\n",
                "\n",
                f"{indent_prop}visp_out2visp_ss_{inst.suffix}: endpoint {{\n",
                f"{indent_ep}remote-endpoint = <&{preproc_ep_label}>;\n",
                f"{indent_prop}}};\n",
                f"{indent_port}}};\n",
            ]
            insert_pos = p1_end + 1
            for j, bl in enumerate(port2_block):
                lines.insert(insert_pos + j, bl)

    print(f"  Updated {len(instances)} visp_ss ISP node(s)")


def transform_vcap_visp_ss(lines, instances):
    """For each vcap_visp_ss node:
      - Add memory-region = <&cma_128m_N>;
      - Change dma-names to include "port1"
      - Add second DMA channel
      - Add port@1 input
    Processes in reverse order.
    """
    for inst in reversed(instances):
        nodes = find_top_level_nodes(
            lines, rf"\bvcap_visp_ss_{re.escape(inst.suffix)}:"
        )
        if len(nodes) != 1:
            fatal(f"Expected 1 vcap_visp_ss_{inst.suffix} node, found {len(nodes)}")
        start, end = nodes[0]

        # --- Add memory-region (if absent) ---
        if not find_property_line(lines, start, end, r"memory-region"):
            streamon_line = find_property_line(lines, start, end, r"xlnx,atomic_streamon")
            if streamon_line is None:
                fatal(f"vcap_visp_ss_{inst.suffix}: cannot find xlnx,atomic_streamon")
            indent = get_indent(lines[streamon_line])
            lines.insert(streamon_line + 1, f"{indent}memory-region = <&cma_128m_{inst.index}>;\n")
            end += 1

        # --- Update dma-names ---
        dma_names_line = find_property_line(lines, start, end, r'dma-names\s*=\s*"port0"')
        if dma_names_line is not None:
            lines[dma_names_line] = lines[dma_names_line].replace(
                'dma-names = "port0";',
                'dma-names = "port0", "port1";'
            )

        # --- Update dmas to add second channel ---
        dmas_line = find_property_line(lines, start, end, r"dmas\s*=")
        if dmas_line is not None:
            old_dmas = lines[dmas_line]
            frmbuf_label = f"mipi_rx_ss_hier_preproc_hier_frmbuf_accel_{inst.frmbuf_index}"
            if frmbuf_label not in old_dmas:
                new_dmas = old_dmas.rstrip().rstrip(";")
                new_dmas += f", <&{frmbuf_label} 0x1>;\n"
                lines[dmas_line] = new_dmas

        # --- Add port@1 (if absent) ---
        # Re-read end since we may have inserted lines
        nodes = find_top_level_nodes(
            lines, rf"\bvcap_visp_ss_{re.escape(inst.suffix)}:"
        )
        start, end = nodes[0]
        ports_nodes = find_sub_nodes(lines, start, end,
                                     rf"vcap_portsvisp_ss_{re.escape(inst.suffix)}:\s+ports")
        if not ports_nodes:
            fatal(f"vcap_visp_ss_{inst.suffix}: cannot find ports sub-node")
        ports_start, ports_end = ports_nodes[0]

        port1_nodes = find_sub_nodes(lines, ports_start, ports_end, r"port@1\s*\{")
        if not port1_nodes:
            port0_nodes = find_sub_nodes(lines, ports_start, ports_end, r"port@0\s*\{")
            if not port0_nodes:
                fatal(f"vcap_visp_ss_{inst.suffix}: cannot find port@0")
            p0_start, p0_end = port0_nodes[0]
            indent_port = get_indent(lines[p0_start])
            indent_prop = indent_port + "        "
            indent_ep = indent_prop + "        "

            # Endpoint label in preprocess_accel port@1 that this points to
            preproc_ep_label = (
                f"mipi_rx_ss_hier_preproc_hier_preprocess_accel_"
                f"{inst.preproc_index}mipi_rx_ss_hier_preproc_hier_frmbuf_accel_"
                f"{inst.frmbuf_index}"
            )

            port1_block = [
                "\n",
                f"{indent_port}vcap_portvisp_ss_{inst.suffix}_1: port@1 {{\n",
                f"{indent_prop}reg = <0x1>;\n",
                f"{indent_prop}direction = \"input\";\n",
                "\n",
                f"{indent_prop}vcap_{inst.index}_input_2: endpoint {{\n",
                f"{indent_ep}remote-endpoint = <&{preproc_ep_label}>;\n",
                f"{indent_prop}}};\n",
                f"{indent_port}}};\n",
            ]
            insert_pos = p0_end + 1
            for j, bl in enumerate(port1_block):
                lines.insert(insert_pos + j, bl)

    print(f"  Updated {len(instances)} vcap_visp_ss node(s)")


def transform_preprocess_accel(lines, instances):
    """For each preprocess_accel node:
      - In port@0 endpoint, add remote-endpoint to visp_out2visp_ss_XX
      - In port@1 endpoint, change remote-endpoint to vcap_N_input_2
    Processes in reverse order.
    """
    for inst in reversed(instances):
        nodes = find_top_level_nodes(
            lines,
            rf"\bmipi_rx_ss_hier_preproc_hier_preprocess_accel_{inst.preproc_index}:\s+preprocess_accel@"
        )
        if len(nodes) != 1:
            fatal(f"Expected 1 preprocess_accel_{inst.preproc_index} node, found {len(nodes)}")
        start, end = nodes[0]

        # Find ports sub-node
        ports_nodes = find_sub_nodes(
            lines, start, end,
            rf"preprocess_portsmipi_rx_ss_hier_preproc_hier_preprocess_accel_{inst.preproc_index}:\s+ports"
        )
        if not ports_nodes:
            fatal(f"preprocess_accel_{inst.preproc_index}: cannot find ports sub-node")
        ports_start, ports_end = ports_nodes[0]

        # --- Update port@1 endpoint remote-endpoint ---
        port1_nodes = find_sub_nodes(lines, ports_start, ports_end, r"port@1\s*\{")
        if not port1_nodes:
            fatal(f"preprocess_accel_{inst.preproc_index}: cannot find port@1")
        p1_start, p1_end = port1_nodes[0]

        old_re = (
            f"mipi_rx_ss_hier_preproc_hier_frmbuf_accel_{inst.frmbuf_index}"
            f"mipi_rx_ss_hier_preproc_hier_preprocess_accel_{inst.preproc_index}"
        )
        new_re = f"vcap_{inst.index}_input_2"
        for i in range(p1_start, p1_end + 1):
            if "remote-endpoint" in lines[i] and old_re in lines[i]:
                lines[i] = lines[i].replace(old_re, new_re)
                break

        # --- Update port@0 endpoint: add remote-endpoint if missing ---
        port0_nodes = find_sub_nodes(lines, ports_start, ports_end, r"port@0\s*\{")
        if not port0_nodes:
            fatal(f"preprocess_accel_{inst.preproc_index}: cannot find port@0")
        p0_start, p0_end = port0_nodes[0]

        has_remote = find_property_line(lines, p0_start, p0_end, r"remote-endpoint")
        if not has_remote:
            # Find the endpoint opening line, insert remote-endpoint before the closing '}'
            for i in range(p0_start, p0_end + 1):
                if "endpoint" in lines[i] and "{" in lines[i]:
                    ep_end = find_brace_end(lines, i)
                    indent = get_indent(lines[i]) + "        "
                    remote_line = (
                        f"{indent}remote-endpoint = "
                        f"<&visp_out2visp_ss_{inst.suffix}>;\n"
                    )
                    lines.insert(ep_end, remote_line)
                    end += 1
                    break

    print(f"  Updated {len(instances)} preprocess_accel node(s)")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args():
    parser = argparse.ArgumentParser(
        description="Update pl.dtsi with ISP pipeline integration changes."
    )
    parser.add_argument(
        "input_dtsi",
        help="Path to the input pl.dtsi file."
    )
    parser.add_argument(
        "--xsa",
        default=None,
        help="Path to the linked XSA file. When provided, ISP-to-preprocess "
             "and preprocess-to-frmbuf connections are extracted from the "
             "hardware handoff instead of using ordinal-based mapping."
    )
    parser.add_argument(
        "-o", "--output",
        default=None,
        help="Path for the output file. If not specified, the input file is "
             "modified in-place."
    )
    return parser.parse_args()


def main():
    args = parse_args()
    pl_path = args.input_dtsi
    out_path = args.output if args.output else pl_path
    xsa_path = args.xsa

    if not os.path.isfile(pl_path):
        fatal(f"Input file not found: {pl_path}")

    print(f"Reading {pl_path}")
    lines = read_file(pl_path)

    # -- Parse XSA for hardware connections (if provided) --
    hw_conn = None
    if xsa_path:
        print(f"Parsing hardware connections from {xsa_path}...")
        hw_conn = parse_xsa_connections(xsa_path)
        print(f"  Found {len(hw_conn.tile_isp_to_preproc)} ISP->preprocess connection(s)")
        print(f"  Found {len(hw_conn.preproc_to_frmbuf)} preprocess->frmbuf connection(s)")

    # -- Discover ISP instances before any modifications --
    print("Discovering ISP instances...")
    instances = discover_visp_instances(lines)
    print(f"  Found {len(instances)} visp_ss instance(s): "
          + ", ".join(f"visp_ss_{v.suffix} (tile={v.tile}, isp={v.tile_local_isp}, "
                      f"rpu={v.rpu})"
                      for v in instances))

    # -- Apply hardware mapping --
    if hw_conn:
        apply_hw_mapping(instances, hw_conn)
        print("  ISP-to-preprocess mapping (from XSA):")
        for inst in instances:
            print(f"    visp_ss_{inst.suffix} -> preprocess_accel_{inst.preproc_index}, "
                  f"frmbuf_accel_{inst.frmbuf_index}")
    else:
        print("  Using ordinal-based mapping (no --xsa provided)")

    # -- Validate that required components exist --
    for inst in instances:
        pp = find_top_level_nodes(
            lines,
            rf"\bmipi_rx_ss_hier_preproc_hier_preprocess_accel_{inst.preproc_index}:\s+preprocess_accel@"
        )
        if not pp:
            fatal(f"preprocess_accel_{inst.preproc_index} node not found (expected for "
                  f"visp_ss_{inst.suffix})")
        vcap = find_top_level_nodes(
            lines, rf"\bvcap_visp_ss_{re.escape(inst.suffix)}:"
        )
        if not vcap:
            fatal(f"vcap_visp_ss_{inst.suffix} node not found")

    # -- Apply transformations --
    print("Applying transformations...")

    # 1. firmware-name
    transform_firmware_name(lines)
    print("  Updated firmware-name")

    # 2. Remove MIPI CSI2 RX subsystem nodes
    remove_top_level_nodes(
        lines,
        r"\bmipi_rx_ss_hier_mipi_csi2_rx_subsyst_\d+:\s+mipi_csi2_rx_subsystem@",
        "MIPI CSI2 RX subsystem"
    )

    # 3. Remove FMC IIC nodes (PL IIC buses not used)
    remove_top_level_nodes(
        lines,
        r"\bmipi_rx_ss_hier_fmc_iic_\d+:\s+i2c@",
        "FMC IIC"
    )

    # 4. Remove AXIS broadcaster nodes
    remove_top_level_nodes(
        lines,
        r"\bmipi_rx_ss_hier_axis_broadcaster_\d+:\s+axis_broadcaster",
        "AXIS broadcaster"
    )

    # 5. Remove standalone vcap preproc frmbuf nodes
    remove_top_level_nodes(
        lines,
        r"\bvcap_mipi_rx_ss_hier_preproc_hier_frmbuf_accel_\d+\s*\{",
        "standalone vcap preproc frmbuf"
    )

    # 6. Update visp_mbox_rpu nodes (memory-region + rproc)
    transform_visp_mbox(lines)

    # 7. Update visp_ss ISP nodes (memory-region, port@0 removal, port@2 addition)
    # Re-discover after removals shifted line numbers
    instances = discover_visp_instances(lines)
    if hw_conn:
        apply_hw_mapping(instances, hw_conn)
    transform_visp_ss(lines, instances)

    # 8. Update vcap_visp_ss nodes (memory-region, DMA, port@1)
    # Re-discover after modifications
    instances = discover_visp_instances(lines)
    if hw_conn:
        apply_hw_mapping(instances, hw_conn)
    transform_vcap_visp_ss(lines, instances)

    # 9. Update preprocess_accel nodes (remote-endpoints)
    instances = discover_visp_instances(lines)
    if hw_conn:
        apply_hw_mapping(instances, hw_conn)
    transform_preprocess_accel(lines, instances)

    # -- Write output --
    write_file(out_path, lines)
    if out_path == pl_path:
        print(f"\nSuccessfully updated {pl_path} (in-place)")
    else:
        print(f"\nSuccessfully wrote updated DTSI to {out_path}")


if __name__ == "__main__":
    main()
