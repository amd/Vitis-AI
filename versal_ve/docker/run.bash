#! /bin/bash

# ===========================================================
# installation helper
#
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================
usage() {
  cat <<EOF

######################################################################
#   $0 - build and launch a VAISW image using docker
######################################################################

Usage:

$0 [docker_run_argument] -- [commands]
  -h,--help     this help
  --runOnly     skip steps and only run the image
  --fromImg     use a tgz file to build the docker image
  --noFromImg   build all the image from the dockerFile
  --noFromHub   don't use docker hub. This can be used to rebuild the base part of the docker image.
  --noFpga      do not check for FPGA
  --dockerfile  specify another docker file to build and run
  --acceptLicense do not prompt for the license agreement.
  --noGpu         disable the use of a GPU. By default, GPU env is enabled if a GPU is found.
Where:
    docker_run_argument are the arguments to pass to docker run command (like -v /path:/mount)
    commands            are the commands to run instead of having an interactive shell

For instance:

$0 -v /home/my_user/DATA/my_image_path:/IMAGE                                             # to mount the local my_imagepath directory in /IMAGE
$0 -- /bin/bash '. npu_ip/settings.sh && make -C examples/python_examples/ssdResnet34 '   # to run a command within the docker
$0 --runOnly --name httpd httpd                                                           # to run an httpd docker
EOF
}

docker_dir=$( realpath $( dirname ${BASH_SOURCE[0]} ))
docker_run_arg=()
docker_run_cmd='/bin/bash'
runOnly=false
fromImg=true
fromHub=true
noFromImg=false
noFpga=false
noGpu=false
askLicense=true
extVenv=
DEFFILE=vaisw.def
DOCKERFILE=vaisw.dockerfile
DOCKER_HUB=amdih/vitis-ai
DOCKER_IMG=V5.1_u2404
DOCKER_IMG_FILE=docker_vai_npu_image_V5.1_u2404.7bd5c332e6ea.tgz

while [ $# -gt 0 ]
do
    if [ "$1" = '--' ]
    then
        shift
        docker_run_cmd=''
        break
    elif [ "$1" = "--help" -o "$1" = "-h" ]
    then
      usage
      exit 0
    elif [ "$1" = "--acceptLicense" ]
    then
      shift
      askLicense=false
      continue
    elif [ "$1" = "--extVenv" ]
    then
      extVenv=$2
      shift 2
      continue
    elif [ "$1" = "--runOnly" ]
    then
      shift
      runOnly=true
      docker_run_cmd=''
      break
    elif [ "$1" = "--fromImg" ]
    then
      shift
      fromImg=true
      continue
    elif [ "$1" = "--noFromHub" ]
    then
      shift
      fromHub=false
      continue
    elif [ "$1" = "--noFromImg" ]
    then
      shift
      noFromImg=true
      continue
    elif [ "$1" = "--noFpga" ]
    then
      shift
      noFpga=true
      continue
    elif [ "$1" = "--noGpu" ]
    then
      shift
      noGpu=true
      continue
    elif [ "$1" = "--dockerfile" ]
    then
      shift
      DOCKERFILE=$1
      noFromImg=true
      shift
      continue
    fi
    docker_run_arg+=("$1")
    shift
done

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'
ERROR="${RED}ERROR${NC}"
OK="${GREEN}OK${NC}"

APT=false
which apt &> /dev/null && APT=true

echo_error() {
  echo
  echo "============================================================"
  echo
  echo -e "$ERROR: $@"
}
echo_ok() {
  echo -e "$OK: $@"
}

fpga_check() {

  [ "$VAISW_SNAPSHOT_TIMESTAMP" != "" ] && echo "Running in offline mode, no FPGA check" && return

  # TODO: do we want to list all the supported devices
  # we should.. but we should take the list from the kernel driver
  # lspci may be placed in /usr/sbin
  # and modinfo may be placed in /sbin
  PATH=$PATH:/sbin:/usr/sbin
  if lspci -n -d 10ee: | grep '10ee:8' &> /dev/null
  then
    echo_ok "Xilinx board detected"
  else
    echo_ok "No FPGA board detected, assuming offline mode will be used."
    return
  fi

  ABI="$( modinfo -F vaisw_ABI vaisw )"

  ABI_MAJOR_INSTALL=$( grep MPDD_ABI_MAJOR_VERSION $VAISW_INSTALL_DIR/drivers/mpdd_mapping.h | sed -e 's/"$//' -e 's/.*"//' )
  ABI_MINOR_INSTALL=$( grep MPDD_ABI_MINOR_VERSION $VAISW_INSTALL_DIR/drivers/mpdd_mapping.h | sed -e 's/"$//' -e 's/.*"//' )

  if [ "$ABI" != "$ABI_MAJOR_INSTALL.$ABI_MINOR_INSTALL" ]
  then
    make -C $VAISW_INSTALL_DIR/drivers clean all install || exit 1
  fi

  echo_ok "vaisw kernel module properly installed"

  echo
  # maybe, vaisw_tools is not compile to run with the libc of the host
  if VAISW_LOG_ENABLE=false vaisw_tools --help &> /dev/null
  then
    echo "Testing board communication"
    # TODO: better check of vaisw_tools --config
    (
      . $VAISW_INSTALL_DIR/settings.sh tensorflow INT8
      export VAISW_LOG_ENABLE=false
      vaisw_tools --config
    )
    if [ "$?" != 0 ]
    then
      echo_error "unable to communicate with the FPGA board"
      echo "Please, make sure the proper bitstream image is loaded"

      echo "refer to the error message above"
      echo "if the error still occurs, please, contact the support"
      exit 1
    else
      echo_ok "communication with the board successful"
    fi
  fi
}

docker_license() {

  $askLicense || return

  prompt_file=PROMPT/PROMPT.txt

  cat $prompt_file

  echo "Do you agree the license term (y/n)"
  while true
  do
    read -n 1 -s -r reply
    [ "${reply,,}" = "y" ] && break
    [ "${reply,,}" = "n" ] && exit 0
  done

}

docker_check() {
  echo
  echo "Testing docker"
  if ! which docker &> /dev/null || ! docker build --help | grep -q -- '--target'
  then
    if which docker &> /dev/null
    then
      echo_error "docker version too old. Please, remove any previous version."
      ! $APT && echo "sudo yum remove docker*"
      echo "Then, try installing the latest version using the command:"
    else
      echo_error "docker command not found. Please, install using the command:"
    fi
      $APT && echo "sudo apt update && sudo apt install -y docker.io"
    ! $APT && echo "curl -fsSL https://get.docker.com/ | sh && sudo systemctl start docker && sudo systemctl enable docker"
    exit 1
  fi

  if ! docker ps &> /dev/null
  then
    if ! ps aux | grep -q dockerd &> /dev/null
    then
      echo_error "docker not running. Please, try the following command"
      echo "sudo systemctl start docker"
      exit 1
    fi
    if [ "$( getent group docker )" = "" ]
    then
      echo_error "docker permission denied. Please, use the following command to have access to docker from your user"
      echo "sudo chmod a+rw /var/run/docker.sock"
      exit 1
    fi
    if getent group docker | grep -q "\b$USER\b"
    then
      echo_error "docker permission denied. Please, don't forget to logout completely from $HOSTNAME (terminate X session) and login again so the group update is taken into account"
      exit 1
    fi
    echo_error "docker permission denied. Please, make sure to be in the docker group. Use the following command, then logout and login again"
      $APT && echo "sudo adduser $USER docker"
    ! $APT && echo "sudo usermod -a -G docker $USER"
    exit 1
  fi
}

docker_build() {

  if ! $noFromImg && ! $fromImg
  then
    # automatically set fromImg if an image is found
    nb_img="$( ls ../docker.image/*vai_npu_image*.tgz 2> /dev/null | wc -l )"
    [ "$nb_img" = "1" ] && fromImg=true
  fi
  if $fromHub
  then
    DOCKERFILE=vaisw_from_hub.dockerfile
    if [ ! -f "$DOCKERFILE" ]
    then
      if ! awk "/^FROM all_common AS all/{p=1;print \"FROM $DOCKER_HUB:$DOCKER_IMG\";next}p" < vaisw.dockerfile > $DOCKERFILE
      then
        echo_error "problem in generating $DOCKERFILE"
        which awk &> /dev/null && echo_error "command awk not found"
        exit 1
      fi
    fi
  elif $fromImg
  then
    nb_img="$( ls ../docker.image/*vai_npu_image*.tgz 2> /dev/null | wc -l )"
    if [ "$nb_img" = "0" ]
    then
      mkdir -p ../docker.image
      if ! ( cd  $VITIS_AI_REPO/docker.image && $VITIS_AI_REPO/bin/download.bash $DOCKER_IMG_FILE --symlink )
      then
        echo "ERROR downloading docker image"
        exit 1
      fi
      echo "$DOCKER_IMG_FILE successfully downloaded"
      nb_img=1
    fi
    [ "$nb_img" != "1" ] && echo "ERROR, several docker images found in $VAISW_INSTALL_DIR/examples/docker.image. Please, keep only one image in the directory." && exit 1
    vaisw_image="$( ls ../docker.image/*vai_npu_image*.tgz 2> /dev/null )"
    # get version
    vaisw_image_full="$( basename "$vaisw_image" .tgz | sed 's/.*vai_npu_image.//' )"
    vaisw_hash="$( echo "$vaisw_image_full" | sed 's/.*\.//' )"
    vaisw_image_version="${vaisw_image_full/.$vaisw_hash}"
    if docker inspect vai_npu_image:$vaisw_image_version &> /dev/null && \
      docker images vai_npu_image:$vaisw_image_version | grep -q $vaisw_hash
    then
      echo "Docker image is already loaded with correct hash $vaisw_hash"
    else
      docker inspect vai_npu_image:$vaisw_image_version &> /dev/null && echo "Removing previous image with wrong hash" && docker rmi vai_npu_image:$vaisw_image_version
      echo "Loading docker image $vaisw_image:$vaisw_image_version"
      if zcat $vaisw_image | docker load
      then
        echo "loading completed"
      else
        echo_error "problem in loading the docker image"
        exit 1
      fi
    fi
    docker tag vai_npu_image:$vaisw_image_version vai_npu_image
    echo "For info, the following docker images exists in docker:"
    docker images vai_npu_image
    DOCKERFILE=vaisw_from_img.dockerfile
    if [ ! -f "$DOCKERFILE" ]
    then
      if ! awk '/^FROM all_common AS all/{p=1;print "FROM vai_npu_image";next}p' < vaisw.dockerfile > $DOCKERFILE
      then
        echo_error "problem in generating $DOCKERFILE"
        which awk &> /dev/null && echo_error "command awk not found"
        exit 1
      fi
    fi
  fi
  dockertag=$( basename $DOCKERFILE .dockerfile )
  echo "building docker $dockertag using dockerfile $( basename $DOCKERFILE ) ($DOCKERFILE)"

  if [ "$( id -u )" = "0" ]
  then
    echo_error "Launching doker using root user is not supported, please use a real user"
    exit 1
  fi

  if ! HOME=/tmp/tmp.$USER docker build --tag=$dockertag:$UID --build-arg UID=$UID --build-arg USERNAME=$USER --build-arg USERHOME=$HOME -f $DOCKERFILE .
  then
    echo_error "building docker image."
    echo "Please, review above commands and contact the support"
    exit 1
  fi

  echo_ok "docker image built without error"
}

docker_run() {
  video_device=$( find /dev -name 'video*' \( ! -writable -or ! -readable \) )
  [ "$video_device" != "" ] && echo "Giving permission to access USB webcam" && sudo chmod a+rw $video_device

  # by default we keep the same net host to be able to use open ssh X11 connection
  docker_arg="--net=host"
  # if the X11 is local, we mount also the X11 local sockets
  [ "${DISPLAY:0:1}" = ":" ] && docker_arg="$docker_arg -v /tmp/.X11-unix:/tmp/.X11-unix"

  if [ -e /dev/video0 ]
  then
    videoGroup=$( stat -c '%G' /dev/video0 )
    groups | grep -q "\b$videoGroup\b" && docker_arg="$docker_arg --group-add=$( getent group $videoGroup | cut -d: -f3 )"
  fi
  for d in examples/models examples/datasets examples/VIDEO bitstream
  do
    [ -h $VAISW_INSTALL_DIR/$d ] && docker_arg="$docker_arg -v $( realpath $VAISW_INSTALL_DIR/$d ):$( readlink $VAISW_INSTALL_DIR/$d )"
  done

  TTY=''
  tty &> /dev/null && TTY='-t'


  if [ "$VITIS_AI_REPO" != "" ]
  then
    # workdir is current dir in case it is a subdir of VITIS_AI_REPO
    workdir=$VITIS_AI_REPO
    [ "$VITIS_AI_REPO_UP" = "${original_directory:0:${#VITIS_AI_REPO_UP}}" ] && workdir=$original_directory

    for d in examples/python_examples/batcher/models examples/python_examples/batcher/datasets
    do
      [ -h $VITIS_AI_REPO/$d ] && docker_arg="$docker_arg -v $( realpath $VITIS_AI_REPO/$d ):$( readlink $VITIS_AI_REPO/$d )"
    done

    mkdir -p $VAISW_INSTALL_DIR/.amd
    docker_history=$VAISW_INSTALL_DIR/.amd/vaisw/bash_docker_history
    [ -f $HOME/.Xauthority ] && docker_arg="$docker_arg -v $HOME/.Xauthority:$HOME/.Xauthority:ro"
    docker_arg="$docker_arg -v $VAISW_INSTALL_DIR/.amd:$HOME/.amd -v $VITIS_AI_REPO_UP:$VITIS_AI_REPO_UP"

    docker_arg="$docker_arg -w $workdir -e VITIS_AI_REPO=$VITIS_AI_REPO"
    docker_arg="$docker_arg -e VAISW_LOG_DIRECTORY=$VAISW_INSTALL_DIR/.amd/vaisw/log"
    docker_arg="$docker_arg -e VAISW_REPORTING_DIRECTORY=$VAISW_INSTALL_DIR/.amd/vaisw/reporting"
    docker_arg="$docker_arg -v $docker_history:/$HOME/.bash_history"
    [ "$NPU_IP" != "" ] && docker_arg="$docker_arg -e NPU_IP=$NPU_IP"
    [ "$NPU_IP_FALLBACK" != "" ] && docker_arg="$docker_arg -e NPU_IP_FALLBACK=$NPU_IP_FALLBACK"
    [ "$USE_DOWNLOAD_DIR" != "" ] && docker_arg="$docker_arg -e USE_DOWNLOAD_DIR=$USE_DOWNLOAD_DIR"
  else
    mkdir -p ~/.amd/vaisw
    docker_history=~/.amd/vaisw/bash_docker_history
    [ -f $HOME/.Xauthority ] && docker_arg="$docker_arg -v $HOME/.Xauthority:/home/demo/.Xauthority:ro"
    docker_arg="$docker_arg -v $VAISW_INSTALL_DIR:/home/demo/vaisw \
      -v $docker_history:/home/demo/.bash_history \
      -v $( realpath ~/.amd/vaisw/log ):/home/demo/.amd/vaisw/log \
      -v $( realpath ~/.amd/vaisw/reporting ):/home/demo/.amd/vaisw/reporting"

    docker_arg="$docker_arg -w /home/demo"
  fi

  if $noGpu
  then
    docker_arg="$docker_arg -e CUDA_VISIBLE_DEVICES=-1"
  else
    if [ -c /dev/kfd ]
    then
      # Use torch+rocm instead of torch+cuda
      docker_arg="$docker_arg -e AMD_GPU=1"

      # Expose GPU-related devices
      docker_arg="$docker_arg --device /dev/kfd --device /dev/dri"
      docker_arg="$docker_arg --security-opt seccomp=unconfined" # Optional

      # Add video and render group
      docker_arg="$docker_arg --group-add video"
      docker_arg="$docker_arg --group-add $( getent group render | cut -d: -f3 )"

      # Mount ROCm utilities
      docker_arg="$docker_arg -v ${ROCM_DIR:-/opt/rocm}:/opt/rocm"
    fi
  fi
  if [ "$extVenv" != "" ]
  then
    docker_arg="$docker_arg -e EXT_VENV=$extVenv -v $extVenv:$extVenv"
    deps=$( find $extVenv/lib -maxdepth 3 -name "base_venv.pth" 2> /dev/null )
    if [[ "$deps" != "" ]]
    then
      while read -r venv_dep
      do
        docker_arg="$docker_arg -v $venv_dep:$venv_dep"
      done < <( cat $deps )
    fi
  fi

  mkdir -p $( dirname $docker_history )
  [ -f $docker_history ] || touch $docker_history

  docker run --shm-size=8gb --privileged --rm -i $TTY \
    --log-driver none \
    -e QT_X11_NO_MITSHM=1 \
    \
    -e DISPLAY=$DISPLAY \
    $docker_arg \
    \
    "${docker_run_arg[@]}" \
    $dockertag:$UID ${docker_run_cmd} "$@"
}

original_directory=$PWD

cd $docker_dir

LOCAL_VAISW_INSTALL_DIR=$( cd ../../ ; pwd )
if [ "$VAISW_INSTALL_DIR" = "" ]
then
  if [ -d ../npu_ip ]
  then
    [ ! -d ../tools/VAI_NPU_SW ] && ! make -C ../tools && exit 1
    VAISW_INSTALL_DIR="$( realpath $PWD/../tools/VAI_NPU_SW )"
  else
    VAISW_INSTALL_DIR="$LOCAL_VAISW_INSTALL_DIR"
  fi
else
  [ "$LOCAL_VAISW_INSTALL_DIR" != "$VAISW_INSTALL_DIR" ] && [ ! -d ../npu_ip ] &&
    echo_error "wrong environment: $VAISW_INSTALL_DIR. Unless you are doing something special, please source environment $LOCAL_VAISW_INSTALL_DIR"
fi

VITIS_AI_REPO=
[ $( basename "$VAISW_INSTALL_DIR" ) = "VAI_NPU_SW" ] && VITIS_AI_REPO=$( realpath "$VAISW_INSTALL_DIR/../.." ) && VITIS_AI_REPO_UP=$( realpath "$VITIS_AI_REPO/.." )


if $runOnly
then
    docker_check
    docker run "$@"
else
    $noFpga || fpga_check
    docker_check
    docker_license
    docker_build
    docker_run "$@"
fi
