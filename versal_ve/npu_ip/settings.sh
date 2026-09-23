#!/bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

DEFAULT_NPU_IP=VE2802_NPU_IP_O00_A304_M3
DEFAULT_NPU_TAIL=NPU_TAIL_YOLO_V5
PACKAGE_PREFIX=vitis_ai_V6.3

if [ ${BASH_SOURCE[0]} == $0 ]; then
	echo "ERROR: You must source this script"
	exit 1
fi

#    -e 's/\.tgz\].*//' -e "$match_active3"
list_ip()
{
  local BASEDIR=$1
  echo "# Here are the list of the available IPs"
  echo
  local match_active=""
  local match_active2=""
  local match_active3=""
  [ "$NPU_IP" != "" ] && match_active="s/$NPU_IP\b/&    (active NPU)/"
  [ "$NPU_IP2" != "" ] && match_active2="s/$NPU_IP2\b/&    (second NPU)/"
  [ "$NPU_TAIL" != "" ] && match_active3="s/$NPU_TAIL.*/&    (active TAIL)/"
  [ "$2" = "FULL" ] && list_grep='.'
  (
    grep "\[${PACKAGE_PREFIX}.*NPU_IP.*tgz" $BASEDIR/../bin/download.bash | sed -e "s/\[${PACKAGE_PREFIX}_//" \
      -e 's/\.tgz\].*//' -e "s/$DEFAULT_NPU_IP\b/&        (default)/" -e "$match_active" -e "$match_active2"
    grep "\[${PACKAGE_PREFIX}.*NPU_TAIL.*tgz" $BASEDIR/../bin/download.bash | sed -e "s/\[${PACKAGE_PREFIX}_//" \
      -e 's/\.tgz\].*//' -e "$match_active3"
  ) | grep --color=none $list_grep
  echo
}

set_family()
{
  family="$( basename $( dirname $( cd $( dirname "${BASH_SOURCE[0]}" ) ; echo $PWD ) ) )"
  case "$family" in
    versal_ve)
      DEFAULT_NPU_IP=VE2802_NPU_IP_O00_A304_M3
      DEFAULT_NPU_TAIL=NPU_TAIL_YOLO_V5
      list_grep='^VE\|TAIL'
      ;;
    zynq)
      DEFAULT_NPU_IP=ZU_NPU_IP_1108
      DEFAULT_NPU_TAIL=
      list_grep='^ZU'
      ;;
    versal_core)
      DEFAULT_NPU_IP=VC1902_NPU_IP_1106
      DEFAULT_NPU_TAIL=
      list_grep='^VC'
      ;;
    versal_rf)
      DEFAULT_NPU_IP=VR1602_NPU_IP_1106
      DEFAULT_NPU_TAIL=
      list_grep='^VR'
      ;;
  esac
  return 0
}


print_help()
{
cat <<EOF
${BASH_SOURCE[0]} Usage:
The first source of the script will set the environment accordingly:

  - no argument
    - for the first source of the script, it will enable the performance IP $DEFAULT_NPU_IP
    - any subsequent source of the script will keep the same settings.
  - arg1 can be:
    - -h    to display this help
    - LIST  to list all the available IPs
    - NPU_IP:
  - NPU_IP: enable the NPU_IP for snapshot generation and platform compilation.
EOF
  if [ "$family" = "versal_ve" ]
  then
    cat <<EOF
  - arg2 is used to specify a second IP to build during the platform compilation.
    arg2 can be either :
    - empty to disable the second IP
    - NPU_IP2: the name of an IP
    - NPU_TAIL: tha name of the post-processing tail
EOF
  fi
cat <<EOF

Examples:

${BASH_SOURCE[0]} $DEFAULT_NPU_IP
${BASH_SOURCE[0]} $DEFAULT_NPU_IP $DEFAULT_NPU_TAIL

EOF
}

make_ip() {
  local BASEDIR=$1
  local TARGET_NPU=$2
  export TARGET_NPU=$TARGET_NPU
  if [ ! -f "$BASEDIR/${TARGET_NPU}/.dep.${PACKAGE_PREFIX}" ]
  then
    make ${TARGET_NPU} -C $BASEDIR || return 1
  elif [ "$( cat "$BASEDIR/${TARGET_NPU}/.dep.${PACKAGE_PREFIX}" )" != "$( "$BASEDIR/../bin/download.bash" ${PACKAGE_PREFIX}_${TARGET_NPU}.tgz -n )" ]
  then
    echo "WARNING downloaded IP ${TARGET_NPU} is not the latest version, please, run the following make clean command to force the redownload:"
    echo "make -C $BASEDIR clean"
  fi
  unset TARGET_NPU
  return 0
}

main() {
  local BASEDIR=$( realpath "$(dirname "${BASH_SOURCE[0]}")" )

  set_family

  if [ "$1" = "-h" ] || [ "$1" = "--help" ]
  then
    print_help
    return 0
  fi

  local ip=$1
  [ "$1" = "autolist" ] && ip=""

  local ip2=$2

  ip=$( basename "$ip" )

  if [ "$ip" = "" ] && [ "$NPU_IP" != "" ]
  then
    echo "Using already defined NPU_IP $NPU_IP"
    ip=$NPU_IP
    if [ "$ip2" = "" ] && [ "$NPU_IP2" != "" ] && [ "$NPU_TAIL" = "" ]
    then
      echo "Using already defined NPU_IP2 $NPU_IP2"
      ip2=$NPU_IP2
    fi
    if [ "$ip2" = "" ] && [ "$NPU_TAIL" != "" ] && [ "$NPU_IP2" = "" ]
    then
      echo "Using already defined NPU_TAIL $NPU_TAIL"
      ip2=$NPU_TAIL
    fi
  elif [ "${ip^^}" = "LIST" ]
  then
    list_ip $BASEDIR
    return 1
  elif [ "${ip^^}" = "FLIST" ]
  then
    list_ip $BASEDIR FULL
    return 1
  fi

#############################
##### First IP setting  #####
#############################

  [ "$ip" = "" ] && ip=$DEFAULT_NPU_IP

  if [[ "$ip" == *NPU_TAIL* ]]; then
    echo "ERROR first IP $ip can't be a TAIL"
    return 1
  fi

  export NPU_IP=$ip

  make_ip $BASEDIR $ip || return 1

  local timestamp
  timestamp=$( basename "$BASEDIR/$ip/"fpga_info_*.txt .txt | sed 's/fpga_info_/0x/' )

  if [ "${#timestamp}" != 10 ]
  then
    echo "ERROR fetching $ip, couldn't find valid fpga_info file"
    return 1
  fi
  export VAISW_SNAPSHOT_TIMESTAMP=$timestamp
  export VAISW_SNAPSHOT_BOARDNAME=$NPU_IP


#############################
##### Second IP settings ####
#############################

  unset NPU_IP2
  unset NPU_TAIL
  unset ENABLE_NPU_TAIL
  if [ "$ip2" != "" ]; then
    if [[ "$ip2" == *NPU_IP* ]]; then
      export NPU_IP2=$ip2
      local o1 o2
      o1=$(grep -o 'O[0-9]\{2\}' <<< "$NPU_IP")
      o2=$(grep -o 'O[0-9]\{2\}' <<< "$NPU_IP2")

      if [[ "$o1" != "$o2" ]]; then
          echo "WARNING : Sourcing IPs With different offset. Please make sure that the configuration aren't overlapping still !"
      else
          echo "ERROR ! Sourcing IPs with same offset $o1 !"
          return 1
      fi

      make_ip $BASEDIR $ip2 || return 1
      local timestamp2
      timestamp2=$( basename "$BASEDIR/$ip2/"fpga_info_*.txt .txt | sed 's/fpga_info_/0x/' )

      if [ "${#timestamp2}" != 10 ]
      then
        echo "ERROR fetching $ip2, couldn't find valid fpga_info file"
        return 1
      fi
      export VAISW_SNAPSHOT_TIMESTAMP2=$timestamp2
      export VAISW_SNAPSHOT_BOARDNAME2=$NPU_IP2

    elif [[ "$ip2" == *NPU_TAIL* ]]; then
      export ENABLE_NPU_TAIL=True
      export NPU_TAIL=$ip2
      make_ip $BASEDIR $ip2 || return 1

    fi
  fi

#############################
#### SW stack settings  #####
#############################


  make -s -C $BASEDIR/../tools || return 1

  . $BASEDIR/../tools/VAI_NPU_SW/settings.sh "npu_no_check"

  export VAISW_SNAPSHOT_MODE=embedded
  export VAISW_FE_MERGENONITERRELATEDGRAPH=false
  export VAISW_DEBUG_CHECKWEIGHTS=false

  export VAISW_FPGA_INFOFILE=$BASEDIR/fpga_info
  export VAISW_HOME=$(dirname $BASEDIR)

  #
  local nb_ddrs
  nb_ddrs="${NPU_IP#*_M}"
  nb_ddrs="${nb_ddrs%%_*}"
  export NB_DDRS=$nb_ddrs
}

main "$@"

# cleaning up variables
unset list_ip
unset print_help
unset make_ip
unset main
unset family
unset list_grep
unset DEFAULT_NPU_IP
unset DEFAULT_NPU_TAIL
unset PACKAGE_PREFIX

