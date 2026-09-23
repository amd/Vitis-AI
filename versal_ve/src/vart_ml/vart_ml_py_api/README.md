
# Python API

## Copyright and license statement
Copyright (C) 2025 Advanced Micro Devices, Inc.


The python API is done to replace with minimum of changes the usual framework (pytorch/tensorflow) API.

## Minimum pseudo code

The following example will generate a model object from a snapshot. The snapshot location is taken from the variable environment
`VAISW_SNAPSHOT_DIRECTORY`.


```
import VART

model = VART.Runner()
output_inference = model(input_inference)
```

## Full API

`VART.Runner` is the model constructor, it will return a model context to execute the inference.

* `VART.Runner` optional arguments

The VART.Runner can take the following optional arguments to build a model:

```
snapshot_dir            path of the snapshot, default is to take the content of the variable environment VAISW_SNAPSHOT_DIRECTORY
network_name            name of the model
output_names            list of output to return, default is to return all outputs
npu_only                runs only the sub-graphs executed on the AIE, CPU subgraphs are ignored
```


* model properties

A model returned by `VART.Runner` has the following properties:

```
input_shapes            a list of list of uint32 indicating the shape of each input.
input_shape_formats     a list of string indicating the format of each input ('NCHW', 'NHWC').
input_types             a list of datatype indicating the type for each input.

output_shape_formats    a list of string indicating the format of each output ('NCHW', 'NHWC').

```

## model methods to enable Zero copy

The API can support zero copy feature for input and output buffers.

Such buffers are accessible directly by the NPU IP, and therefore have to be
allocated physically in the DDR memory.
A buffer allocated by a classic python function can't be used directly by the NPU.
Only a buffer allocated by XRT can be used.

As a consequence, the API provides a function to allocate the input buffers.
The python application needs to write (like using `np.copyto`) into that
buffer.

However, the output buffers can be allocated directly by the SW stack, the
inference call can return directly a physically allocated buffer.


```
set_output_native_formats(bool)     take a bool to enable native outputs
set_input_native_formats(bool)      take a bool to enable native inputs
alloc_native_bufs(nbImages)         allocate input buffers to be used for native input.
                                    nbImages is an optional argument, default value is the batch size of the snapshot.
                                    Returns a list of buffers of size nbInputs x nbImages with the same order as C++ stack,
                                    so for a 2 inputs batch size of 3:
                                    [inputA_batch0, inputB_batch0, inputA_batch1, inputB_batch1, inputA_batch2, inputB_batch2]
```

## Limitations for the zero copy API

Currently, the SW API has the following limitations:

* input format is in `NHWC`.
* maximum of input channels supported is `8`.
* model with multiple inputs with batchSize > 1 hasn't been validated
* input and output of the models don't have the same type as the classic mode:
  * zero copy exposes each batch as a separate buffer. It allows having
    each buffer on a separate DDR memory, to optimize efficiency.
  * while the default mode concatenates each batch in the same buffer.



## Minimum pseudo code with zero copy


```
import VART

model = VART.Runner()
model.set_input_native_formats(True)
model.set_output_native_formats(True)
native_input = model.alloc_native_bufs()
np.copyto(native_input[0], input_infernce[0])
output_inference = model(input_inference)
```

