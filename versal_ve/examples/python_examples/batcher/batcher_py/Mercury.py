# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


import ctypes
import os
from multiprocessing import Array
from multiprocessing import Process
from multiprocessing import Queue

from .utils import get_libc_env


class Message:
    class Image:
        def __init__(self, path, preds, goldBestPreds):
            self.path = path
            self.preds = preds
            self.goldBestPreds = goldBestPreds

        def format(self):
            r = []
            r.append(f'"fileName":"{os.path.basename(self.path)}"')
            r.append(f'"filePath":"{os.path.dirname(self.path)}"')
            if self.goldBestPreds in self.preds:
                r.append(f'"goldBestPredIdx":{self.preds.index(self.goldBestPreds)}')
            r.append(f'"nbPreds":{len(self.preds)}')
            p = []
            for pred in self.preds:
                p.append(f'{{"label":"{pred.label}"')
                p.append(f'"prob":{pred.probability:.06}}}')
            r.append('"preds":[{}]'.format(",".join(p)))
            return ",".join(r)

    def __init__(self, ind):
        self.batchIndex = ind
        self.wastedTime = 0.0  # see if it is always relevant
        self.TimeNeededOnPredict = 0.0
        self.images = []
        self.nbSystemsPerBoard = 0
        self.nbCoresPerSystem = 0
        self.nbNcesPerCore = 0
        self.frequency = 0
        self.neuronType = ""
        self.boardName = ""
        self.modelName = ""

    def pushImage(self, path, preds, goldBestPreds):
        self.images.append(Message.Image(path, preds, goldBestPreds))

    def increaseWastedTime(self, inc):
        self.wastedTime += inc

    def increaseTimeNeededOnPredict(self, inc):
        self.TimeNeededOnPredict += inc

    def setModelName(self, modelName):
        self.modelName = modelName

    def format(self):
        r = []
        r.append(f'"batchIndex":{self.batchIndex}')
        r.append(f'"wastedTime":{self.wastedTime}')
        r.append(f'"TimeNeededOnPredict":{self.TimeNeededOnPredict}')
        r.append(f'"nbImages":{len(self.images)}')
        r.append(f'"nbSystemsPerBoard":{self.nbSystemsPerBoard}')
        r.append(f'"nbCoresPerSystem":{self.nbCoresPerSystem}')
        r.append(f'"nbNcesPerCore":{self.nbNcesPerCore}')
        r.append(f'"frequency":{self.frequency}')
        r.append(f'"neuronType":"{self.neuronType}"')
        r.append(f'"boardName":"{self.boardName}"')
        r.append(f'"modelName":"{self.modelName}"')
        r.append(f'"imageListSize":{len(self.images)}')
        img = [f"{{{i.format()}}}" for i in self.images]
        r.append('"images":[{}]'.format(",".join(img)))
        return "{{{}}}".format(",".join(r))


class Mercury:
    """Message Emitter interface."""

    EXIT_MESSAGE = "STOP"

    def __init__(self):
        self.mails = {}
        self.queue = Queue()

    def __del__(self):
        self.queue.put_nowait(Mercury.EXIT_MESSAGE)

    def end(self):
        self.queue.put_nowait(Mercury.EXIT_MESSAGE)

    def _getMail(self, msg_id):
        if msg_id not in self.mails:
            msg = Message(msg_id)
            msg.nbSystemsPerBoard = get_libc_env("VAISW_nbSystemsPerBoard", 0)
            msg.nbCoresPerSystem = get_libc_env("VAISW_nbCoresPerSystem", 0)
            msg.nbNcesPerCore = get_libc_env("VAISW_nbNcesPerCore", 0)
            msg.frequency = int(float(get_libc_env("VAISW_usedRunFrequency", 0)))
            msg.neuronType = get_libc_env("VAISW_precision", "FP32")
            msg.boardName = get_libc_env("VAISW_boardName", "GPU")
            self.mails[msg_id] = msg
        return self.mails[msg_id]

    def sendMail(self, msg_id):
        self.queue.put_nowait(self._getMail(msg_id))
        del self.mails[msg_id]

    def pushImage(self, msg_id, path, preds, goldBestPreds):
        self._getMail(msg_id).pushImage(path, preds, goldBestPreds)

    def increaseWastedTime(self, msg_id, inc):
        self._getMail(msg_id).increaseWastedTime(inc)

    def increaseTimeNeededOnPredict(self, msg_id, inc):
        self._getMail(msg_id).increaseTimeNeededOnPredict(inc)

    def setModelName(self, msg_id, modelName):
        self._getMail(msg_id).setModelName(modelName)


class FileEmitter(Mercury):
    """Send messages to a file"""

    @staticmethod
    def __worker(mailq, dstPath):
        for count, message in enumerate(iter(mailq.get, Mercury.EXIT_MESSAGE)):
            dst = os.path.join(dstPath.value.decode(), f"message_{count}.json")
            with open(dst, "w") as f:
                print(message.format(), file=f)

    def __init__(self, dstPath):
        if dstPath is None or len(dstPath) == 0:
            raise ValueError("Anonymous destination path is not a thing!")
        if not os.path.isdir(dstPath):
            raise RuntimeError(f"There is a problem with the directory {dstPath}")
        super().__init__()
        dst = Array(ctypes.c_char, dstPath.encode())
        self.p = Process(target=FileEmitter.__worker, args=(self.queue, dst))
        self.p.start()
