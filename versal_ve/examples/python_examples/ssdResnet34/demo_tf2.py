
# ===========================================================
# Copyright 2024 Advanced Micro Devices Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# ===========================================================

import os
import tensorflow as tf
from PIL import Image
import cv2
import numpy as np
import sys
from utils_tf2 import dboxes_R34_coco, Encoder

COCO_DICT = ["None", "person", "bicycle", "car", "motorbike", "aeroplane", "bus", "train", "truck", "boat",
             "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
             "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
             "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
             "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
             "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
             "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair",
             "sofa", "pottedplant", "bed", "diningtable", "toilet", "tvmonitor", "laptop", "mouse",
             "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator",
             "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"]

def preprocess(image):
    image = image.astype('float32') / 255.
    std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
    mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
    image = (image - mean) / std
    return image

def run_inference_for_demo(graph_def_path, input_dir, output_dir, score_threshold=0.5, num_images=None, batch_size=1):
    dboxes = dboxes_R34_coco()
    encoder = Encoder(dboxes)

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    image_files = os.listdir(input_dir)
    if num_images:
        image_files = image_files[:num_images]

    with tf.compat.v1.Graph().as_default():
        graph_def = tf.compat.v1.GraphDef()
        with tf.io.gfile.GFile(graph_def_path, 'rb') as f:
            graph_def.ParseFromString(f.read())
            tf.import_graph_def(graph_def, name='')

        images = None
        with tf.compat.v1.Session() as sess:

            for image_file in image_files:
                image_path = os.path.join(input_dir, image_file)

                image_tensor = sess.graph.get_tensor_by_name('image:0')

                image = Image.open(image_path).convert("RGB")
                image = np.array(image.resize((1200, 1200), Image.BILINEAR))
                image = preprocess(image)
                image = np.expand_dims(image, 0).astype(np.float32)
                if images is None:
                    images = image
                    images_file = [image_file]
                else:
                    images = np.concatenate((images, image))
                    images_file.append(image_file)
                if len(images) < batch_size:
                    continue

                output_tensors = {
                    'ploc': sess.graph.get_tensor_by_name('ssd1200/py_location_pred:0'),
                    'plabel': sess.graph.get_tensor_by_name('ssd1200/py_cls_pred:0')
                }

                print("Running inference for ", images_file)
                output_dict = sess.run(output_tensors, feed_dict={image_tensor: images})
                ploc = output_dict['ploc']
                plabel = output_dict['plabel']

                infResult = encoder.decode_batch(ploc, plabel, 0.50, 200, device=0)

                for inf, image_file in zip(infResult, images_file):
                    loc, label, prob = inf
                    image_path = os.path.join(input_dir, image_file)
                    image_demo = cv2.imread(image_path)
                    h_ori, w_ori = image_demo.shape[0:2]

                    output_path = os.path.join(output_dir, f"output_{image_file}")
                    for i in range(prob.shape[0] - 1, -1, -1):
                        xmin = int(loc[i][0] * w_ori)
                        ymin = int(loc[i][1] * h_ori)
                        xmax = int(loc[i][2] * w_ori)
                        ymax = int(loc[i][3] * h_ori)
                        score = prob[i]
                        class_coco = COCO_DICT[label[i]]
                        if score < score_threshold:
                            break
                        cv2.rectangle(image_demo, (xmin, ymin), (xmax, ymax), (0, 0, 255), 1)
                        cv2.putText(image_demo, str(class_coco), (xmin, ymin), cv2.FONT_HERSHEY_COMPLEX, 0.5,
                                    (100, 200, 200), 1)
                        cv2.putText(image_demo, str(score), (xmin, ymin + 15), cv2.FONT_HERSHEY_COMPLEX, 0.5,
                                    (100, 200, 200), 1)
                    cv2.imwrite(output_path, image_demo)

                images = None

if __name__ == '__main__':
    graph_def_path = 'resnet34_tf.22.5.nhwc.pb'
    output_dir = './output_images'

    argv = {i: sys.argv[i] for i in range(len(sys.argv))}
    input_dir = argv.get(1, './coco/val2017')
    batch_size = int(argv.get(2, os.environ.get("BATCH_SIZE", 1)))
    num_images = int(argv.get(3, os.environ.get("NB_IMGS", 4)))
    if batch_size > num_images:
        print(f"ERROR: a batch size of {batch_size} has been requested, but only {num_images} images are going to be executed")
        exit(1)
    run_inference_for_demo(graph_def_path, input_dir, output_dir, num_images=num_images, batch_size=batch_size)
