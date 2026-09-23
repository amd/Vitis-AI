## Download ssdResnet34 model and generate snapshot

Run make command to download the model and weights of the model.It also compiles samples/Makefile to get samples for quantization.<br>
After running the make command, you can generate a snapshot inside the docker for ssdResnet34 model.<br>

## How to generate snapshot
cd Vitis-AI <br>
Run below command to generate snapshot in current working directory.
```
  ./docker/run.bash --acceptLicense -- /bin/bash -c "source npu_ip/settings.sh && VAISW_SNAPSHOT_DIRECTORY=$PWD/<name of snapshot>  make -C examples/python_examples/ssdResnet34 BATCH_SIZE=1 NB_IMAGES=9"
```
