import os
import re
import sys
import glob

rpt_file_o = "./npu_utilization.rpt"

# Post-route hierarchical utilization report
hier_rpt = "link/reports/utilization_post_route_opt_hierarc.log"

def usage():
  print("ERROR: Missing or too many arguments.")
  print(f"USAGE: python3 utilization.py PATH_2_NPU_VSS NPU_IP")
  print(f"")
  print(f"Report is generated in {rpt_file_o}")
  sys.exit(1)

if __name__ == "__main__":
  if (len(sys.argv) == 3):
    chippart = sys.argv[2].split('_NPU_IP')[0]
    fpga_info_p =  glob.glob(f"{sys.argv[1]}/fpga_info_*.txt")
    if fpga_info_p == []:
      print(f"ERROR: Missing fpga_info file in {sys.argv[1]}")
      sys.exit(1)
    else:
      print(f"\n############################# ################################\n")
      print(f"  INFO: Generating utilization based on fpga_info:")
      print(f"         {fpga_info_p[0]}\n")
      print(f"############################# ################################\n")
  else:
    usage()

  # Extract relevant data from fpga_info
  nbColumns = 0
  nbAiePerColumn = 0
  firstColumn = None
  boardName = None
  ddrs = None
  architecture = None
  systems = None
  cores = None
  nces = None
  fpga_info_f = open(fpga_info_p[0], 'r')
  fpga_info_c = fpga_info_f.readlines()
  fpga_info_f.close()
  for line in fpga_info_c:
    if "nbColumns" in line:
      nbColumns = int(line.split('=')[1])
    if "nbAiePerColumn" in line:
      nbAiePerColumn = int(line.split('=')[1])
    if "firstColumn" in line:
      firstColumn = int(line.split('=')[1])
    if "boardName" in line:
      boardName = line.split('=')[1].strip().strip('"')
    if "architecture" in line:
      architecture = line.split('=')[1].strip().strip('"')
    if line.strip().startswith("systems"):
      systems = int(line.split('=')[1])
    if line.strip().startswith("cores"):
      cores = int(line.split('=')[1])
    if line.strip().startswith("nces"):
      nces = int(line.split('=')[1])
    # DDR count key differs per arch (AIE vs AIE-ML)
    if line.strip().startswith("externalMemories"):
      ddrs = int(line.split('=')[1])
    if line.strip().startswith("ddrs"):
      ddrs = int(line.split('=')[1])

  # AIE fields validated later, only if the part has AIE (see has_aie below).
  if not os.path.isfile(hier_rpt):
    print(f"ERROR: Missing post-route utilization report\n{hier_rpt}")
    sys.exit(1)
  hier_f = open(hier_rpt, 'r')
  hier_content = hier_f.readlines()
  hier_f.close()

  # Available totals: "#NPU_AVAIL <KEY> <VAL>" lines from post_route_opt-99-reports.tcl
  avail = {}
  for line in hier_content:
    if line.startswith("#NPU_AVAIL"):
      parts = line.split()
      if len(parts) >= 3:
        avail[parts[1]] = int(parts[2])
  needed = ["LUTS", "REGISTERS", "BLOCK_RAMS", "URAM", "DSP", "PL_NMU", "AIE_NMU", "AIE"]
  if any(k not in avail for k in needed):
    print(f"ERROR: Missing #NPU_AVAIL totals in {hier_rpt} (found {sorted(avail)}).")
    print(f"       Ensure post_route_opt-99-reports.tcl appended them.")
    sys.exit(1)

  # AIE total > 0 => Versal (AIE + NoC NMUs); 0 => PL-only ZynqMP.
  has_aie = avail["AIE"] > 0
  if has_aie:
    if nbColumns == 0 or nbAiePerColumn == 0 or firstColumn is None:
      print(f"ERROR: Corrupted fpga_info file {fpga_info_p[0]}")
      sys.exit(1)
    if architecture is not None and "AIEML" in architecture:
      nb_aie = nbColumns * nbAiePerColumn
    else:
      if None in [systems, cores, nces]:
        print(f"ERROR: Missing systems/cores/nces in {fpga_info_p[0]}")
        sys.exit(1)
      nb_aie = systems * cores * nces * nbAiePerColumn
  else:
    nb_aie = 0

  # NPU wrapper module, named per arch: AIE-ML vss, AIE-v1 vss, or ZynqMP non-vss.
  aieml_mod = f"bd_vss_npu_aieml_O{firstColumn}_npu_aieml_O{firstColumn}_0" if firstColumn is not None else None
  aiev1_mod = "bd_vss_npu_0_npu_0_0"
  zynqmp_re = re.compile(r"^bd_npu_\d+_0$")

  def find_wrapper(module):
    for line in hier_content:
      cols = line.split('|')
      if len(cols) >= 12 and cols[2].strip() == module:
        return cols
    return None

  def find_wrapper_re(rx):
    # Match module or instance column (OOC top row carries the name as instance).
    for line in hier_content:
      cols = line.split('|')
      if len(cols) >= 12 and (rx.match(cols[2].strip()) or rx.match(cols[1].strip())):
        return cols
    return None

  if has_aie:
    wrapper = find_wrapper(aieml_mod) if aieml_mod else None
    npu_instance_id = firstColumn
    if wrapper is None:
      wrapper = find_wrapper(aiev1_mod)
      npu_instance_id = 0
  else:
    wrapper = find_wrapper_re(zynqmp_re)
    npu_instance_id = 0

  if wrapper is None:
    print(f"ERROR: NPU wrapper row not found in {hier_rpt}")
    print(f"       Tried modules: {aieml_mod} , {aiev1_mod} , bd_npu_<n>_0")
    sys.exit(1)

  # Hierarchical row columns (split on '|'):
  # [1]Instance [2]Module [3]TotalLUTs [4]LogicLUTs [5]LUTRAMs [6]SRLs
  # [7]FFs [8]RAMB36 [9]RAMB18 [10]URAM [11]DSP Blocks
  luts_used   = int(wrapper[3].strip())
  regs_used   = int(wrapper[7].strip())
  ramb36_used = int(wrapper[8].strip())
  ramb18_used = int(wrapper[9].strip())
  uram_used   = int(wrapper[10].strip())
  dsp_used    = int(wrapper[11].strip())
  # Block RAM tiles: a RAMB18 counts as half a RAMB36 tile.
  bram_used = ramb36_used + 0.5 * ramb18_used

  rpt_file_o_f = open(rpt_file_o, 'w')
  # Versal encodes offset/AIEs/mem in the name; ZynqMP keeps the raw NPU_IP.
  if has_aie:
    npu_ip_name = f"{chippart}_NPU_IP_O{firstColumn:02d}_A{nb_aie:03d}_M{ddrs}"
  else:
    npu_ip_name = sys.argv[2]
  rpt_file_o_f.write("############################# ################################\n")
  rpt_file_o_f.write(f"####### Utilization NPU_IP: {npu_ip_name}\n")
  rpt_file_o_f.write("############################# ################################\n")

  def pct(used, total):
    return (100.0 * used / total) if total else 0.0

  rpt_file_o_f.write(f"Registers  : {regs_used} / {avail['REGISTERS']} ({pct(regs_used, avail['REGISTERS']):.2f} %)\n")
  rpt_file_o_f.write(f"LUTS       : {luts_used} / {avail['LUTS']} ({pct(luts_used, avail['LUTS']):.2f} %)\n")
  rpt_file_o_f.write(f"Block RAMs : {bram_used:g} / {avail['BLOCK_RAMS']} ({pct(bram_used, avail['BLOCK_RAMS']):.2f} %)\n")
  rpt_file_o_f.write(f"URAMs      : {uram_used} / {avail['URAM']} ({pct(uram_used, avail['URAM']):.2f} %)\n")
  rpt_file_o_f.write(f"DSP Slices : {dsp_used} / {avail['DSP']} ({pct(dsp_used, avail['DSP']):.2f} %)\n")

  # NMU usage from the link spec. Versal-only; ZynqMP has no NMUs / no system.cfg.
  pl_nmu_cnt = 0
  aie_nmu_cnt = 0
  if avail['PL_NMU'] > 0 and avail['AIE_NMU'] > 0:
    link_cfg_f = open("link/system.cfg", 'r')
    link_cfg_content = link_cfg_f.readlines()
    link_cfg_f.close()
    for line in link_cfg_content :
      if f"sp=vss_npu_{npu_instance_id}_npu_0" in line:
        pl_nmu_cnt = pl_nmu_cnt + 1
        continue

      if f"sp=ai_engine_0.npuGraph_{npu_instance_id}" in line:
        aie_nmu_cnt = aie_nmu_cnt + 1
        continue

  # Omit NMU / AIE lines when the part has none (ZynqMP).
  if avail['PL_NMU'] > 0:
    rpt_file_o_f.write(f"PL_NMU     : {pl_nmu_cnt} / {avail['PL_NMU']} ({pct(pl_nmu_cnt, avail['PL_NMU']):.2f} %)\n")
  if avail['AIE_NMU'] > 0:
    rpt_file_o_f.write(f"AIE_NMU    : {aie_nmu_cnt} / {avail['AIE_NMU']} ({pct(aie_nmu_cnt, avail['AIE_NMU']):.2f} %)\n")
  if avail['AIE'] > 0:
    rpt_file_o_f.write(f"AI Engines : {nb_aie} / {avail['AIE']} ({pct(nb_aie, avail['AIE']):.2f} %)\n\n")
  rpt_file_o_f.close()
