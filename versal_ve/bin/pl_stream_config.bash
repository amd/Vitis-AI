#!/bin/bash

# Copyright (C) 2026 Advanced Micro Devices, Inc.

# this script will generate a PL stream configuration file from a snapshot directory

cat << EOF >&2
Usage:

$0 <snapshot_path>

Generate pl config file from snapshot directory.

EOF

if [ "$1" == "--v70" ]
then
  timestamp=0
  baseaddr=0x00010004
  shift 1
else
  baseaddr=0x04
  [ "$NPU_TAIL" = "" ] && echo "ERROR: please source the NPU TAIL" >&2 && exit 1

  timestamp="0x$( grep -m 1 TIMESTAMP $VAISW_HOME/npu_ip/$NPU_TAIL/xcve2802_PP_TOP_EMBEDDED_PL/src/pp_cfg.sv | sed -e  's/.*h//' -e 's/;//' )"

  echo "INFO: generating PL stream config for tail $NPU_TAIL (having timestamp $timestamp)." >&2
fi

snap=$1
shift
[ ! -d "$snap" ] && echo "ERROR: snapshot directory $snap not found" >&2 && exit 1


dir="$( find "$snap" -name "snapshot.dump.downloadInfos" )"
[ ! -e "$dir" ] && echo "ERROR unable to find snapshot file in $snap" >&2 && exit 1
dir=$( dirname $dir )

output="$(jq -r 'to_entries | map(select(.key | endswith("_CPU") or endswith("_cpu_subgraph_call"))) | .[0].value.outputs[0]' "$dir/../main.json")"

shape=""
stride=""
j=4
while read i; do
  shape="$i${shape:+, $shape}"
  stride="$j${stride:+, $stride}"
  [ "$i" == 85 ] && i=96
  j=$(( $j * $i ))
done < <(jq -r ".\"$output\".params.shape | reverse | map(tostring) | join(\"\\n\")" "$dir/../main.json")

cat <<EOF
{
  "timestamp" : $(printf %d $timestamp),
  "inputs": {
EOF

first=true
addr=0x20100000000
jq -r 'to_entries[] | "\(.value.coreBlocks[0].nbuf_idx) \(.key) \(.value.coreBlocks[0].size)"' "$dir/snapshot.dump.downloadInfos" | sort -n | while read i; do
  name=$(cut -d' ' -f2 <<< "$i")
  offset=$(cut -d' ' -f3 <<< "$i")
  $first || echo ","
  printf "    \"%s\": [\"0x%011x\"]" $name $addr
  addr=$(( $addr + $offset ))
  first=false
done

cat <<EOF

  },
  "outputs": {
    "$output": {
      "shape" : [$shape],
      "strides" : [$stride],
      "type" : "FLOAT32",
      "baseaddr" : "$baseaddr",
      "offset" : ["0x04"]
    }
  }
}
EOF
