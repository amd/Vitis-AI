import os
import sys
from collections import OrderedDict
import re


SECTIONS_TO_MERGE = ['clock', 'connectivity']
link_path = os.path.abspath("../../link/system.cfg")
base_path = os.getenv("VAISW_HOME")
npu_name  = os.getenv("NPU_IP")
npu2_name = os.getenv("NPU_IP2")
mipso_aggr_noc_tag = "AIE_DDR"
vitis_aggr_noc_tag = "LPDDR"
CFG_FILES_LIST = []

#
### NPU 0 checking & setting
#
if not npu_name:
    print(f"ERROR: NPU_IP is not set, please source npu_ip/settings.sh to select the IP to generate")
    sys.exit(1)

print(f"NPU_IP0 is set to: {npu_name}")
npu_cfg = os.path.join(base_path, 'npu_ip', npu_name, 'link', 'system.cfg')
CFG_FILES_LIST.append(npu_cfg)
if not os.path.exists(npu_cfg):
    print(f"Missing cfg file of {npu_name} at: {npu_cfg}")
    sys.exit(1)

#
### NPU 2 checking & setting
#
if npu2_name is None:
    print(f"NPU_IP2 isn't set, hence no system.cfg merge !")
    npu2_cfg = ""
else:
    print(f"NPU_IP2 detected : {npu2_name} \nMerging ...")
    npu2_cfg = os.path.join(base_path, 'npu_ip', npu2_name, 'link', 'system.cfg')
    CFG_FILES_LIST.append(npu2_cfg)
    if not os.path.exists(npu2_cfg):
        print(f"Missing cfg file of {npu2_name} at: {npu2_cfg}")
        sys.exit(1)

#
### image_processing settings
#
if os.getenv("DISABLE_IMG_PROCE", "").lower() not in ("true", "1"):
    img_proc_cfg = os.path.abspath("../image_processing/img_processing.cfg")
    CFG_FILES_LIST.append(img_proc_cfg)
    if not os.path.exists(img_proc_cfg):
        print(f"Missing cfg file of image_processing at: {img_proc_cfg}")
        sys.exit(1)

def parse_file(path):
    sections = OrderedDict()
    current = None
    header_pattern = re.compile(r'^\s*\[(.+?)\]\s*$')
    with open(path, 'r') as f:
        for line in f:
            match = header_pattern.match(line)
            if match:
                current = match.group(1)
                sections[current] = []
            elif current is not None:
                stripped = line.rstrip('\n')
                if stripped.strip():
                    sections[current].append(stripped)
    return sections


def merge_sections(sections_list, sections_to_merge):
    merged = OrderedDict()
    base_sections = sections_list[0]

    for section, entries in base_sections.items():
        if section in sections_to_merge:
            merged_entries = []
            for sec in sections_list:
                merged_entries.extend(sec.get(section, []))
            merged[section] = merged_entries
        else:
            merged[section] = list(entries)
    return merged


def write_output(merged, out_path):
    with open(out_path, 'w') as f:
        for section, entries in merged.items():
            f.write(f"[{section}]\n")
            for entry in entries:
                if section == "connectivity":
                    entry = entry.replace(mipso_aggr_noc_tag, vitis_aggr_noc_tag)
                    if ("noc.read_bw=" in entry or "noc.write_bw" in entry) and ("vss_npu" in entry or "npuGraph" in entry) and npu2_name :
                        bw = int(entry.split(':')[-1].split('.')[0])
                        half_bw = int(bw / 2)
                        entry = entry.replace(str(bw) ,str(half_bw))
                f.write(f"{entry}\n")
            f.write("\n")


def main():
    sections_list = [parse_file(path) for path in CFG_FILES_LIST]
    merged = merge_sections(sections_list, SECTIONS_TO_MERGE)
    write_output(merged, link_path)
    print(f"Merged sections written to {link_path}")


if __name__ == '__main__':
    main()
