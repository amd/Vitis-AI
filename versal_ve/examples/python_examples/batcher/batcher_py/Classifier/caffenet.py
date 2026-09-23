# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import torch.nn as nn


class Flatten(nn.Module):
    def forward(self, x):
        return x.view(x.size(0), -1)


class CaffeNet(nn.Sequential):
    def __init__(self):
        super().__init__()
        self.index = 0
        # self.layers=[]
        # layer 1
        self.__add__(nn.Conv2d(in_channels=3, out_channels=96, kernel_size=11, stride=4))
        self.__add__(nn.ReLU())
        self.__add__(nn.MaxPool2d(kernel_size=3, stride=2))

        # layer 2
        self.__add__(nn.ZeroPad2d(padding=2))
        self.__add__(nn.Conv2d(in_channels=96, out_channels=256, kernel_size=5, groups=2))
        self.__add__(nn.ReLU())
        self.__add__(nn.MaxPool2d(kernel_size=3, stride=2))

        # layer 3
        self.__add__(nn.ZeroPad2d(padding=1))
        self.__add__(nn.Conv2d(in_channels=256, out_channels=384, kernel_size=3))
        self.__add__(nn.ReLU())

        # layer 4
        self.__add__(nn.ZeroPad2d(padding=1))
        self.__add__(nn.Conv2d(in_channels=384, out_channels=384, kernel_size=3, groups=2))
        self.__add__(nn.ReLU())

        # layer 5
        self.__add__(nn.ZeroPad2d(padding=1))
        self.__add__(nn.Conv2d(in_channels=384, out_channels=256, kernel_size=3, groups=2))
        self.__add__(nn.ReLU())
        self.__add__(nn.MaxPool2d(kernel_size=3, stride=2))

        # layer 6
        self.__add__(Flatten())

        # layer 7
        self.__add__(nn.Linear(9216, 4096))  # 19-th layer, normally 4096 neurons
        self.__add__(nn.ReLU())
        self.__add__(nn.Dropout())

        # layer 8
        self.__add__(nn.Linear(4096, 4096))
        self.__add__(nn.ReLU())
        self.__add__(nn.Dropout())

        # layer 9
        self.__add__(nn.Linear(4096, 1000))
        # self.__add__(nn.LogSoftmax(dim=0))

    def __add__(self, layer):
        self._modules[str(self.index)] = layer
        self.index = self.index + 1

    """def forward(self, x):
        k=0
        for layer in self.layers:
            print("layer num "+str(k))
            x=layer(x)
            k=k+1
        return x"""


# net=CaffeNet()

"""
#torch.save(net,"caffenet.torch")
input = torch.randn(20, 3, 227, 227,requires_grad=False)
input_names = [ "input" ]
output_names = [ "output" ]

torch.onnx.export(net,input,"caffenet.onnx",verbose=True, input_names=input_names, output_names=output_names)
"""
