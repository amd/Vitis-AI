
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import sys
import json
with open('scripts/networks.json', 'r') as f:
    networks = json.load(f)

if len(sys.argv) == 1:
    print(' '.join(networks.keys()))
    exit(0)

framework = sys.argv[1]

if len(sys.argv) == 2:
    print(' '.join(networks[framework].keys()))
    exit(0)

network = sys.argv[2]

for key in ["version", "shape", "maxImagesPerCore", "depth", "preProcess", "postProcess", "predictMode", "goldFile", "images", "testList", "bestPerfsOptions", "pipelineSubBatch"]:
    if key not in networks[framework][network]:
        continue
    value = str(networks[framework][network][key])
    if key != "bestPerfsOptions":
      for sRemove in ["[", "]"]:
          value = value.replace(sRemove, "")
    print(key + "='" + value + "'\n")

