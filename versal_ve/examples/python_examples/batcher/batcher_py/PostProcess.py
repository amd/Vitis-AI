# -*- coding: utf-8 -*-
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================
# flake8: noqa
# isort:skip_file

"""
Module containing all classes to post-process data returns by network in order
to get usable results.
All classes will have the same postProcess function to be able to be abstacted.
"""

from __future__ import print_function

import itertools
import math
import textwrap
from collections import OrderedDict
from functools import reduce, total_ordering
from typing import Any

import numpy as np

from . import Image
from .utils import Box, parse_prog, yaccer, log
from .Stats import Stats


def sigmoid(x):
    """The sigmoid of value x : 1 / (1 + e^-x)"""
    return 1.0 / (1.0 + np.exp(-x))


def softmax(x, axis=-1):
    exp = np.exp(x - np.amax(x, axis=axis, keepdims=True))
    return exp / np.sum(exp, axis=axis, keepdims=True)


class Prediction:
    """Group Prediction classes.

    Each class will allow to retrive the list of predictions in an image.
    """

    class __Base:
        def __init__(self, image):
            self.image = image
            self.preds = []

        def display(self, prefix=""):
            return "\n".join(["{}{}".format(prefix, x) for x in self.preds])

        def __getitem__(self, i):
            return self.preds[i]

        def __len__(self):
            return len(self.preds)

    @total_ordering
    class Label:
        """Simple label class that keep track of the label id"""

        def __init__(self, name, id):
            self.name = name
            self.id = id

        def __eq__(self, o):
            if isinstance(o, Prediction.Label):
                return self.name == o.name
            elif isinstance(o, str):
                return self.name == o
            else:
                raise TypeError("Labels can only compare with other labels or strings.")

        def __lt__(self, o):
            if isinstance(o, Prediction.Label):
                return self.name < o.name
            elif isinstance(o, str):
                return self.name < o
            else:
                raise TypeError("Labels can only compare with other labels or strings.")

        def __hash__(self):
            return hash(self.name)

        def __str__(self):
            return self.name

    class Classification(__Base):
        """Predictions returns by network that only recognise the image.
        Contains a label and the percentage of confidence.
        """

        class Prob:
            """Inner probability for labels"""

            def __init__(self, label, prob):
                self.label = label
                self.probability = float(prob)

            def __eq__(self, o):
                """Prob class can be compare with label"""
                if isinstance(o, self.__class__):
                    return self.label == o.label
                else:
                    return self.label == o

            def __repr__(self):
                return "TOPN_Prob({})".format(str(self))

            def __str__(self):
                return "{} - {:>1.6f}".format(self.label, self.probability)

        def add_prediction(self, label, prob):
            self.preds.append(self.Prob(label, prob))

        def __repr__(self):
            if len(self.preds) == 0:
                return "TOP{}({}, No data)".format(len(self.preds), self.image)
            else:
                return "TOP{}({}, {})".format(len(self.preds), self.image, self.preds[0].label)

    class Detection(__Base):
        """Predictions returns by network that find objects in a picture.
        Contains a label, the bounding box and the percentage of confidence.
        """

        class Pred:
            """Inner box prediction class"""

            def __init__(self, label, prob, box):
                self.label = label
                self.box = box
                self.probability = float(prob)

            def __repr__(self):
                return "Boxed_Pred({})".format(str(self))

            def __str__(self):
                return "{} - {:>1.6f} - @{}".format(self.label, self.probability, self.box)

        def add_prediction(self, label, prob, box):
            self.preds.append(self.Pred(label, prob, box))

        def append(self, pred):
            # Special function to directly add a Pred object
            self.preds.append(pred)

        def __repr__(self):
            return "BOXED({}, {})".format(self.image, self.preds)


FuncList = []  # List all post process functions


def _ppValidator(cls):
    """Validate that the classes are correctly sets"""
    for name, val in vars(cls).items():
        if not name.startswith("_"):
            if not hasattr(val, "postProcess"):
                raise AttributeError(
                    "Class {} must contains a 'postProcess' function.".format(name)
                )
            pp = val.postProcess
            if "return" not in pp.__annotations__:
                raise SyntaxError("Please provide return hint type to {}.postProcess".format(name))
            if "outputs" not in pp.__annotations__:
                raise SyntaxError(
                    "Please provide hint type to outputs parameter of {}.postProcess".format(name)
                )
            FuncList.append(val)
    return cls


@_ppValidator
class _PostProcessFuncs:
    """Group of post processing functions"""

    class __Base:
        def set_labels(self, labels):
            self.labels = labels

        def __call__(self, outputs, batch):
            with Stats.Timer(
                lambda x: log.verbose(
                    "PostProcess {} time -> {} s".format(self.__class__.__name__, x)
                )
            ):
                return self.postProcess(outputs, batch)

    class DictToList(__Base):
        """Convert output to a list if it is a dict."""

        def postProcess(self, outputs: Any, batch) -> list:
            if isinstance(outputs, (dict, OrderedDict)):
                return list(outputs.values())
            else:
                return outputs

    class Transpose(__Base):
        """Transpose outputs dimension. Give the axes as parameter"""

        def __init__(self, axes=None):
            if axes:
                axes = [int(x) for x in axes]
            self.axes = axes

        def postProcess(self, outputs: list, batch) -> list:
            return [np.transpose(x, axes=self.axes) for x in outputs]

    class SLICE(__Base):
        """get only the output layers with the given index"""

        def __init__(self, l):
            self.l = [int(i) for i in l]

        def postProcess(self, outputs: list, batch) -> list:
            return [outputs[i] for i in self.l]

    class NONE(__Base):
        """No post process"""

        def postProcess(self, outputs: list, batch) -> list:
            return outputs

    class TOP_N(__Base):
        """Get top N predictions from the results."""

        def __init__(self, N: int = 5):
            self.N = int(N)

        def postProcess(self, outputs: list, batch) -> Prediction.Classification:
            """Return 2D list of group of N StatPredictions from the most confident to the lowest.
            outputs must be a list of the output layers data.
            If the Nth prediction share its probability with some other, all the predictions will
            be added to the StatPrediction. Thus the length can be more than N.
            """
            if len(outputs) != 1:
                raise ValueError(f"TOP_N is suppose to have one output layer, got {len(outputs)}.")
            ret = []
            # Eager Tensor needs an explicit conversion to numpy
            output = np.array(outputs[0]).reshape((outputs[0].shape[0], -1))
            top_k = output.argsort(axis=1)[:, ::-1]
            for image, out, top in zip(batch, output, top_k):
                out = out[top]
                # keep all values equal to the last one.
                out = out[out >= out[self.N - 1]]
                pred = Prediction.Classification(image.file)
                for i, p in zip(top, out):
                    pred.add_prediction(self.labels[i], p)
                ret.append(pred)
            return ret

    class Yolov1_PP(__Base):
        """Get Yolov1 boxed prediction."""

        def __init__(self, pred_per_cell=2, conf_thresh=0.2):
            self.prediction_per_cell = int(pred_per_cell)
            self.confidence_threshold = float(conf_thresh)

        def postProcess(self, outputs: list, batch) -> Prediction.Detection:
            """Return a list of Box prediction for each object validate in the pictures.
            Predictions are return by images in non sorted order.
            As yolov1 is waiting a single output, multiple outputs will raise an error.
            """
            if len(outputs) != 1:
                raise ValueError("Yolo_v1 is suppose to have only one output layer.")
            outputs = outputs[0].reshape((outputs[0].shape[0], -1))
            ret = [Prediction.Detection(im.file) for im in batch]
            C = len(self.labels)
            B = self.prediction_per_cell
            SS = outputs.shape[1] // (B * 5 + C)  # number of grid cells
            S = math.sqrt(SS)  # output must be a square
            prob_size = SS * C  # class probabilities
            conf_size = SS * B  # confidences for each grid cell
            for r, net_out in zip(ret, outputs):
                probs = net_out[0:prob_size].reshape([SS, C])
                confs = net_out[prob_size : (prob_size + conf_size)].reshape([SS, B])
                coords = net_out[(prob_size + conf_size) :].reshape([SS, B, 4])
                for grid in range(SS):
                    for b in range(B):
                        final_probs = probs[grid] * confs[grid, b]
                        bc = np.argmax(final_probs)
                        if final_probs[bc] >= self.confidence_threshold:
                            coords[grid, b, 0] = (coords[grid, b, 0] + grid % S) / S
                            coords[grid, b, 1] = (coords[grid, b, 1] + grid // S) / S
                            coords[grid, b, 2] = coords[grid, b, 2] ** 2
                            coords[grid, b, 3] = coords[grid, b, 3] ** 2
                            r.add_prediction(
                                self.labels[bc], final_probs[bc], Box.from_center(*coords[grid, b])
                            )
            return ret

    class NMS(__Base):
        """\
        Apply Non Maximum Suppression into the box predictions,
        filtering out prediction that iou is above the threshold parameter."""

        def __init__(self, threshold=0.5):
            self.threshold = float(threshold)

        def _NMS(self, detect):
            preds = sorted(detect.preds, key=lambda p: (p.label, p.probability))
            # print("preds len :", len(preds))
            detect.preds.clear()
            for _, g in itertools.groupby(preds, key=lambda p: p.label):
                g_preds = list(g)
                # print("  gpred len :", len(g_preds))
                while g_preds:
                    cur_pred = g_preds.pop()
                    detect.append(cur_pred)
                    g_preds = [p for p in g_preds if cur_pred.box.iou(p.box) < self.threshold]
            return detect

        def postProcess(self, outputs: Prediction.Detection, batch) -> Prediction.Detection:
            return [self._NMS(o) for o in outputs]

    class EfficientDet(__Base):
        """EfficientDet box prediction."""

        def __init__(
            self,
            image_size,
            conf_threshold=0.5,
            ratios=[1, 0.5, 2],
            scales=[2**0, 2 ** (1.0 / 3.0), 2 ** (2.0 / 3.0)],
        ):
            if isinstance(image_size, (tuple, list)):
                self.image_size = [int(x) for x in image_size[:2]]
            else:
                self.image_size = (int(image_size), int(image_size))
            if self.image_size[0] % 2**7 != 0:
                raise ValueError(
                    "For now image size must be divisible by {} for efficientdet.".format(2**7)
                )

            self.ratios = np.array([float(x) for x in ratios], dtype=np.float32)
            self.scales = np.array([float(x) for x in scales], dtype=np.float32)
            self.pred_per_cell = len(self.ratios) * len(self.scales)
            self.conf_threshold = float(conf_threshold)

            # anchors are all centered on each cell, ratio and scale make size variations
            a = np.tile(np.repeat(scales, len(ratios))[None], (2, 1)).T
            s = np.sqrt(np.tile(ratios, len(scales)))
            self.anchors = np.stack((a[:, 0] / s, a[:, 1] * s), axis=1).astype(np.float32)

        def postProcess(self, outputs: list, batch) -> Prediction.Detection:
            assert len(outputs) % 2 == 0, (
                "Efficientdet is waiting an even number of outputs, received {} outputs".format(
                    len(outputs)
                )
            )

            # Detect regression and classification in output based on their size.
            # Regression boxes got 4 values per cell.
            outputs = sorted(
                [o.reshape(o.shape[:-1] + (self.pred_per_cell, -1)) for o in outputs],
                key=lambda o: o.shape[1],
            )
            regression = [o for o in outputs if o.shape[-1] == 4]
            classification = [o for o in outputs if o.shape[-1] != 4]
            ret = [Prediction.Detection(im.file) for im in batch]

            for reg, clsf in zip(regression, classification):
                _, H, W, _, _ = reg.shape
                tc = np.amax(clsf, axis=-1)
                stride = self.image_size[0] // H
                base_size = stride * 4
                for pos in zip(*np.where(tc > self.conf_threshold)):
                    n, h, w, b = pos
                    # Anchors
                    xa = stride / 2 + w * stride
                    ya = stride / 2 + h * stride
                    wa = base_size * self.anchors[b, 0]
                    ha = base_size * self.anchors[b, 1]

                    yr, xr, hr, wr = reg[pos]
                    wr = np.exp(wr) * wa
                    hr = np.exp(hr) * ha
                    yr = yr * ha + ya
                    xr = xr * wa + xa
                    ret[n].add_prediction(
                        self.labels[np.argmax(clsf[pos])], tc[pos], Box.from_center(xr, yr, wr, hr)
                    )
                    last_pred = ret[n].preds[-1]
                    # TODO: separate these actions
                    last_pred.box = last_pred.box.clip_inside(
                        self.image_size[0] - 1
                    )  # clip in image
                    last_pred.box /= self.image_size[0]  # normalize

            return ret

    class TFHUB_DET(__Base):
        """TFHUB Object Detection post-processing."""

        def __init__(self, conf_thresh=0.0):
            self.confidence_threshold = conf_thresh

        def postProcess(self, outputs: dict, batch) -> Prediction.Detection:
            assert isinstance(outputs, (dict, OrderedDict)), (
                "TFHUB detection models outputs are a dictionary"
            )
            assert len(outputs) == 8, (
                f"TFHUB detection models have 8 outputs, received {len(outputs)} outputs"
            )

            # Get relevant output elements
            boxes = outputs["detection_boxes"].numpy()
            classes = outputs["detection_classes"].numpy()
            scores = outputs["detection_scores"].numpy()

            ret = [Prediction.Detection(im.file) for im in batch]

            for idx, (box, clss, score) in enumerate(zip(boxes, classes, scores)):
                for b, c, s in zip(box, clss, score):
                    if s > self.confidence_threshold:
                        ret[idx].add_prediction(
                            self.labels[int(c)], s, Box.up_left_down_right(b[1], b[0], b[3], b[2])
                        )  # Boxes in yxyx format

            return ret

    class Darknet(__Base):
        """Use Darknet box prediction."""

        def __init__(self, anchors, conf_thresh, class_operation):
            self.anchors = [[float(x) for x in l] for l in anchors]
            self.confidence_threshold = float(conf_thresh)
            ops = {"softmax": softmax, "sigmoid": sigmoid}
            self.cls_op = (
                ops[class_operation] if isinstance(class_operation, str) else class_operation
            )
            self.prediction_per_cell = len(anchors[0]) // 2

        def postProcess(self, outputs: list, batch) -> Prediction.Detection:
            if len(outputs) != len(self.anchors):
                raise ValueError(
                    "Darknet postProcess is waiting {} output layer.".format(len(self.anchors))
                )
            ret = [Prediction.Detection(im.file) for im in batch]
            for output, anchors in zip(outputs, self.anchors):
                B = self.prediction_per_cell
                if len(output.shape) == 3:
                    H = W = int(math.sqrt(output.shape[1] / B))
                else:
                    _, H, W, _ = output.shape
                output = output.reshape((output.shape[0], H, W, B, -1))
                for im, r, out in zip(batch, ret, output):
                    Classes = self.cls_op(out[..., 5:])
                    Bbox = out[..., :4]
                    Bbox_pred = sigmoid(out[..., 4:5])
                    final_probs = Bbox_pred * Classes
                    for row, col, box in itertools.product(range(H), range(W), range(B)):
                        bc = np.argmax(final_probs[row, col, box])
                        if final_probs[row, col, box, bc] >= self.confidence_threshold:
                            bx = (col + sigmoid(Bbox[row, col, box, 0])) / W
                            by = (row + sigmoid(Bbox[row, col, box, 1])) / H
                            bx = (bx - im.offset[0]) * im.scale[0]
                            by = (by - im.offset[1]) * im.scale[1]
                            bw = np.exp(Bbox[row, col, box, 2]) * anchors[2 * box + 0] / W
                            bh = np.exp(Bbox[row, col, box, 3]) * anchors[2 * box + 1] / H
                            bw *= im.scale[0]
                            bh *= im.scale[1]
                            r.add_prediction(
                                self.labels[bc],
                                final_probs[row, col, box, bc],
                                Box.from_center(bx, by, bw, bh),
                            )
            return ret

    class Yolov5_BP(__Base):
        """Use Yolov5 box prediction."""

        def __init__(self, conf_thresh, multi_label, scale=False):
            self.confidence_threshold = float(conf_thresh)
            self.multi_label = multi_label
            self.scale = scale
            assert 0 <= self.confidence_threshold <= 1, (
                f"Invalid Confidence threshold {conf_thresh}, valid values are between 0.0 and 1.0"
            )

        def postProcess(self, outputs: list, batch) -> Prediction.Detection:
            outputs = np.array(outputs[0])
            num_classes = outputs.shape[2] - 5

            # filter by confidence
            valid_outputs = outputs[..., 4] > self.confidence_threshold

            # Settings: multiple labels per box
            self.multi_label &= num_classes > 1

            ret = [Prediction.Detection(im.file) for im in batch]

            for idx, output in enumerate(outputs):  # image index, image inference output
                # Rescaling
                if self.scale:
                    output[:, :4] /= batch.data.shape[2:] + batch.data.shape[2:]
                output[..., 0] = (output[..., 0] - batch[idx].offset[0]) * batch[idx].scale[0]
                output[..., 1] = (output[..., 1] - batch[idx].offset[1]) * batch[idx].scale[1]
                output[..., 2] *= batch[idx].scale[0]
                output[..., 3] *= batch[idx].scale[1]

                # Apply filter by confidence
                output = output[valid_outputs[idx]]

                # If none remain process next image
                if not output.shape[0]:
                    continue

                # Compute conf
                output[:, 5:] *= output[:, 4:5]  # conf = obj_conf * cls_conf

                # Detection matrix [num_detections, 6] (x, y, w, h, conf, cls)
                if self.multi_label:
                    i, j = np.array((output[:, 5:] > self.confidence_threshold).nonzero())
                    output = np.concatenate(
                        (output[i, :4], output[i, j + 5, None], j[:, None].astype(np.float32)), 1
                    )
                else:  # best class only
                    conf = output[:, 5:].max(axis=1, keepdims=True)
                    j = output[:, 5:].argmax(axis=1)
                    output = np.concatenate((output[:, :4], conf, j[:, None]), 1)[
                        output[:, 4] > self.confidence_threshold
                    ]

                # Limit number of predictions from exploding when the output scores are wrong
                max_nms = 5000
                if not output.shape[0]:
                    continue
                if output.shape[0] > max_nms:
                    output = output[output[:, 4].argsort()[-max_nms:]]

                # Format prediction in [label, probability, bbox]
                for pred in output:
                    ret[idx].add_prediction(
                        self.labels[int(pred[5])],
                        pred[4],
                        Box.from_center(pred[0], pred[1], pred[2], pred[3]),
                    )

            return ret

    class RefineDet(__Base):
        """Use RefineDet box prediction."""

        def __init__(
            self,
            image_size,
            min_sizes=[32, 64, 128, 256],
            steps=[8, 16, 32, 64],
            aspect_ratios=(1, 2, 0.5),
            offset=0.5,
        ):
            if isinstance(image_size, (tuple, list)):
                self.image_size = [int(x) for x in image_size]
            else:
                self.image_size = (int(image_size), int(image_size))
            self.min_sizes = [int(x) for x in min_sizes]
            self.steps = [int(x) for x in steps]
            self.feature_maps = [self.image_size[0] // s for s in self.steps]
            self.aspect_ratios = [float(x) for x in aspect_ratios]
            self.offset = float(offset)
            self.prior_box = self._calculate_prior_boxes()

        def _calculate_prior_boxes(self):
            # Rewrite priorbox based on the C++ code
            top_data = []
            for f, step, min_size in zip(self.feature_maps, self.steps, self.min_sizes):
                top_data.append([])
                for h, w in itertools.product(range(f), repeat=2):
                    center_x = (w + self.offset) * step
                    center_y = (h + self.offset) * step
                    for ar in self.aspect_ratios:
                        box_width = min_size * np.sqrt(ar)
                        box_height = min_size / np.sqrt(ar)
                        # xmin
                        top_data[-1].append((center_x - box_width / 2.0) / self.image_size[0])
                        # ymin
                        top_data[-1].append((center_y - box_height / 2.0) / self.image_size[1])
                        # xmax
                        top_data[-1].append((center_x + box_width / 2.0) / self.image_size[0])
                        # ymax
                        top_data[-1].append((center_y + box_height / 2.0) / self.image_size[1])
            return np.concatenate(top_data).reshape((-1, 4))

        def last_layers_refinedet(self, tensors, batch):
            # if self.base_network == 'refinedet_vgg':
            #     odm_sources = ["P3", "P4",
            #                    "P5", "P6"]
            #     arm_sources = ["conv4_3_norm", "conv5_3_norm",
            #                    "fc7", "conv6_2"]
            # else:
            odm_sources = ["resP3_inter", "resP4_inter", "resP5_inter", "resP6_inter"]
            arm_sources = [
                "resres3b3_relu_inter",
                "resres4b22_relu_inter",
                "resres5c_relu_inter",
                "resres6_relu_inter",
            ]
            odm_loc_sources = [i + "_mbox_loc" for i in odm_sources]
            odm_conf_sources = [i + "_mbox_conf" for i in odm_sources]
            arm_loc_sources = [i + "_mbox_loc" for i in arm_sources]
            arm_conf_sources = [i + "_mbox_conf" for i in arm_sources]

            ## recupere l'index de l'output_name et introduire l'index dans le tenseur pour récupérer
            ## la vraie output et faire équivaloir les bonnes dims et les bonnes structures de données
            ## (lists a la place des np.array)
            num = len(batch)

            # all tensors goes to flatten NHWC
            for layer_name in tensors:
                tensors[layer_name] = (
                    tensors[layer_name][:num, ...].transpose((0, 2, 3, 1)).reshape(num, -1)
                )

            # Calculate odm_loc
            loc_data = np.concatenate([tensors[layer] for layer in odm_loc_sources], axis=1)
            loc_data = loc_data.reshape(num, -1, 4)

            # Calculate odm_conf
            conf_data = np.concatenate([tensors[layer] for layer in odm_conf_sources], axis=1)
            # The first component is like a confidence score, and much bigger than
            # the rest. If the softmax is applied to the whole vector, it becomes
            # just [1, 0, ..., 0].
            conf_data = softmax(conf_data.reshape(num, -1, 80 + 1), axis=2)

            # Calculate arm_loc
            arm_loc_data = np.concatenate([tensors[layer] for layer in arm_loc_sources], axis=1)
            arm_loc_data = arm_loc_data.reshape(num, -1, 4)

            # Calculate arm_conf
            arm_conf_data = np.concatenate([tensors[layer] for layer in arm_conf_sources], axis=1)
            arm_conf_data = softmax(arm_conf_data.reshape(num, -1, 2), axis=2)

            return loc_data, conf_data, arm_loc_data, arm_conf_data

        def CreateFile(self, odm_loc, odm_conf, arm_loc, arm_conf):
            h, w = self.image_size
            name = "refinedet_resnet101_{}_postproc.prototxt".format(w)
            text = """
                   name: "coco_refinedet_resnet101_{}x{}_deploy"
                   layer {{
                    type: "Input"
                    top: "odm_loc"
                    name: "odm_loc"
                    input_param: {{
                     shape: {{ dim: 1 dim: {} }}
                    }}
                   }}
                   layer {{
                    type: "Input"
                    top: "odm_conf_flatten"
                    name: "odm_conf_flatten"
                    input_param: {{
                    shape: {{ dim: 1 dim: {} }}
                    }}
                   }}
                   layer {{
                    type: "Input"
                    top: "arm_loc"
                    name: "arm_loc"
                    input_param: {{
                     shape: {{ dim: 1 dim: {} }}
                    }}
                   }}
                   layer {{
                    type: "Input"
                    top: "arm_conf_flatten"
                    name: "arm_conf_flatten"
                    input_param: {{
                     shape: {{ dim: 1 dim: {} }}
                    }}
                   }}
                   layer {{
                    type: "Input"
                    name: "arm_priorbox"
                    top: "arm_priorbox"
                    input_param: {{
                     shape: {{ dim: 1 dim: 2 dim: {} }}
                    }}
                   }}
                   layer {{
                    name: "detection_out"
                    type: "DetectionOutput"
                    bottom: "odm_loc"
                    bottom: "odm_conf_flatten"
                    bottom: "arm_priorbox"
                    bottom: "arm_conf_flatten"
                    bottom: "arm_loc"
                    top: "detection_out"
                    include {{
                     phase: TEST
                    }}
                    detection_output_param {{
                     num_classes: 81
                     share_location: true
                     background_label_id: 0
                     nms_param {{
                      nms_threshold: 0.45
                      top_k: 1000
                     }}
                     code_type: CENTER_SIZE
                     keep_top_k: 500
                     confidence_threshold: 0.01
                     objectness_score: 0.01
                    }}
                   }}
                   """.format(
                h,
                w,
                odm_loc[0].size,
                odm_conf[0].size,
                arm_loc[0].size,
                arm_conf[0].size,
                self.prior_box.size,
            )
            with open(name, "w") as prototxt:
                prototxt.write(textwrap.dedent(text))
            return name

        def postProcess(self, outputs: dict, batch) -> Prediction.Detection:
            import caffe

            ## PostProcess for refinedet
            if len(outputs) != 16:
                raise ValueError("refinedet is supposed to have 16 output layer.")
            odm_loc, odm_conf, arm_loc, arm_conf = self.last_layers_refinedet(outputs, batch)

            detectionOutFile = self.CreateFile(odm_loc, odm_conf, arm_loc, arm_conf)
            tmpNet = caffe.Net(detectionOutFile, caffe.TEST)

            var = np.asarray([0.1, 0.1, 0.2, 0.2] * (self.prior_box.shape[0])).reshape((1, -1))
            out = [Prediction.Detection(im.file) for im in batch]
            for i in range(len(odm_loc)):
                tmpNet.blobs["odm_loc"].data[...] = odm_loc[i, :, :].flatten()
                tmpNet.blobs["odm_conf_flatten"].data[...] = odm_conf[i, :, :].flatten()
                tmpNet.blobs["arm_loc"].data[...] = arm_loc[i, :, :].flatten()
                tmpNet.blobs["arm_conf_flatten"].data[...] = arm_conf[i, :, :].flatten()
                tmpNet.blobs["arm_priorbox"].data[...] = np.concatenate((
                    self.prior_box.flatten().reshape(1, -1),
                    var,
                ))
                blobs = tmpNet.forward(start="detection_out")
                detection_out = np.asarray(blobs["detection_out"].data)
                detection_out = detection_out.reshape(-1, 7)
                for d in detection_out:
                    out[i].add_prediction(
                        self.labels[int(d[1]) - 1], d[2], Box.up_left_down_right(*d[3:])
                    )
            return out

    class IMAGE_COMPARISON(__Base):
        """Get Image from output."""

        # TODO: Rework Image to return images from here
        def __init__(self, outputColor="Unset", outputFormat="Unset"):
            self.outputColor = Image.Color[outputColor]
            self.outputFormat = Image.Format[outputFormat]

        def postProcess(self, outputs: list, batch) -> Image.Image:
            if len(outputs) != 1:
                raise ValueError("Image Comparison is suppose to have only one output layer.")
            ret = []
            output = outputs[0]
            for image, out in zip(batch, output):
                ret.append(
                    Image.DataImg(
                        image.file,
                        out,
                        color=image.color
                        if self.outputColor == Image.Color.Unset
                        else self.outputColor,
                        format=image.format
                        if self.outputFormat == Image.Format.Unset
                        else self.outputFormat,
                    )
                )
            return ret

    # TODO: make it more generic
    class Boxes_from_dict(__Base):
        """\
        Read boxes directly from output.

        Outputs must be a dict with scores, class and position separated.
        All datas must be shapable as [batch_size, num_boxes, ...]
        'box_type' must be one of :
        - center    : if box are set as (center, size)
        - center_yx : Same as center but with y pos before x pos.
        - uldr      : if box are set as (upper-left point, downer-right point)
        - uldr_yx   : Same as uldr but with y pos before x pos.
        """

        BOX_TYPES = {
            "center": Box.from_center,
            "center_yx": lambda y, x, h, w: Box.from_center(x, y, w, h),
            "uldr": Box.up_left_down_right,
            "uldr_yx": lambda uy, ux, dy, dx: Box.up_left_down_right(ux, uy, dx, dy),
        }

        def __init__(self, scores, classes, boxes, score_threshold=0.1, box_type="center"):
            self.scores = scores
            self.classes = classes
            self.boxes = boxes
            self.threshold = float(score_threshold)
            if box_type not in self.BOX_TYPES:
                raise ValueError("Invalid box_type {}".format(box_type))
            self.make_box = self.BOX_TYPES[box_type]

        def postProcess(self, outputs: dict, batch) -> Prediction.Detection:
            required_keys = [self.scores, self.classes, self.boxes]
            if any(k not in outputs for k in required_keys):
                raise RuntimeError(
                    f"Postprocess requires tensors {', '.join(required_keys())}"
                    f"but the available ones are {', '.join(list(outputs.keys()))}"
                )
            scr = outputs[self.scores].reshape(len(batch), -1)
            cls = outputs[self.classes].reshape(len(batch), -1)
            bxs = outputs[self.boxes].reshape(len(batch), scr.shape[1], -1)

            if scr.shape != cls.shape or scr.shape + (4,) != bxs.shape:
                raise RuntimeError(
                    "Invalid output batch : score {}, classes {}, boxes {}".format(
                        scr.shape, cls.shape, bxs.shape
                    )
                )

            # loop on image
            ret = [Prediction.Detection(im.file) for im in batch]
            for r, im, im_s, im_c, im_b in zip(ret, batch, scr, cls, bxs):
                for s, c, b in zip(im_s, im_c, im_b):
                    if s > self.threshold:
                        r.add_prediction(self.labels[int(c) - 1], s, self.make_box(*b))
                        # Apply rescale. TODO: separate rescale into specific command.
                        r[-1].box.x -= im.offset[0]
                        r[-1].box.y -= im.offset[1]
                        r[-1].box *= im.scale
            return ret

    class UpdateLabelId(__Base):
        """\
        Update label ids to another value

        In case of label ids need to be something else than the output index
        this function allows the user to setup such update.
        The update_map is a simple list with the new value for each id.
        The offset allow to move all values to a certain offset.
        """

        def __init__(self, update_map=None, offset=0):
            self.update_map = [int(x) for x in update_map]
            self.offset = int(offset)

        def postProcess(self, outputs: Any, batch) -> Any:
            for output in outputs:
                for pred in output:
                    if self.update_map:
                        pred.label.id = self.update_map[pred.label.id]
                    pred.label.id += self.offset
            return outputs

    class CocoLabelIds(UpdateLabelId):
        """COCO categories starts at 1. Update categories for it"""

        def __init__(self):
            super().__init__(offset=1)

    class Coco80To91LabelIds(UpdateLabelId):
        """Specific function to update label ids for Coco from 80 to 91 base categories."""

        def __init__(self):
            super().__init__([
                1,
                2,
                3,
                4,
                5,
                6,
                7,
                8,
                9,
                10,
                11,
                13,
                14,
                15,
                16,
                17,
                18,
                19,
                20,
                21,
                22,
                23,
                24,
                25,
                27,
                28,
                31,
                32,
                33,
                34,
                35,
                36,
                37,
                38,
                39,
                40,
                41,
                42,
                43,
                44,
                46,
                47,
                48,
                49,
                50,
                51,
                52,
                53,
                54,
                55,
                56,
                57,
                58,
                59,
                60,
                61,
                62,
                63,
                64,
                65,
                67,
                70,
                72,
                73,
                74,
                75,
                76,
                77,
                78,
                79,
                80,
                81,
                82,
                84,
                85,
                86,
                87,
                88,
                89,
                90,
            ])

    class __Alias(__Base):
        """Base class to create aliases classes"""

        def postProcess(self, outputs: Any, batch) -> Any:
            # Alias will be replaced so this should not be called
            return NotImplementedError("Should not be called")

        def alias(self):
            return NotImplementedError("Aliases must implement alias function.")

    class Yolov1(__Alias):
        """Alias for "Yolov1_PP (NMS :NMS_threshold)"."""

        def __init__(self, NMS_threshold=0.5):
            self.NMS_threshold = NMS_threshold

        def alias(self):
            return [_PostProcessFuncs.Yolov1_PP(), _PostProcessFuncs.NMS(self.NMS_threshold)]

    class Yolov2(__Alias):
        """\
        Alias for "(Darknet anchors conf_thresh :class_operation softmax) (NMS NMS_thresh)".
        Default anchors are {spec.defaults[2]}."""

        def __init__(
            self,
            conf_thresh=0.1,
            NMS_thresh=0.4,
            anchors=(
                (
                    0.57273,
                    0.677385,
                    1.87446,
                    2.06253,
                    3.33843,
                    5.47434,
                    7.88282,
                    3.52778,
                    9.77052,
                    9.16828,
                ),
            ),
        ):
            self.conf_thresh = conf_thresh
            self.NMS_thresh = NMS_thresh
            self.anchors = anchors

        def alias(self):
            return [
                _PostProcessFuncs.Darknet(self.anchors, self.conf_thresh, softmax),
                _PostProcessFuncs.NMS(self.NMS_thresh),
            ]

    class Yolov3(__Alias):
        """\
        Alias for "(Darknet anchors conf_thresh :class_operation sigmoid) (NMS NMS_thresh)".
        Default anchors are {spec.defaults[2]}."""

        def __init__(
            self,
            conf_thresh=0.3,
            NMS_thresh=0.5,
            anchors=(
                (116, 90, 156, 198, 373, 326),
                (30, 61, 62, 45, 59, 119),
                (10, 13, 16, 30, 33, 23),
            ),
        ):
            self.conf_thresh = conf_thresh
            self.NMS_thresh = NMS_thresh
            # Modify anchors to simplify their usage in the PostProcess function.
            self.anchors = [[float(x) / l for x in a] for a, l in zip(anchors, [32, 16, 8])]

        def alias(self):
            return [
                _PostProcessFuncs.Darknet(self.anchors, self.conf_thresh, sigmoid),
                _PostProcessFuncs.NMS(self.NMS_thresh),
            ]

    class Yolov5(__Alias):
        """\
        Alias for "(Yolov5 conf_thresh multi_label) (NMS NMS_thresh)".
        """

        def __init__(self, conf_thresh=0.01, multi_label=True, NMS_thresh=0.6, scale=False):
            self.conf_thresh = conf_thresh
            self.NMS_thresh = NMS_thresh
            self.multi_label = multi_label
            self.scale = scale

        def alias(self):
            return [
                _PostProcessFuncs.Yolov5_BP(self.conf_thresh, self.multi_label, self.scale),
                _PostProcessFuncs.NMS(self.NMS_thresh),
            ]


class PostProcessing:
    """The post-processing class"""

    class _labellist(list):
        """List without error when asking values outside of its range."""

        def __getitem__(self, i):
            if 0 <= i < len(self):
                return Prediction.Label(super().__getitem__(i), int(i))
            else:
                return Prediction.Label("label_{} (???)".format(i), int(i))

    def __init__(self, prog, labels):
        self.labels = PostProcessing._labellist(labels)
        prog = parse_prog(prog)
        self.pp = yaccer(prog, _PostProcessFuncs)
        self.pp = self._replace_aliases(self.pp)
        self._check_func_chain()
        for c in self.pp:
            # share some values with all classes if they need them
            c.set_labels(self.labels)

    def _replace_aliases(self, ppfuncs):
        r = []
        for p in ppfuncs:
            if hasattr(p, "alias"):
                r.extend(p.alias())
            else:
                r.append(p)
        return r

    def _check_func_chain(self):
        last_ret = Any
        last_p = None

        annot = self.pp[0].postProcess.__func__.__annotations__
        if annot["outputs"] is list:
            # Force conversion to list
            self.pp.insert(0, _PostProcessFuncs.DictToList())

        for p in self.pp:
            annot = p.postProcess.__func__.__annotations__
            if (
                annot["outputs"] is not Any
                and last_ret is not Any
                and last_ret is not annot["outputs"]
            ):
                raise ValueError("chain invalid")
            last_p = p
            last_ret = annot["return"]

    def postProcess(self, batch, output, batchIndex):
        return reduce(lambda o, f: f(o, batch), self.pp, output)
