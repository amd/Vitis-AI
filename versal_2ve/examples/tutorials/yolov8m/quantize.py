#!/bin/bash 

# ===========================================================
# Copyright © 2025 Advanced Micro Devices, Inc. All rights reserved.
# MIT License
# ===========================================================

import os
import cv2
import numpy as np
import onnx
from onnxruntime.quantization import CalibrationDataReader
from typing import List, Tuple
import re

def get_model_input_name(input_model_path: str) -> str:
        model = onnx.load(input_model_path)
        model_input_name = model.graph.input[0].name
        return model_input_name

class ImageDataReader(CalibrationDataReader):

    def __init__(self, calibration_image_folder: str, input_name: str):
        self.enum_data = None

        self.input_name = input_name

        self.data_list = self._preprocess_images(
                calibration_image_folder)

    def _preprocess_images(self, image_folder: str):
        data_list = []
        img_names = [f for f in os.listdir(image_folder) if f.endswith('.png') or f.endswith('.jpg')]
        input_size = (640, 640)  # YOLOv8m input size
        
        for name in img_names:
            # Read image (BGR format from cv2.imread)
            img = cv2.imread(os.path.join(image_folder, name))
            if img is None:
                continue
            
            # Preprocess using same logic as utils.py preprocess_image()
            # 1. Preserve aspect ratio and resize
            img_height, img_width = img.shape[:2]
            scale = min(input_size[0] / img_width, input_size[1] / img_height)
            new_size = int(img_width * scale), int(img_height * scale)
            img_resized = cv2.resize(img, new_size)
            
            # 2. Add padding (letterboxing) to make it square
            top = (input_size[1] - new_size[1]) // 2
            bottom = (input_size[1] - new_size[1]) - top
            left = (input_size[0] - new_size[0]) // 2
            right = (input_size[0] - new_size[0]) - left
            
            img_resized = cv2.copyMakeBorder(
                img_resized,
                top,
                bottom,
                left,
                right,
                borderType=cv2.BORDER_CONSTANT,
                value=(0, 0, 0),
            )
            
            # 3. Convert BGR to RGB (matching evaluate.py which uses bgr2rgb=True)
            img_resized = cv2.cvtColor(img_resized, cv2.COLOR_BGR2RGB)
            
            # 4. Normalize to [0, 1] and convert to float32
            img_resized = np.float32(img_resized) / 255.0
            
            # 5. Transpose HWC to CHW
            img_resized = img_resized.transpose(2, 0, 1)  # hwc --> chw
            
            # 6. Add batch dimension
            img_resized = np.expand_dims(img_resized, axis=0)  # chw --> 1chw
            
            data_list.append(img_resized)

        return data_list

    def get_next(self):
        if self.enum_data is None:
            self.enum_data = iter([{self.input_name: data} for data in self.data_list])
        return next(self.enum_data, None)

    def rewind(self):
        self.enum_data = None

def parse_subgraphs_list(exclude_subgraphs: str) -> List[Tuple[List[str]]]:
    subgraphs_list = []
    tuples = exclude_subgraphs.split(";")
    for tup in tuples:
        tup = tup.strip()
        pattern = r'\[.*?\]'
        matches = re.findall(pattern, tup)
        assert len(matches) == 2
        start_nodes = matches[0].strip("[").strip("]").split(",")
        start_nodes = [node.strip() for node in start_nodes]
        end_nodes = matches[1].strip("[").strip("]").split(",")
        end_nodes = [node.strip() for node in end_nodes]
        subgraphs_list.append((start_nodes, end_nodes))
    return subgraphs_list

from quark.onnx.quantization.config import Config, get_default_config
from quark.onnx import ModelQuantizer
from onnxruntime.quantization import QuantType

quant_config = get_default_config("VINT8")
quant_config.extra_options["Int32Bias"] = False
quant_config.extra_options["DedicatedQDQPair"]: True
quant_config.extra_options["QuantizeAllOpTypes"]: True
quant_config.enable_npu_cnn = True
quant_config.subgraphs_to_exclude = [(["/model.22/Concat_3"], ["/model.22/Concat_5"])]

# Set up quantization with a specified configuration
quantization_config = Config(global_quant_config=quant_config)
quantizer = ModelQuantizer(quantization_config)

float_model_path = "models/yolov8m.onnx"
quantized_model_path = "models/yolov8m_VINT8_skipNodes.onnx"
calib_data_path = "calib_data"
model_input_name = get_model_input_name(float_model_path)
calib_data_reader = ImageDataReader(calib_data_path, model_input_name)

# Quantize the ONNX model and save to specified path
quantizer.quantize_model(float_model_path, quantized_model_path, calibration_data_reader=calib_data_reader)
