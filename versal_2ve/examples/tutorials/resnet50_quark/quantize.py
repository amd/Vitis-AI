#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import os
import cv2
import numpy as np
import onnx
from torchvision import transforms
from onnxruntime.quantization import CalibrationDataReader

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
            img_names = [f for f in os.listdir(image_folder) 
                         if f.endswith('.png') or f.endswith('.jpg')]
        
            mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
            std  = np.array([0.229, 0.224, 0.225], dtype=np.float32)
        
            for name in img_names:
                img = cv2.imread(os.path.join(image_folder, name))
                
                # BGR -> RGB
                img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        
                # Resize short side to 256
                h, w, _ = img.shape
                scale = 256 / min(h, w)
                img = cv2.resize(img, (int(w*scale), int(h*scale)))
        
                # Center crop 224
                h, w, _ = img.shape
                start_h = (h - 224) // 2
                start_w = (w - 224) // 2
                img = img[start_h:start_h+224, start_w:start_w+224]
        
                # float & normalize
                img = img.astype(np.float32) / 255.0
                img = (img - mean) / std
        
                # HWC -> CHW
                img = img.transpose(2, 0, 1)
        
                # NCHW
                img = np.expand_dims(img, axis=0)
        
                data_list.append(img)
        
            return data_list

        def get_next(self):
                if self.enum_data is None:
                        self.enum_data = iter([{self.input_name: data} for data in self.data_list])
                return next(self.enum_data, None)

        def rewind(self):
                self.enum_data = None

from quark.onnx.quantization.config import Config, get_default_config
from quark.onnx import ModelQuantizer
from onnxruntime.quantization import QuantType

quant_config = get_default_config("VINT8")
quant_config.extra_options["Int32Bias"] = False
quant_config.enable_npu_cnn = True

# Set up quantization with a specified configuration
quantization_config = Config(global_quant_config=quant_config)
quantizer = ModelQuantizer(quantization_config)

float_model_path = "models/resnet50-v1-12.onnx"
quantized_model_path = "models/resnet50-v1-12_quantized.onnx"
calib_data_path = "calib_data"
model_input_name = get_model_input_name(float_model_path)
calib_data_reader = ImageDataReader(calib_data_path, model_input_name)

# Quantize the ONNX model and save to specified path
quantizer.quantize_model(float_model_path, quantized_model_path, calibration_data_reader=calib_data_reader)
