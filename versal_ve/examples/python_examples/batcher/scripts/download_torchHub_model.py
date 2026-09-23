
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

object_detection = {k.lower()[:-len("_weights")]: v.DEFAULT for k,v in torchvision.models.detection.__dict__.items() if k.endswith("_Weights")}

local_to_hub = {
    "googlenet_no_lrn" : "googlenet",
    "inceptionv3"      : "inception_v3",
    "squeezenet"       : "squeezenet1_0"
}

model_url_keys = {
    "shufflenet_v2_x0_5" : "shufflenetv2_x0.5",
    "shufflenet_v2_x1_0" : "shufflenetv2_x1.0",
}

def dl_model(m_name, network_dir, version):
    # m_name is the name of the file in the arborescence models/<m_name>/pytorch

    # pytorch_m_name is the model's name for the torch.hub call
    pytorch_m_name = local_to_hub.get(m_name, m_name)

    print(f"Downloading model {pytorch_m_name} via torch.hub version {version}")

    if pytorch_m_name in object_detection:
        w = object_detection[pytorch_m_name]
        model_func = getattr(torchvision.models.detection, pytorch_m_name)
        model = model_func(weights=w)
        model.eval()
    else:
        w = weights_list[pytorch_m_name]
        model = torch.hub.load(f'pytorch/vision:v{version}', pytorch_m_name, weights=w, verbose=False)

    hash_m_name = w.value.url.split('/')[-1]
    m_path = os.path.join(os.path.expanduser(network_dir), hash_m_name)
    print(f"Saving model {m_name} to {m_path}")
    torch.save(model, m_path)
    os.symlink(hash_m_name, os.path.join(network_dir, f"network.{version}"))

if __name__=="__main__":
    # workaround for rate limit exceeded https://github.com/pytorch/vision/issues/4156#issuecomment-886005117
    torch.hub._validate_not_a_forked_repo=lambda a,b,c: True

    if len(sys.argv) < 4:
        sys.exit("Need to specify torchvision version for download")
    version = sys.argv[3]

    dl_model(sys.argv[1], sys.argv[2], version)
