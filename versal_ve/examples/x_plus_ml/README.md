# Compile X+ML Application

1. Install sdk.sh provided as part of release in your x86 host machine.


2. Source the sysroot path:
```
  source <path_to_installed_sdk_sysroot>/environment-setup-cortexa72-cortexa53-xilinx-linux
```

3. Navigate to x-plus-ml folder:
```
  cd <path_of_vitis-ai-2025.1.1>/examples/x_plus_ml/
```

4. Build the x-plus-ml application and the postprocess shared library:
```
  make
```

   To build only the postprocess plugin:
```
  make -C postprocess
```

5. Copy the x-plus-ml app, postprocess plugin, and json-config to the target board using SCP:
```
  scp x_plus_ml_app <board ip>:/usr/bin/
  scp postprocess/libvvascore_postprocess_resnet50-1.0.so <board ip>:/usr/lib/
  scp -r json-config <board ip>:/etc/vai/
```

   > **Note:** The postprocess plugin must be deployed to `/usr/lib/` on the target board.
   > This path is hardcoded in the runtime as `/usr/lib/libvvascore_postprocess_resnet50-1.0.so`
   > and is loaded dynamically via `dlopen` at inference time.

# Usage
The X+ML application accepts input in JPEG or NV12 raw format. Application usage follows this syntax:

```
      x_plus_ml_app [OPTIONS]
 	-i      Input file path (mandatory)
 	-o      Output file path (optional)
             	If provided, inference results overlaid on the frame and dumped into this file.
 	-c      Config file path (mandatory)
 	-s      Snapshot path (mandatory)
 	-n      Number of frames to process (optional, default is to process all frames)
 	-l      Application log level to print logs (optional, default is ERROR and WARNING).
             	Accepted log levels: 1 for ERROR, 2 for WARNING, 3 for INFERENCE RESULT, 4 for INFO, 5 for DEBUG.
             	Logs at the provided level and all levels below will be printed.
 	-d      WidthxHeight of the input
             	(required only in case of nv12 input, Ex : 224x224)
 	-h      Print this help and exit
```

# Run X+ML Application on Target
Copy the required snapshot to the target and run below commands.
```
  export VAISW_INSTALL_DIR=/etc/vai
  x_plus_ml_app -i <input_224x224_jpg_file> -c /etc/vai/json-config/resnet50.json -s <path-to-snapshot.resnet50>/snapshot.resnet50 -o output.bgr -l 3
```

