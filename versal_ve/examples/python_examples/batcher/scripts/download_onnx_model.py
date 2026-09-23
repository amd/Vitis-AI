
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import torch
import torchvision
import os
import sys

VERSION = torchvision.__version__.split('+')[0]

full_list_weights = list(torchvision.models.__dict__.items())
full_list_weights.extend(list(torchvision.models.segmentation.__dict__.items()))
full_list_weights.extend(list(torchvision.models.optical_flow.__dict__.items()))
weights_list = {k.lower()[:-len("_weights")]: v.DEFAULT for k,v in full_list_weights if k.endswith("_Weights")}

local_to_hub = {
    "googlenet_no_lrn" : "googlenet",
    "inceptionv3"      : "inception_v3",
    "squeezenet"       : "squeezenet1_0"
}

model_url_keys = {
    "shufflenet_v2_x0_5" : "shufflenetv2_x0.5",
    "shufflenet_v2_x1_0" : "shufflenetv2_x1.0",
}

def dl_model(m_name, network_dir, version, hw):
    # m_name is the name of the file in the arborescence models/<m_name>/pytorch

    # pytorch_m_name is the model's name for the torch.hub call
    pytorch_m_name = local_to_hub.get(m_name, m_name)

    print(f"Downloading ONNX model {pytorch_m_name} via torch.hub version {version}")

    w = weights_list[pytorch_m_name]
    model = torch.hub.load(f'pytorch/vision:v{version}', pytorch_m_name, weights=w, verbose=False)
    hash_m_name = w.value.url.split('/')[-1]

    hash_m_name = os.path.splitext(hash_m_name)[0] + ".onnx"
    m_path = os.path.join(os.path.expanduser(network_dir), hash_m_name)
    print(f"Saving model {m_name} to {m_path}")

    height, width = map(int, hw.split("x"))
    dummy_input = torch.randn(1,3,height,width)
    torch.onnx.export(model, dummy_input, m_path, input_names=["input"], output_names=["output"], dynamic_axes={"input": {0: "batch_size"}, "output": {0: "batch_size"}})
    os.symlink(hash_m_name, os.path.join(network_dir, f"network.{version}"))

if __name__=="__main__":
    # workaround for rate limit exceeded https://github.com/pytorch/vision/issues/4156#issuecomment-886005117
    torch.hub._validate_not_a_forked_repo=lambda a,b,c: True

    if len(sys.argv) < 5:
        print("Some arguments are missing! Command line was:")
        sys.exit("python3 " + " ".join(sys.argv))

    dl_model(*sys.argv[1:5])
