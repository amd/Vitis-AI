import os
import sys
import glob

rpt_file_o = "./npu_tail_utilization.rpt"

def usage():
  print("ERROR: Missing or too many arguments.")
  print(f"USAGE: python3 npu_tail_utilization.py PATH_2_NPU_TAIL")
  print(f"")
  print(f"Report is generated in {rpt_file_o}")
  sys.exit(1)

if __name__ == "__main__":
  if (len(sys.argv) != 2):
    usage()

  tmp = f"{sys.argv[1]}/link/_x/link/vivado/vpl/prj/prj.runs/bd_pp_top_0_0_synth_1/bd_pp_top_0_0_utilization_synth.rpt"
  path_to_rpt_in = glob.glob(tmp)
  if path_to_rpt_in == []:
    print(f"ERROR: Missing build.rpt file for NPU_TAIL\n{tmp}")
    sys.exit(1)

  print(f"\n############################# ################################\n")
  print(f"  INFO: Generating npu_tail utilization based on:")
  print(f"         {path_to_rpt_in[0]}\n")
  print(f"############################# ################################\n")

  with open(path_to_rpt_in[0], 'r') as f:
    rpt_in_content = f.readlines()

  rpt_file_o_f = open(rpt_file_o, 'w')

  npu_tail_name = os.environ.get("NPU_TAIL")
  rpt_file_o_f.write("############################# ################################\n")
  rpt_file_o_f.write(f"####### Utilization NPU_TAIL: {npu_tail_name}\n")
  rpt_file_o_f.write("############################# ################################\n")

  # npu_tail has fixed NMU/NSU usage
  pl_nmu_cnt = 2
  pl_nsu_cnt = 1
  avail_pl_nmu = 0
  avail_pl_nsu = 0

  for line in rpt_in_content:
    if "CLB LUTs" in line:
      line = line.split("|")
      resource = f"LUTS       : {line[2].strip()} / {line[5].strip()} ({line[6].strip()} %)"
      rpt_file_o_f.write(resource+"\n")
      continue

    if "| Registers" in line:
      line = line.split("|")
      resource = f"Registers  : {line[2].strip()} / {line[5].strip()} ({line[6].strip()} %)"
      rpt_file_o_f.write(resource+"\n")
      continue

    if "Block RAM Tile" in line:
      line = line.split("|")
      resource = f"Block RAMs : {line[2].strip()} / {line[5].strip()} ({line[6].strip()} %)"
      rpt_file_o_f.write(resource+"\n")
      continue

    if "| URAM   " in line:
      line = line.split("|")
      resource = f"URAMs      : {line[2].strip()} / {line[5].strip()} ({line[6].strip()} %)"
      rpt_file_o_f.write(resource+"\n")
      continue

    if "DSP Slices " in line:
      line = line.split("|")
      resource = f"DSP Slices : {line[2].strip()} / {line[5].strip()} ({line[6].strip()} %)"
      rpt_file_o_f.write(resource+"\n")
      continue

    if "NOC Master 512 bit " in line:
      avail_pl_nmu = int(line.split("|")[5].strip())
      continue

    if "NOC Slave 512 bit " in line:
      avail_pl_nsu = int(line.split("|")[5].strip())
      continue

  rpt_file_o_f.write(f"PL_NMU     : {pl_nmu_cnt} / {avail_pl_nmu} ({(100*pl_nmu_cnt/avail_pl_nmu):.2f} %)\n")
  rpt_file_o_f.write(f"PL_NSU     : {pl_nsu_cnt} / {avail_pl_nsu} ({(100*pl_nsu_cnt/avail_pl_nsu):.2f} %)\n")

  rpt_file_o_f.close()
