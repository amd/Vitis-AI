# Demo for the CPP VART ML API

You'll need `libopencv-dev`, then run `make`.

First, generate a snapshot for resnet50 with batch size 1 using VAISW compilation SW stack:

```
./docker/run.bash --acceptLicense -- /bin/bash -c "source npu_ip/settings.sh && cd examples/python_examples/batcher && VAISW_SNAPSHOT_DIRECTORY=IP ./run_classification.sh -b 1 -N 10"
```

This creates the "IP" folder, that's the snapshot you need to pass to the demo.

Demo usage:

```
DEMO --imagePath PATH... --snapshot PATH [OPTION]...

Mandatory arguments:
  --imgPath PATH... either a directory or a list of images
                      if it is a directory, run on the nbImages first images
                      if it is a list of images, run on them (overrides nbImages)
  --snapshot PATH   path to the snapshot directory. Use '+' to separate multiple snapshots.

Options:
  --batchSize BATCHSIZE size of a batch of images to process, defaults to 1
  --goldFile PATH       path to the file containing the gold results
                          if none given, does not perform comparison
  --labels PATH         path to the file containing labels of results, defaults to 'labels'
  --mean MEAN           mean of a pixel (depends on the framework), defaults to 0
  --nbImages NBIMAGES   number of images to process, defaults to 1
  --network NETWORK     network to display
  --std STD             standard deviation (depends on the framework)
```

The supported image formats are OpenCV's supported image formats.

DEMO can be one of those:

* `async_demo`: demo using the thread safe calls, using a pool of thread for the execution.
* `multi_models_demo`: demo showing multiple models execution (not yet supported by compilation SW stack).
* `multi_threads_demo`: demo creating and running multiple runners in different threads.
* `simple_demo`: demo using the low level C API.
* `vart_ml_demo`: demo using the vart::Runner API. This supports also multiple models.
