
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import sys
import tensorflow as tf

network_names = {
    "vgg16"       : tf.keras.applications.VGG16,
    "vgg19"       : tf.keras.applications.VGG19,
    "densenet121" : tf.keras.applications.DenseNet121,
    "densenet169" : tf.keras.applications.DenseNet169,
    "densenet201" : tf.keras.applications.DenseNet201,
    "xception"    : tf.keras.applications.Xception,
# The following are the other NN available in tf.keras.applications
#tf.keras.applications.EfficientNetB0
#tf.keras.applications.EfficientNetB1
#tf.keras.applications.EfficientNetB2
#tf.keras.applications.EfficientNetB3
#tf.keras.applications.EfficientNetB4
#tf.keras.applications.EfficientNetB5
#tf.keras.applications.EfficientNetB6
#tf.keras.applications.EfficientNetB7
#tf.keras.applications.InceptionResNetV2
#tf.keras.applications.InceptionV3
#tf.keras.applications.MobileNet
#tf.keras.applications.MobileNetV2
#tf.keras.applications.MobileNetV3Large
#tf.keras.applications.MobileNetV3Small
#tf.keras.applications.NASNetLarge
#tf.keras.applications.NASNetMobile
#tf.keras.applications.ResNet101
#tf.keras.applications.ResNet101V2
#tf.keras.applications.ResNet152
#tf.keras.applications.ResNet152V2
#tf.keras.applications.ResNet50
#tf.keras.applications.ResNet50V2

}

def main():
    m_name = sys.argv[1]
    directory = sys.argv[2]

    if m_name not in network_names:
        print(f"ERROR downloading model {m_name} not implemented")
        exit(1)

    app = network_names[m_name]
    model = app()
    model.save(directory)

if __name__=="__main__":
    main()
