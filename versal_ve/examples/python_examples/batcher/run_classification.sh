#! /bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


BASEDIREXAMPLES=$PWD

export GLOG_minloglevel=1


# constants
DEMO_PATH=.
DEFAULT_FRAMEWORK="tensorflow2"
DEFAULT_NETWORK="resnet50"


# default values
framework=$DEFAULT_FRAMEWORK
network=$DEFAULT_NETWORK
nbImages=-1
nbCores=6
userImages="false"
predictFile="predict.log"
usePython=true
useLabelImage=false
noBestParams=false
pipelining=false
snapshot=false
skipDoSnapshot=false
batchCount=10
pipelineDepth=0
batchSizePerCore=""
noPipInstall=false

get_framework_list()
{
    python3 $BASEDIREXAMPLES/scripts/network_params.pl
}
get_network_list()
{
    python3 $BASEDIREXAMPLES/scripts/network_params.pl $framework
}

# extract run options args
print_usage () {
    printf "\033[1mNAME\033[0m\n"
    printf "      $(basename $0) - run Neural Network image classification\n\n"
    printf "\033[1mSYNOPSIS\033[0m\n"
    printf "      \033[1m$(basename $0)\033[0m [options]\n\n"
    printf "\033[1mOPTIONS\033[0m\n"
    printf "      -f, --framework name\n"
    printf "            Select the framework, \033[1mname\033[0m must be \033[1mlist of $( get_framework_list )\033[0m (default value: ${DEFAULT_FRAMEWORK}).\n"
    printf "      -P, --parallel mode (not implemented on VERSAL)\n"
    printf "            Enable multijob \033[1mmode\033[0m is either \033[1mboard\033[0m or \033[1msystem\033[0m or \033[1mcore\033[0m\n"
    printf "            Note that, in parallel mode network can be a colon separated listm for instance \033[1mresnet50:resnet152\033[0m\n\n"
    printf "      -s, --snapshot (not implemented on VERSAL)\n"
    printf "            Enable snapshot mode: dump the snapshot and runs the inference\n\n"
    printf "          --snapshotRun\n"
    printf "            Enable snapshot mode: runs the inference using a previously saved snapshot\n\n"
    printf "      -n, --network name\n"
    printf "            Select the network, \033[1mname\033[0m must be \033[1mlist or $( get_network_list )\033[0m (default value: ${DEFAULT_NETWORK}).\n\n"
    printf "      -b, --batchSize B\n"
    printf "            Force the size of the batch to \033[1mB\033[0m\n\n"
    printf "          --batchSizePerCore B\n"
    printf "            Force the size of the batch to \033[1mB * nbNpuCore\033[0m\n\n"
    printf "      -B, --batchCount B\n"
    printf "            Force the number the batch to \033[1mB\033[0m\n\n"
    printf "          --pipelineDepth p\n"
    printf "            Compute the batch size using pipelineSubBatch and pipelineDepth (default value $pipelineDepth)\n\n"
    printf "      -N, --nbImages N\n"
    printf "            Number \033[1mN\033[0m of images to process.\n\n"
    printf "      -d, --imagesDir dir\n"
    printf "            Directory \033[1mdir\033[0m where to find images to process (default is \".\").\n\n"
    printf "          --predictFile filename\n"
    printf "            File to save predictions (default is \"$predictFile\").\n\n"
    printf "          --labelImage\n"
    printf "            Use label_image instead of the Python batcher. Only implemented using tensorflow.\n\n"
    printf "          --noBestParams\n"
    printf "            Disable the best parameters preset for the running network.\n\n"
    printf "          --pipelining\n"
    printf "            Enable pipelining.\n\n"
    printf "          --noPipInstall\n"
    printf "            Disable the automatic installation of any missing python dependencies.\n\n"
    printf "      --  \033[1m[options]\033[0m\n"
    printf "            Pass options directly to the batcher.\n"
    printf "      -h, --help\n"
    printf "            Display this help.\n"
}

TEMP=`getopt -o f:n:N:d:hP:sb:B: --long framework:,network:,nbImages:,imagesDir:,parallel,labelImage,noBestParams,noPipInstall,snapshot,snapshotRun,predictFile:,batchSize:,batchCount:,batchSizePerCore:,help -n $0 -- "$@"`
ret_code=$?
if [ $ret_code != 0 ];
then
    print_usage
    exit 1
fi

eval set -- "$TEMP"


# update variables
while true;
do
    case "$1" in
        -f|--framework)
            case "$2" in
                "") shift 2 ;;
                *)
                    framework=$2
                    shift 2
                    if [ "$framework" = "list" ]
                    then
                        echo "List of supported framework:"
                        get_framework_list
                        exit 0
                    fi
                    ;;
            esac ;;
        -n|--network)
            case "$2" in
                "") shift 2 ;;
                *)
                    network=${2,,}
                    if [ "$network" = "list" ]
                    then
                        echo "List of supported network for $framework:"
                        get_network_list
                        exit 0
                    fi
                    shift 2
                    ;;
            esac ;;
        -N|--nbImages)
            case "$2" in
                "") shift 2 ;;
                *) nbImages=$2 ; shift 2 ;;
            esac ;;
        -b|--batchSize)
            case "$2" in
                "") shift 2 ;;
                *) batch=$2 ; shift 2 ;;
            esac ;;
        --batchSizePerCore)
            case "$2" in
                "") shift 2 ;;
                *) batchSizePerCore=$2 ; shift 2 ;;
            esac ;;
        -B|--batchCount)
            case "$2" in
                "") shift 2 ;;
                *) batchCount=$2 ; shift 2 ;;
            esac ;;
        -d|--imagesDir)
            case "$2" in
                "") shift 2 ;;
                *) imagesDir=$2 ; userImages=true ; shift 2 ;;
            esac ;;
        -P|--parallel)
            useLabelImage=true # works only with label Image so far
            case "$2" in
                "") shift 2 ;;
                *) parallelMode=${2,,} ; shift 2 ;;
            esac ;;
        -s|--snapshot)
            snapshot=true
            shift
            ;;
        --snapshotRun)
            snapshot=true
            skipDoSnapshot=true
            shift
            ;;
        --predictFile)
            case "$2" in
                "") shift 2 ;;
                *) predictFile=$2 ; shift 2 ;;
            esac ;;
        --labelImage)
            useLabelImage=true
            shift
            ;;
        --noBestParams)
            noBestParams=true
            shift
            ;;
        --pipelining)
            pipelining=true
            shift
            ;;
        --noPipInstall)
            noPipInstall=true
            shift
            ;;
        --) shift ; break ;;
        *) print_usage; exit 1;;
    esac
done

batcher=$BASEDIREXAMPLES/batcher_py/${framework}_batcher.py

# the below function will set from the json the below bash variables
# preProcess postProcess shape maxImagesPerCore depth predictMode goldFile images testList version bestPerfsOptions pipelineSubBatch
# FIXME: easy support of multinetwork (for python batcher): limitation is we use the same json parameters from the first network
network_params=$( python3 $BASEDIREXAMPLES/scripts/network_params.pl $framework ${network%%:*} )
[ "$?" != "0" ] && echo "ERROR: model $network is not supported" && exit 1
eval "$network_params"

if [[ "$imagesDir" == "" ]]; then
    imagesDir="datasets/$images"
    goldFile="datasets/$goldFile"
    DATA_PATH="$( dirname "$imagesDir" )"
    if [[ "$predictMode" == "TOP_N" ]]; then
        goldFile=$DATA_PATH/ILSVRC_2012_val_GroundTruth_10p.txt
    fi
fi


# compute batchSize/nbImages
if [[ ! -z $VAISW_INSTALL_DIR ]]
then
    if [[ $(uname -m) = "x86_64" ]]
    then
        OUTPUT="$(VAISW_SNAPSHOT_MODE=none $VAISW_INSTALL_DIR/bin/vaisw_tools --config)"
        boards=$(echo "${OUTPUT}"  | grep -m 1 -o "boards *:  [0-9]"  | rev | cut -d " " -f 1 | rev )
        systems=$(echo "${OUTPUT}" | grep -m 1 -o "systems *:  [0-9]" | rev | cut -d " " -f 1 | rev )
        cores=$(echo "${OUTPUT}"   | grep -m 1 -o "cores *:  [0-9]"   | rev | cut -d " " -f 1 | rev )

        if [ -z $boards ] || [ -z $systems ] || [ -z $cores ];
        then
            echo "${OUTPUT}"
            echo ""
            echo "Cannot get number of systems/cores."
            exit 1
        fi

        nbCores=$( echo "$OUTPUT" | grep -m -1 -o "enabled cores.*" | cut -d : -f 2 | awk -F "," '{sum=0; for(i=1; i<=NF;i++) sum += $i; print sum}' )
        if ! [[ "${nbCores}" =~ ^[0-9]+$ ]];
        then
            echo "Cannot get number of boards/systems/cores."
            exit 1
        fi

        for((b=0;b<$boards;b++))
        do
            all_board="$all_board B${b}"
            for((s=0;s<$systems;s++))
            do
                all_system="$all_system B${b}_S${s}"
                for((c=0;c<$cores;c++))
                do
                    all_core="$all_core B${b}_S${s}_C${c}"
                done
            done
        done

    else
        OUTPUT="$( vart_ml_tools config )"
        if echo "$OUTPUT" | grep -q AIEML_V1C
        then
            nbCores=$( echo "$OUTPUT" | grep -m 1 '^AIE ' | sed -e 's/.* = //' -e 's/x.*//' )
            nbCores=$(( $nbCores / 2 ))
        else
            boards=$(  echo "$OUTPUT" | grep -m 1 boards  | sed 's/.* //' )
            systems=$( echo "$OUTPUT" | grep -m 1 systems | sed 's/.* //' )
            cores=$(   echo "$OUTPUT" | grep -m 1 cores   | sed 's/.* //' )
            nbCores=$(( $boards * $systems * $cores ))
        fi
    fi

    # disable pipelineSubBatch from cmdline
    [ "$pipelineDepth" = "0" ] && pipelineSubBatch=0
    if [ "$batchSizePerCore" = "" ]
    then
        if [ "$pipelineSubBatch" != "" ] && [ "$pipelineSubBatch" != "0" ]
        then
            batchSizePerCore=$(( $pipelineSubBatch * $pipelineDepth ))
        else
            batchSizePerCore=${maxImagesPerCore:=4}
            # force a batchSizePerCore of 1
            batchSizePerCore=1
        fi
    fi
    batchSize=$(($batchSizePerCore * $nbCores))

else
    batchSize=50
fi

[ "$batch" != "" ] && batchSize=$batch

if [[ $nbImages -eq -1 ]];
then
    nbImages=$(($batchSize * $batchCount))
elif [[ $nbImages -ne "all" ]];
then
    if [[ $nbImages -lt $batchSize ]];
    then
        batchSize=$nbImages
    fi
fi

# no pip install on ARM, we ask customer to install manually the necessary dependencies
[ "$( uname -m )" = "aarch64" ] && noPipInstall=true

# no pip install on Mipso docker
[ "$MIPSO_DOCKER" = "1" ] && noPipInstall=true

if ! $noPipInstall && [ ! -f $BASEDIREXAMPLES/.pip_install_done ]
then
    # install python dependencies for SW stack and for batcher

    python3 -m pip install --user --upgrade pip || exit 1
    python3 -m pip install --user -r $BASEDIREXAMPLES/../doc/requirements.txt -r requirements.txt || exit 1

    touch $BASEDIREXAMPLES/.pip_install_done
fi

if [ "$framework" = "pytorch" ]
then
    unset LD_PRELOAD
    for n in ${network//:/ }
    do
        modelDir=$BASEDIREXAMPLES/models/$n/$framework
        if [ ! -e $modelDir/network${version:+.}${version} ]
        then
            echo "$modelDir not found, trying to download"
            ./scripts/download_torchHub_model.sh $n ${version} || exit 1
        fi
    done
fi

if [ "$framework" = "tensorflow" ]
then
    for n in ${network//:/ }
    do
        modelDir=$BASEDIREXAMPLES/models/$n/$framework
        if [ ! -e $modelDir/network${version:+.}${version} ]
        then
            echo "$modelDir not found, trying to download"
            ./scripts/download_tensorflow_model.sh $n || exit 1
        fi
    done
fi

if [ "$framework" = "tensorflow2" ]
then
    for n in ${network//:/ }
    do
        modelDir=$BASEDIREXAMPLES/models/$n/$framework
        if [ ! -e $modelDir/network${version:+.}${version} ]
        then
            echo "$modelDir not found, trying to download"
            ./scripts/download_tensorflow2_model.sh $n || exit 1
        fi
    done
fi

if [ "$framework" = "onnxRuntime" ]
then
    heightwidth=${shape[0]#*,}
    heightwidth=${heightwidth//, /x}
    for n in ${network//:/ }
    do
        modelDir=$BASEDIREXAMPLES/models/$n/$framework
        if [ ! -e $modelDir/network${version:+.}${version} ]
        then
            echo "$modelDir not found, trying to download"
            ./scripts/download_onnx_model.sh $n ${version} ${heightwidth} || exit 1
        fi
    done
fi

# modelDir is only used with batcher Python without multiple networks
modelDir=$BASEDIREXAMPLES/models/$network/$framework

# download non-user images
if ! $userImages ;
then
    echo "+----------------------------+"
    echo "|      DOWNLOAD IMAGES       |"
    echo "+----------------------------+"
    printf "\n"
    if test -d $imagesDir;
    then
        echo Images have already been downloaded into $DATA_PATH
    else
        mkdir -p $DATA_PATH
        if [[ "$predictMode" == "TOP_N" ]]; then
            python3 $BASEDIREXAMPLES/scripts/download_ILSVRC12.py $DATA_PATH || exit 1
        elif [[ "$predictMode" == "BOXED" && "$postProcess" == "Yolov1" ]]; then
            $BASEDIREXAMPLES/scripts/download_PascalVOC.sh $DATA_PATH || exit 1
            python3 $BASEDIREXAMPLES/scripts/createGold_PascalVOC.py $DATA_PATH/VOC2007/Annotations $goldFile || exit 1
        elif [[ "$predictMode" == "COCO" ]]; then
            $BASEDIREXAMPLES/scripts/download_Coco.sh $DATA_PATH || exit 1
        fi
    fi
fi

if [[ "$predictMode" == "TOP_N" ]]; then
    mode="CreateGold"
else
    mode="default"
fi

# run batcher
echo ""
echo "***********************************************"
printf "Running \033[1m$framework\033[0m framework image classification with \033[1m$network\033[0m model.\n"
echo "***********************************************"

echo "Running testbench ..."

export VAISW_RUNSESSION_NETWORKNAME=$network

if ! $noBestParams
then
    # setting SW stack environment in case bestPerfsOptions is set
    for config in $bestPerfsOptions
    do
        option=${config%%=*}
        opt=${option^^}
        opt=${opt/./_}
        if [ "$config" = "$option" ]
        then
            unset VAISW_${opt}
        else
            value=${config##*=}
            export VAISW_${opt}=$value
        fi
    done
fi

if $pipelining
then
    [ "$pipelineSubBatch" != "" ] && export VAISW_RUNSESSION_PIPELINESUBBATCH=$pipelineSubBatch
fi

$userImages || extra_args="--goldFile $goldFile"
if $useLabelImage && [ $framework == "tensorflow" ]
then
    if [ "$color" != "BGR" ]
    then
        echo
        echo "WARNING: neural network input is $color while label_image supports only $BGR"
        echo "accuracy will be reduced"
        echo
    fi
    [ "$predictMode" != "TOP_N" ] && extra_args="$extra_args --noLabels"
    CMD="python3 ./label_image.py \
        --imgPath $imagesDir \
        --predictFile $predictFile \
        --nbImages $nbImages \
        $extra_args \
        --batchSize $batchSize \
        --net_json scripts/networks.json \
        $network"
else
    [ "$predictMode" = "TOP_N" ] && predictMode=TOP_N_DEMO
    [[ "$version" != "" ]] && extra_args="$extra_args --networkVersion $version"
    CMD="$batcher \
        --mode Gold \
        --modelPath $modelDir \
        --imgPath  $imagesDir \
        $extra_args \
        --testList $testList \
        --demoPath $DEMO_PATH \
        --imageShape $(echo $shape | tr ',' ' ') \
        --preProcess ${preProcess} \
        --postProcess ${postProcess} \
        --predictMode ${predictMode} \
        --predictFile $predictFile \
        --nbImages $nbImages \
        --batchSize $batchSize"
fi

if [ "$parallelMode" = "" ]
then
    echo $CMD $@
    if $snapshot
    then
        $skipDoSnapshot || { VAISW_SNAPSHOT_MODE=txt $CMD "$@" || exit 1 ; }
        if $useLabelImage
        then
            CMD="$CMD --preLoadImages --snapshotRunner --snapshotSplit system --pipeline --repeat 10 --batchSize 1"
            fps=$( python3 -c "import sys,json ; print(int(0.95*list(json.load(sys.stdin).values())[0]['summary']['img per sec']))" < reporting.json )
            [ "$fps" != "" ] && CMD="$CMD --pipelineFps $fps"
            echo "Now, running with snapshot pipeline mode at $fps"
        else
            CMD=$(sed s/tensorflow_batcher/embedded_batcher/ <<< $CMD)
        fi
        echo $CMD $@
    fi
    $CMD "$@" || exit 1
else
    if ! xhost &> /dev/null
    then
        echo "parallelMode will create terminals"
        echo "Please make sure you have access to the X11 display $DISPLAY"
        exit 1
    fi
    all="all_$parallelMode"
    all=${!all}
    count=$( echo "$all" | wc -w )
    if [ "$count" = "0" ]
    then
        echo "ERROR wrong parallel mode: $parallelMode"
        exit 1
    fi

    root_geometry=$( xwininfo -root | grep geometry | tail -n 1 | sed -e s'/+.*//' -e 's/.* //' )
    # let's map 4 terminal horizontally

    xpos=0
    ypos=0
    xinc=$(( ${root_geometry%%x*} / 4 ))
    yinc=$(( ${root_geometry##*x} / ( ($count+3)/4) ))

    bgProcess=()
killSubProcess()
{
    echo "Detect CTRL-C. Killing sub processes: ${bgProcess[@]}"
    kill ${bgProcess[@]}
}
    for core in ${all}
    do
        # process the colon separated list
        n=${network%%:*}
        network=${network#*:}
        [ "$network" = "" ] && network=$n
        [ "$n" = "" ] && n=$DEFAULT_NETWORK

        echo "Running $n on $core"
        xterm -geometry +$xpos+$ypos \
            -title "Running $n on $core" \
            -e "\
            cd $PWD ; \
            source $VAISW_INSTALL_DIR/settings.sh ; \
            export VAISW_RUNSESSION_ENABLECORES=$core ; \
            export VAISW_RUNSESSION_NETWORKNAME=$n ; \
            ${CMD% *} $n ; \
            echo ; \
            echo press enter to close the terminal ; \
            read" &
        bgProcess+=( $! )

        xpos=$(( $xpos + $xinc ))
        if [ "$xpos" = "$(( $xinc * 4 ))" ]
        then
            xpos=0
            ypos=$(( $ypos + $yinc ))
        fi

    done
    trap killSubProcess INT
    wait
fi


# vim: set expandtab sw=4:
