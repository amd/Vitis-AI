# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================
#
# Based on label_image.py from Tensorflow examples.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import argparse
import sys
import os
import json
import time
from enum import Enum
from collections import deque
from functools import partial

import itertools

import numpy as np
import cv2
try:
  import vaisw
except:
  vaisw = None

try:
  from PIL import Image as PILImage
except:
  PILImage = None

INPUT_NODE_NAME = "tfinput"

# Shell colors
SHC_GREEN = "\033[1;32m"
SHC_YELLOW = "\033[1;33m"
SHC_RESET = "\033[0m"


def warning(str_value, *args, **kwargs):
  """Print warning in visible way. Use like the print function."""
  print("{}[WARNING]{} {}".format(SHC_YELLOW, SHC_RESET, str_value), *args, **kwargs)

def grouper(iterable, n, fillvalue=None):
    "Collect data into fixed-length chunks or blocks (from python doc)"
    # grouper('ABCDEFG', 3, 'x') --> ABC DEF Gxx
    args = [iter(iterable)] * n
    return itertools.zip_longest(fillvalue=fillvalue, *args)

def variableBatchGenerator(i, n):
    "generator for a variable batch size between 1 and n"
    b = 0
    it = iter(i)
    while True:
        batch = list(itertools.islice(it, b+1))
        if len(batch) == 0:
            return
        yield batch
        b = (b+1)%n

class VaiswSplit(Enum):
  none   = vaisw.SplitSession3.NONE if vaisw else None
  core   = vaisw.SplitSession3.CORE if vaisw else None
  system = vaisw.SplitSession3.SYSTEM if vaisw else None
  board  = vaisw.SplitSession3.BOARD if vaisw else None

  def __str__(self):
    return self.name

  @staticmethod
  def from_string(s):
    try:
      return VaiswSplit[s]
    except KeyError:
      raise ValueError()


class ImageReader(object):
  def __init__(self, path, limit=-1, batchSize=20, resize=None, mean=0, std=255, repeat=1, method='default', variableBatch=False):
    "resize must be in form (width, height)"
    if resize is not None and len(resize) != 2:
      raise ValueError("Resize must have only 2 values")
    self.path      = path
    self.resize    = resize
    self.batchSize = batchSize
    self.mean      = mean
    self.std       = std or 1
    self.preloaded = False
    self.img_data  = []
    self.repeat    = repeat
    self.varBatch  = variableBatch

    # list files in chunks of batchSize
    if os.path.isdir(path):
      self.images = [os.path.join(path, x) for x in sorted(os.listdir(path))
                     if os.path.splitext(x)[1] in (".jpg", ".png", ".JPEG")]
    else:
      self.images = [path]

    # check boundaries
    if limit > len(self.images):
      warning("nbImages set to {} but only {} images are available.".format(limit, len(self.images)))
      limit = len(self.images)
    elif limit == -1:
      limit = len(self.images)
    if self.batchSize > limit:
      warning("batchSize set to {} but only {} images will be processed.".format(self.batchSize, limit))
      self.batchSize = limit
    elif limit % self.batchSize != 0:
      warning("batchSize ({}) is not a divisor of the number of images ({}). This may not work...".format(self.batchSize, limit))

    # make batches of images
    if self.varBatch:
        self.images = list(variableBatchGenerator(self.images[:limit], self.batchSize))
    else:
        self.images = list(map(lambda x: list(filter(None, x)), grouper(self.images[:limit], self.batchSize)))

    if method in self.resizeMethods:
      self._resize = self.resizeMethods[method]
    else:
      warning("resize method {} is not implemented, fall back to default".format(method))
      self._resize = self.resizeMethods['default']

  def _read_img(self, img_path):
    "'Virtual' function to read a single image"
    raise Exception("This object should not be used directly")

  def _read_img_batch(self, batch):
    "'Virtual' function to read a batch of images"
    resized = []
    if batch is None:
      return None
    for f in batch:
      resized.append(self._read_img(f))
    return (np.asarray(resized) - self.mean) / self.std

  def iter_repeat(self, l, count=1):
    while count > 0:
      for e in l:
        yield e
      count = count - 1

  def __iter__(self):
    "allow to loop through all images by batch. Each iteration return the array of batch size images."

    if self.preloaded:
      self._iterVals = self.iter_repeat(self.img_data, self.repeat)
    self._iterNames = self.iter_repeat(self.images, self.repeat)

    return self

  def next(self): # python2
    return self.__next__()

  def __next__(self): # python3
    n = next(self._iterNames)
    return (n, next(self._iterVals) if self.preloaded else self._read_img_batch(n))

  def preload(self):
    try:
        from tqdm import tqdm
    except ImportError:
        def tqdm(flist, **kwargs):
            for e in flist:
                yield e
    print("Reading ALL images in memory. This may take a while");
    for batch in tqdm(self.images, bar_format='{l_bar}{bar}'):
      self.img_data.append(self._read_img_batch(batch))
      sys.stdout.flush()
    self.preloaded = True


class TFImgReader(ImageReader):
  def __init__(self, path, limit=-1, batchSize=20, resize=None, mean=0, std=255, repeat=1, method='default', variableBatch=False):
    super(TFImgReader, self).__init__(path, limit, batchSize, resize, mean, std, repeat, method, variableBatch)
    tf.compat.v1.disable_eager_execution()
    self.resize_session = tf.compat.v1.Session()

  """Read image files with tensorflow."""
  def _read_img_batch(self, batch):
    resized = None
    for f in batch:
      file_reader = tf.io.read_file(f, "file_reader")
      image_reader = tf.image.decode_image(file_reader, channels = 3,
                                           name='image_reader')
      image_reader = tf.reverse(image_reader, [-1])
      if not f.endswith(".gif"):
        # gif image return 4D array
        image_reader = tf.expand_dims(image_reader, 0)

      img = tf.cast(image_reader, tf.float32)
      if self.resize is not None and self.resize != (-1, -1):
        # tensorflow take reasize in form [height, width]
        img = self._resize(self, img)
      if resized is None:
        resized = img
      else:
        resized = tf.concat([resized, img], 0)

    normalized = tf.divide(tf.subtract(resized, [self.mean]), [self.std])
    result = self.resize_session.run(normalized)
    return result

  def resize_default(self, img):
    return tf.compat.v1.image.resize_bilinear(img, self.resize[::-1])

  resizeMethods  = {
          'default':   resize_default,
  }

class PILImgReader(ImageReader):
  """Read image files with python PIL."""
  def _read_img(self, img_path):
    img = PILImage.open(img_path).convert("RGB")
    if self.resize is not None and self.resize != (-1, -1):
      img = self._resize(self, img)
    return np.array(img, dtype=np.float32)[:,:,::-1]
  def resize_default(self, img):
    return img.resize(self.resize, PILImage.BILINEAR)

  resizeMethods  = {
          'default':   resize_default,
  }

class CV2ImgReader(ImageReader):
  """Read image files with OpenCV."""
  def _read_img(self, img_path):
    img = cv2.imread(img_path)
    assert img is not None, "Cannot read image : {}".format(img_path)
    if self.resize is not None and self.resize != (-1, -1):
      img = self._resize(self, img)
    return img.astype(np.float32)
  def resize_default(self, img):
    return cv2.resize(img, self.resize)
  def resize_LetterBox(self, img):
    warning("letter box resize is not implemented, fallback to default resize")
    return self.resize_default(img)
  def resize_PanScan(self, img):
    h, w = img.shape[:2]
    width, height = self.resize
    new_w = max(w*width//w, w*height//h)
    new_h = max(h*width//w, h*height//h)
    img = cv2.resize(img, (new_w, new_h))
    return img[(new_h-height)//2:(new_h-height)//2+height, (new_w-width)//2:(new_w-width)//2+width]

  resizeMethods  = {
          'default':   resize_default,
          'LetterBox': resize_LetterBox,
          'PanScan':   resize_PanScan
  }

class TFGraph:
  def __init__(self, model, network_name, align_buf=False, dumpGraphInfo=False, graphOutput=None):
    if model is None:
      raise ValueError("Model is None")
    self.height       = 299
    self.width        = 299
    self.input_layer  = ""
    self.model_file   = model
    self._load_graph(network_name, dumpGraphInfo, graphOutput)
    self.begin_time = 0

  def get_input_size(self):
    "Return the image input size"
    return (self.width, self.height)

  def run_img_batch(self, batch, quiet=True):
    """Run batch on graph"""
    input_img = np.squeeze(batch) if self.squeeze else batch
    start_inf = time.time()
    result = self.graph_session.run(self.output_operations,
                                    {self.input_operation.outputs[0]: input_img})

    end_inf = time.time()
    if self.begin_time == 0:
        self.begin_time = end_inf
    rel_time = end_inf - self.begin_time
    if not quiet:
      print("@ {:.4f}s: FULL inference time for {} images is {} ms => {:.2f} img/s".format(rel_time, len(batch), int(1000*(end_inf - start_inf)), len(batch)/(end_inf-start_inf)))

    result = result[0] # the list contains the full batch as a single entry
    return np.expand_dims(result, axis=0) if self.squeeze else result

  def _get_io_layers(self, graph_def):
    """Retrive the input and output layers from a tensorflow GraphDef."""
    inputs = [] # list entry nodes (placeholders)
    nodes = set() # list all nodes
    full_inputs = set() # list nodes that are inputs of others
    for n in graph_def.node:
      if n.op != "Const" and n.op != "NoOp" and n.op != "Assert":
        nodes.add(n.name)
        for inp in n.input:
          nodes.add(inp)
          full_inputs.add(inp)
        if len(n.input) == 0:
          inputs.append(n)
    outputs = nodes - full_inputs
    return inputs, list(outputs)

  def _dumpGraphInfo(self, graph_def, inputs, outputs):
    with open("graph.info", "w") as f:
      print_f = partial(print, file=f)
      print_f("# Dumping various informations regarding the graph")
      print_f("Inputs:", inputs)
      print_f("Outputs:", outputs)
      print_f("")
      print_f("# Listing operations")
      for n in graph_def.node:
        print_f("{} {}".format(n.op, n.name))
      print_f("# Listing nodes with inputs:")
      for n in graph_def.node:
        print_f("{} {} ({})".format(n.op, n.name, n.input))

  def _load_graph(self, network_name, dumpGraphInfo, graphOutput):
    """Import tensorflow graph."""
    self.tfgraph = tf.Graph()
    graph_def = tf.compat.v1.GraphDef()

    with open(self.model_file, "rb") as f:
      graph_def.ParseFromString(f.read())

    inputs, outputs = self._get_io_layers(graph_def)
    if dumpGraphInfo:
      self._dumpGraphInfo(graph_def, inputs, outputs)
    assert(len(inputs) == 1) # we only support one input node
    if (len(outputs)) != 1:
      warning("The graph has {} outputs ({}), this may not be fully supported".format(len(outputs), outputs))

    self.input_layer = inputs[0].name
    self.squeeze = False
    if len(inputs[0].attr["shape"].shape.dim) == 0:
      self.height = -1
      self.width = -1
    elif len(inputs[0].attr["shape"].shape.dim) in [3,4] and inputs[0].attr["shape"].shape.dim[-1].size == 3:
      self.height = inputs[0].attr["shape"].shape.dim[-3].size
      self.width = inputs[0].attr["shape"].shape.dim[-2].size
      self.squeeze = len(inputs[0].attr["shape"].shape.dim) == 3
    else:
      raise RuntimeError("Unsupprted input shape {} from layer {}".format(inputs[0].attr["shape"].shape.dim, self.input_layer))

    with self.tfgraph.as_default():
      if len(inputs[0].attr["shape"].shape.dim) == 0 or (inputs[0].attr["shape"].shape.dim[0].size != -1 and len(inputs[0].attr["shape"].shape.dim) == 4):
        # Replace input layer to support multi image batch and adding dimension
        ph = tf.compat.v1.placeholder(inputs[0].attr["dtype"].type, shape=(None,
                                                                 None if self.height == -1 else self.height,
                                                                 None if self.width == -1 else self.width,
                                                                 3), name=INPUT_NODE_NAME)
        tf.import_graph_def(graph_def, input_map={self.input_layer + ":0": ph}, name="")
        self.input_layer = INPUT_NODE_NAME
      else:
        tf.import_graph_def(graph_def, name="")
      if network_name:
        tf.constant("runSession.networkName=" + network_name, name="vaisw")
    self.input_operation = self.tfgraph.get_operation_by_name(self.input_layer);
    self.output_operations = [self.tfgraph.get_operation_by_name(x).outputs[0] for x in sorted(graphOutput.split(',') if graphOutput else outputs)]
    self.graph_session = tf.compat.v1.Session(graph=self.tfgraph)



def load_labels(label_file):
  if label_file is None:
      raise ValueError("Labels is None")
  labels = []
  with open(label_file) as lf:
    for l in lf:
      labels.append(l.rstrip())
  return labels

def load_gold_v1(gold_file):
  """Gold Version 1 :
     one line with list of labels and one image per line. Returns a dict with
     keys are image name and values are image class."""
  SEPARATOR = "|"
  SUBSEPARATOR = ";"
  with open(gold_file, 'r') as f:
    # First line
    line1 = f.readline().strip(" \r\n" + SEPARATOR).split(SEPARATOR)
    cls = {}
    for e in line1:
      v, k = e.split(SUBSEPARATOR)
      cls[k] = v

    # All other lines
    r = {}
    for l in f:
      im, cl = l.strip().split(SEPARATOR)
      cl = cl.split("_")[0]
      r[im] = cls[cl]
    return r

def load_gold_v2(gold_file):
  """Gold simple parser from dowload script"""
  with open(gold_file, 'r') as f:
    r = {}
    for l in f:
      l = l.strip().split()
      r[l[0]] = " ".join(l[1:])
    return r



def topN(batchNames, results, labels, gold, accuracy, predictFile, batchSize, batchIndex, networkName, N=5):
  top_k = results.argsort(axis=1)[:,-N:][:,::-1]
  for imageIndex,top in enumerate(top_k):
    predictFile.write("{} Image {} ({}:{}) {}\n".format(networkName, batchSize * batchIndex + imageIndex, batchIndex, imageIndex, os.path.basename(batchNames[imageIndex])))
    g = gold[batchNames[imageIndex]] if gold is not None else None
    if g is not None:
      predictFile.write ("{}    GOLD - {} - {:>1.6f}\n".format(networkName, g, 1))
    for j,i in enumerate(top):
      color = ""
      if predictFile == sys.stdout:
        resetColor = SHC_RESET
      else:
        resetColor = ""
      label = labels[i]
      if g is not None:
        if g == label:
          # Let's be optimist if we have multiple prediction at the same value in top1
          if j == 0 or results[imageIndex][i] == results[imageIndex][top[0]]:
            if predictFile == sys.stdout:
              color = SHC_GREEN
            accuracy[0] += 1
          else:
            if predictFile == sys.stdout:
              color = SHC_YELLOW
          accuracy[1] += 1
      predictFile.write("{}    PRED - {} - {}{:>1.6f}{}\n".format(networkName, label, color, results[imageIndex][i], resetColor))
    predictFile.write("{}\n".format(networkName))


def process_result(results, names, labels, gold, accuracy, quiet, predictFile, batchSize, batchIndex, networkName, resultFile):
  if resultFile:
     with np.printoptions(threshold=np.inf):
         resultFile.write(str(results))
  if labels is not None and (not quiet or gold is not None):
    if len(names) != results.shape[0]:
      # We run a sub batch, ignore extra output data
      results = results[0:len(names),]
    results = results.reshape(len(names), -1) # reshape as (batch_size, results_per_image)
    topN(list(map(os.path.basename, names)), results, labels, gold, accuracy, predictFile, batchSize, batchIndex, networkName)

class Pipeline_ctx:
  def __init__(self):
    self.results   = deque([])
    self.startTime = []
    self.endTime   = []
    self.warnCount = [0,0]

def concatenateResult(listResults):
    assert len(listResults) > 0, "No images"
    assert len(listResults) == 1, "Multiple output not supported in label image"
    return listResults[list(listResults.keys())[0]]


def run_batch(graph, names, batch, labels, gold, accuracy, quiet, predictFile, batchSize, batchIndex, networkName, snapshotRunner, pipeline, resultFile, pipelineFps, vaiswAPI, vaiswHandler, embedded=False, profiling=None):
  """Run batch on graph"""
  results = None
  start_inf = time.time()
  if snapshotRunner == None:
    if vaiswHandler == None:
      results = graph.run_img_batch(batch, quiet)
    else:
      results = vaiswAPI.run(vaiswHandler, np.array(batch, dtype=np.float32))[0]
  elif embedded:
    snapshotRunner.upload(snapshotRunner.quantize([np.array(batch, dtype=np.float32)]))
    start_fpga = time.time()
    snapshotRunner.start()
    snapshotRunner.wait()
    profiling.append(time.time() - start_fpga)
    results = snapshotRunner.unquantize(snapshotRunner.download())[0]
  elif not pipeline:
    snapshotRunner.upload(np.array(batch, dtype=np.float32))
    results = concatenateResult(snapshotRunner.download())
  else:
    if batch is not None:
      upload_images = np.array(batch[0:len(names)], dtype=np.float32)
      uploadTime = time.time()
      if len(pipeline.startTime) > 0:
        waitTime = batchIndex * len(names)/pipelineFps - (uploadTime-pipeline.startTime[0])
        if waitTime > 0:
          time.sleep(waitTime)
          uploadTime = time.time()
        else:
          if pipeline.warnCount[0] < 10:
            warning("We may have a fifo underflow for batch {} ({:.3f} ms late) at time {}".format(batchIndex, -1000*waitTime, uploadTime - pipeline.startTime[0]))
            pipeline.warnCount[0] = pipeline.warnCount[0] + 1
      if snapshotRunner.isFull():
        if pipeline.warnCount[1] < 10:
            pipeline.warnCount[1] = pipeline.warnCount[1] + 1
            warning("Vaisw fifo is already full for batch {} at time {}, reduce pipelineFps".format(batchIndex, uploadTime - pipeline.startTime[0]))
      pipeline.startTime.append(uploadTime)
      snapshotRunner.upload(upload_images)
      pipeline.results.append((names,batchIndex))
    while snapshotRunner.isTerminated() or (batch is None and len(pipeline.results)) > 0:
      results = concatenateResult(snapshotRunner.download())
      downloadTime = time.time()
      pipeline.endTime.append(downloadTime)
      names_, batchIndex_ = pipeline.results.popleft()
      process_result(results, names_, labels, gold, accuracy, quiet, predictFile, batchSize, batchIndex_, networkName, resultFile)
    results = None
    # display a progress bar
    if batchIndex == 0:
      print("")
    if len(pipeline.endTime) > 0:
      print ("\r{:6.2f} imgs/s ({} images)  ".format(batchSize*len(pipeline.endTime)/(pipeline.endTime[-1]-pipeline.startTime[0]), batchSize*len(pipeline.endTime)), end="")
    if batch is None:
      print("")

  end_inf = time.time()

  if results is not None:
    if not quiet:
      print ("{:.2f} imgs/s".format(len(batch)/(end_inf-start_inf)))
    process_result(results, names, labels, gold, accuracy, quiet, predictFile, batchSize, batchIndex, networkName, resultFile)


if __name__ == "__main__":

  framework = "tensorflow"
  network_filename = "network"
  labels_filename = "labels"

  img_readers = {}
  img_readers["tensorflow"] = TFImgReader
  if PILImage is not None:
    img_readers["PIL"] = PILImgReader
  img_readers["cv2"] = CV2ImgReader

  parser = argparse.ArgumentParser()
  parser.add_argument("--modelPath", default="./models", help="path to model files")
  parser.add_argument("--imgPath", default="/proj/zebra/QA/datasets/imagenet/ILSVRC2012_img_val",
                      help="image(s) to be processed (can be a file or a folder)")
  parser.add_argument("--goldFile", default=None, help="path to a gold file")
  parser.add_argument("--goldVersion", default=2, type=int, choices=[1, 2],
                      help="version of gold file. For advanced users only.")
  parser.add_argument("--predictFile", type=argparse.FileType("w"), default=sys.stdout,
                      help="file to save prediction output")
  parser.add_argument("--resultFile", type=argparse.FileType("w"), default=None,
                      help="file to save raw output data")
  parser.add_argument("--dumpGraphInfo", action="store_true", help="dump graph information in graph.info file")

  parser.add_argument("network_name", nargs="?",
                      help="The network name. Make the script to look into models for this network")
  parser.add_argument("--nbImages", type=int, default=-1,
                      help="Number of images to parse in the imgPath (only if imgPath is a folder). -1 for all images. Must be a multiple of batchSize")
  parser.add_argument("--batchSize", type=int, default=20,
                      help="Number of images to be processed at the same time")
  parser.add_argument("--variableBatch", action="store_true",
                      help="Use a different batch size for each inference")
  parser.add_argument("--repeat", type=int, default=1,
                      help="Number of time the input images are repeated (useful for longer tests")
  parser.add_argument("--net_json", type=argparse.FileType('r'), default="./scripts/networks.json",
                      help="Path to the networks.json description file (only valid with network specified, otherwise use the mean and std options)")
  parser.add_argument("--img_reader", default="cv2", choices=img_readers.keys(),
                      help="Define which lib would read the image. Default is cv2")
  parser.add_argument("--quiet", action="store_true", help="don't show top5")
  parser.add_argument("--preLoadImages", action="store_true", help="preload all images before starting inference (useful for power measurement)")
  parser.add_argument("--resizeMethod", default="default", choices=CV2ImgReader.resizeMethods.keys(), help="specify the resize method to use")

  group = parser.add_argument_group('Extra option when not specifying network')
  group.add_argument("--graph", default=None,
                     help="Full path to graph/model to be executed. Replace the network one if it was specified")
  group.add_argument("--graphOutput", default=None,
                     help="comma separated list of the layer name for the output, default is to identify automatically the output node")
  group.add_argument("--inputSize", default=None,
                     help="define input image size in case network has generic dimension")
  group.add_argument("--labels", default=None,
                     help="Full path of file containing labels. Replace the network one if it was specified")
  group.add_argument("--noLabels", action="store_true", help="don't load the default label, useful for non classification neworks", default=False)
  group.add_argument("--mean", type=int, help="Input mean when no json is available", default=0)
  group.add_argument("--std", type=int, help="Input std when no json is available", default=255)
  group.add_argument("--snapshotRunner", action="store_true", help="use snapshot saved info", default=False)
  group.add_argument("--snapshotSplit", type=lambda split: VaiswSplit[split], choices=list(VaiswSplit), default=VaiswSplit.none)
  group.add_argument("--snapshotTimeout", type=int, help="timeout of vaisw server for incomplete batch size", default=100)
  group.add_argument("--pipeline", action="store_true", help="use pipeline execution mode", default=False)
  group.add_argument("--pipelineFps", type=int, help="input fps", default=1000)
  group.add_argument("--vaiswAPI", help="use Vaisw API", default=None)

  group.add_argument("--embedded", action="store_true", default=False, help="use the embedded Vaisw stack")
  group.add_argument("--snapshot", default="SNAP", help="path to the snapshot, embedded mode only")

  args = parser.parse_args()

  if args.network_name:
    args.graph = args.graph or os.path.join(args.modelPath, args.network_name, "tensorflow", "network")
    if not args.noLabels:
        args.labels = args.labels or os.path.join(args.modelPath, args.network_name, "tensorflow", "labels")
    njson = json.load(args.net_json)
    args.mean = njson[framework][args.network_name]["mean"]
    args.std = njson[framework][args.network_name]["normalizeImages"]

  if args.graph is None:
    raise Exception("Graph must be specified through network name or --graph option")

  gold = None
  if args.goldFile is not None and args.goldFile != "None":
    gold = [load_gold_v1, load_gold_v2][args.goldVersion-1](args.goldFile)
  labels = None
  if args.labels is not None:
    labels = load_labels(args.labels)

  if (args.snapshotRunner or args.pipeline) and not vaisw:
      raise Exception("snapshotRunner option can be used only if vaisw environment is sourced")
  if args.vaiswAPI and not vaisw:
      raise Exception("vaiswAPI option can be used only if vaisw environment is sourced")
  if args.vaiswAPI and (args.snapshotRunner or args.pipeline):
      raise Exception("vaiswAPI and snapshotRunner options are not compatible")

  if args.img_reader == "tensorflow":
    # tensorflow import needs to be done before importTFGraph of vaiswAPI
    import tensorflow as tf

  snapshotRunner = None
  graph = None
  vaiswAPI = None
  vaiswHandler = None
  profiling = None

  if args.snapshotRunner:
    snapshotRunner = vaisw.Server3(args.snapshotSplit.value, timeout=args.snapshotTimeout)
  elif args.vaiswAPI != None:
    inputName, outputName = args.vaiswAPI.split()
    vaiswAPI = vaisw.FrameworkRunner()
    vaiswHandler = vaiswAPI.importTFGraph(args.graph, [ inputName ], [ outputName ])
    vaiswAPI.initHandler(vaiswHandler, args.batchSize, args.network_name)
  elif args.embedded:
    import vart_ml
    snapshotRunner = vart_ml.NpuRunner(snapshot=args.snapshot, verbose=1)
    print("Setup done")
    profiling = []
  else:
    import tensorflow as tf
    graph = TFGraph(args.graph, args.network_name, dumpGraphInfo=args.dumpGraphInfo, graphOutput=args.graphOutput)

  if args.inputSize:
    input_size = tuple([int(s) for s in args.inputSize.split("x")])
  elif graph:
    input_size = graph.get_input_size()
    if None in input_size or -1 in input_size:
      warning("The graph is using generic dimension and no inputSize argument has been given. If the graph can't support any dimension, please specify --inputSize to apply a scaling of the input before sending to the graph")
  elif args.network_name:
    input_size = tuple(njson[framework][args.network_name]["shape"][1:3])
  else:
    raise Exception("unable to figure out automatically the input size. Please pass inputSize option.")

  img_reader = img_readers[args.img_reader](args.imgPath, args.nbImages, args.batchSize, input_size, args.mean, args.std, args.repeat, args.resizeMethod, args.variableBatch)
  preloadStart = time.time()
  if args.preLoadImages:
    img_reader.preload()
  preloadEnd = time.time()

  accuracy = [0,0]
  nb_images = 0
  pipeline_ctx = Pipeline_ctx() if args.pipeline else None

  for batchIndex, (names, batch) in enumerate(img_reader):
    nb_images += len(names) if names is not None else 0
    run_batch(graph, names, batch, labels, gold, accuracy, args.quiet, args.predictFile, img_reader.batchSize, batchIndex, args.network_name, snapshotRunner, pipeline_ctx, args.resultFile, args.pipelineFps, vaiswAPI, vaiswHandler, args.embedded, profiling)

  if args.pipeline:
    # flush pipeline
    run_batch(graph, None, None, labels, gold, accuracy, args.quiet, args.predictFile, img_reader.batchSize, batchIndex, args.network_name, snapshotRunner, pipeline_ctx, args.resultFile, args.pipelineFps, vaiswAPI, vaiswHandler)

    latencyTime = np.asarray(pipeline_ctx.endTime) - np.asarray(pipeline_ctx.startTime)

    if False: # show each run stats
      pipeline_ctx.endTime   = [e - pipeline_ctx.startTime[0] for e in pipeline_ctx.endTime]
      pipeline_ctx.startTime = [s - pipeline_ctx.startTime[0] for s in pipeline_ctx.startTime]
      for i,l in enumerate(latencyTime):
        print("{:4} {:6f} latency. Start {:.6f} + {:.6f} end {:.6f} + {:.6f}".format(i, l,
            pipeline_ctx.startTime[i], pipeline_ctx.startTime[i] - pipeline_ctx.startTime[i-1 if i > 0 else 0],
            pipeline_ctx.endTime[i], pipeline_ctx.endTime[i] - pipeline_ctx.endTime[i-1 if i > 0 else 0]))
    avgFps = int(nb_images / (pipeline_ctx.endTime[-1] - pipeline_ctx.startTime[0]))
    print("{:.3f} s total run time for {} img: {} FPS. \nInput FPS: {} (requested: {}). \nMax latency seen at batch {}: {:.3f} ms. \nLatency of first image is {:.3f} ms. ".format(
      pipeline_ctx.endTime[-1] - pipeline_ctx.startTime[0], nb_images,
      avgFps, int(nb_images / (pipeline_ctx.startTime[-1] - pipeline_ctx.startTime[0])), args.pipelineFps,
      np.argmax(latencyTime), 1000*max(latencyTime), 1000*latencyTime[0]
      ))
    if args.preLoadImages:
      print("For info {:.3f} s ({} FPS) to do img loading".format(
        preloadEnd - preloadStart, int(nb_images / args.repeat / (preloadEnd - preloadStart))))


  if args.embedded:
    print("")
    print(f"Number of Images processed = {nb_images} with batch of {args.batchSize}")
    print("=" * 60)
    print("Profiling summary:")
    for op, op_str in [(np.min, "min"), (np.mean, "avg"), (np.max, "max")]:
        print("FPGA latency: {} {:.2f} ms. Throughput: {:.2f} fps.".format(op_str, 1000*op(profiling[1:], axis=0), args.batchSize / op(profiling[1:], axis=0)))

  if args.predictFile and args.predictFile != sys.stdout:
    args.predictFile.close()
  if args.resultFile:
    args.resultFile.close()
  if gold is not None and nb_images > 0:
    print("")
    print("=" * 60)
    print("Accuracy Summary:")
    try:
      vaisw_log = open("vaisw_execution.log", "a")
    except:
      warning("can't open file vaisw_execution.log")
      vaisw_log = None
    for file in [sys.stdout, vaisw_log]:
      if file is None:
        continue
      print("[AMD] [{} TEST top1] {:.3f}% passed.".format(args.network_name, 100*accuracy[0]/nb_images), file=file)
      print("[AMD] [{} TEST top5] {:.3f}% passed.".format(args.network_name, 100*accuracy[1]/nb_images), file=file)
      print("[AMD] [{} ALL TESTS] {:.3f}% passed.".format(args.network_name, 100*accuracy[0]/nb_images), file=file)
    if file is not None:
      file.close()

  if args.pipeline:
    error=0
    if max(latencyTime) > 1.1 * latencyTime[0] and max(latencyTime) > latencyTime[0] + 0.002:
      warning("max latency {:.3f} ms is too high compared to first latency {:.3f}, maybe the requested FPS {} is too high".format(1000*max(latencyTime), 1000*latencyTime[0], args.pipelineFps))
      error = 1
    if avgFps < 0.9 * args.pipelineFps:
      warning("average FPS {} is too low compared to input FPS, maybe the requested FPS {} is too high".format(avgFps, args.pipelineFps))
      error = 1
    # be nice and don't complain yet, but it has to come soon...
    #exit(error)

