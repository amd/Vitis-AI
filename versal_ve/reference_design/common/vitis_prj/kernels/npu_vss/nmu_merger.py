import os
import sys


SECTIONS_TO_MERGE = ['clock', 'connectivity']
placement_path = os.path.abspath("../../link/place_pl_nmu.tcl")
connect_pp_patch = os.path.abspath("../npu_tail/connect_pp_top.tcl")


base_path = os.getenv("VAISW_HOME")
npu_name  = os.getenv("NPU_IP")
npu2_name = os.getenv("NPU_IP2")
npu_tail  = os.getenv("ENABLE_NPU_TAIL")

disable_img_proce = os.getenv("DISABLE_IMG_PROCE")
NMU_FILES_LIST = []

#
### NPU 0 checking & setting
#
if not npu_name:
    print(f"\nError: NPU_IP is not set, please source npu_ip/settings.sh to select the IP to generate\n")
    sys.exit(1)

print(f"NPU_IP0 is set to : {npu_name}")
npu_nmu = os.path.join(base_path, 'npu_ip', npu_name, 'link', 'place_pl_nmu.tcl')
NMU_FILES_LIST.append(npu_nmu)
if not os.path.exists(npu_nmu):
        print(f"Missing cfg file of {npu_name} at: {npu_nmu}")
        sys.exit(1)


#
### NPU 2 checking & setting
#
if npu2_name is None:
  print(f"NPU_IP2 is not set, hence no system.cfg merge!")
  npu2_nmu = ""
else:
  print(f"NPU_IP2 detected: {npu2_name}\nMerging NMU placements...")
  npu2_nmu = os.path.join(base_path, 'npu_ip', npu2_name, 'link', 'place_pl_nmu.tcl')
  NMU_FILES_LIST.append(npu2_nmu)
  if not os.path.exists(npu2_nmu):
    print(f"Missing cfg file of {npu2_name} at: {npu2_nmu}")
    sys.exit(1)


#
### Image Processing checking & setting
#
if disable_img_proce is None or disable_img_proce.lower() in ['false', '0']:
  print(f"Image processing is enabled (DISABLE_IMG_PROCE={disable_img_proce})\nMerging image_processing NMU placement...")
  img_proce_nmu = os.path.abspath("../image_processing/place_pl_nmu.tcl")
  if not os.path.exists(img_proce_nmu):
    print(f"Missing placement file for image_processing at: {img_proce_nmu}")
    sys.exit(1)
  NMU_FILES_LIST.append(img_proce_nmu)
else:
  print(f"Image processing is disabled (DISABLE_IMG_PROCE={disable_img_proce}), skipping image_processing placement")
  img_proce_nmu = ""





def extract_placements_and_interfaces(files):
  placements = []
  phy_loc_cstr = []

  for path in files:
    print(f"opening path : {path}")
    with open(path, "r") as nmu_f:
      lines = nmu_f.readlines()
      for line in lines:
        line = line.strip()
        if not line or line[0] == "#":
          continue

        parts = line.split()
        if len(parts) < 5:
          print(f"ERROR: {path}: Malformed constraint line (expected at least 5 parts): {line}")
          sys.exit(1)

        placement  = parts[2].strip("{}")

        # Check for NMU overlaps
        if placement in placements:
          print(f"\nERROR: Duplicate NMU placement found: {placement} (in file {path})\n")
          sys.exit(1)

        placements.append(placement)
        phy_loc_cstr.append(line)

  return phy_loc_cstr


def dump_noc_configs(phy_loc_cstr):
  with open(placement_path, "w") as pla_f:
    print(f"Writing placement...")
    for placement in phy_loc_cstr:
      pla_f.write(placement+"\n")
    pla_f.write("\n##########################################################################################\n\n")
    print(f"INFO: NMU placements merged in {placement_path}")

    if npu_tail is not None:
      with open(connect_pp_patch, "r") as pp_f:
        lines = pp_f.readlines()
        for line in lines:
          pla_f.write(line)
    print(f"INFO: PP_TAIL NOC connectivity patch inserted in {placement_path}")


def main():
  dump_noc_configs(extract_placements_and_interfaces(NMU_FILES_LIST))

if __name__ == '__main__':
    main()
