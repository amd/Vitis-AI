# -*- coding: utf-8 -*-
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================
# flake8: noqa
# isort:skip_file

from __future__ import print_function
import os
import sys
from threading import Thread, Lock
from time import time, sleep, perf_counter
from collections import deque
from itertools import zip_longest

import numpy as np

from .Stats import Stats
from .utils import log, grouper
from . import Cicero, Proteus, PostProcess, GoldManager, Mercury, Image
from .Classifier import Classifiers


class Dir:
    def __init__(self, name):
        self._name = name

    def getName(self):
        return self._name

    name = property(getName)


class Model(object):
    """Class containing all files that describe the network.
    - the model files for the network (depending on the framework)
    - the label file for the output class labels
    """

    NETWORK_FILENAME = "network"
    TRAINED_MODEL_FILENAME = "trained_model"
    LABELS_FILENAME = "labels"

    def __init__(self, model_path_, labels_file_=None, networkVersion=None):
        self.path = model_path_
        # this is internal specific
        self.name = os.path.basename(os.path.dirname(model_path_))

        # Every file variable is a property and will be opened at demand.
        self._network = None
        self._trained_data = None
        self._labels_file = labels_file_ if labels_file_ is not None else self.LABELS_FILENAME
        self._labels = None
        self.networkVersion = networkVersion

    @property
    def network(self):
        if self._network is None:
            # take the right version number
            n = self.NETWORK_FILENAME
            if self.networkVersion is not None:
                n += f".{self.networkVersion}"
            self._network = self._try_open(n, "rb")
        return self._network

    @property
    def trained_data(self):
        if self._trained_data is None:
            self._trained_data = self._try_open(self.TRAINED_MODEL_FILENAME, "rb")
        return self._trained_data

    @property
    def labels(self):
        if self._labels is None:
            self._labels = self._try_open(self._labels_file, "r")
        return self._labels

    def have_labels(self):
        return os.path.exists(os.path.join(self.path, self._labels_file))

    def _try_open(self, filename, mode="r"):
        try:
            if os.path.isfile(os.path.join(self.path, filename)):
                return open(os.path.join(self.path, filename), mode)
            elif os.path.isdir(os.path.join(self.path, filename)):
                return Dir(os.path.join(self.path, filename))
            else:
                raise Exception(f"{filename} does not exist in {self.path} directory")
        except Exception:
            log.error(
                f"<model path> argument (value={self.path}) must contain the following files:",
                file=sys.stderr,
            )
            log.error(f"- {self.NETWORK_FILENAME}", file=sys.stderr)
            log.error(
                f"- {self.TRAINED_MODEL_FILENAME} (Optional depending on framework)",
                file=sys.stderr,
            )
            log.error(f"- {self.LABELS_FILENAME}", file=sys.stderr)
            raise


class Batcher(object):
    """The batcher class (maybe remove later to simplify everything)"""

    def __init__(
        self,
        networkName_,
        classifier_,
        preProcess_,
        postProcess_,
        gold_,
        mercury_=None,
        displayResults_=False,
        predictFile_=sys.stdout,
    ):
        self.networkName = networkName_
        self.classifier = classifier_
        self.preProcess = preProcess_
        self.postProcess = postProcess_
        self.gold = gold_
        self.display = displayResults_
        self.predictFile = predictFile_
        self.stats = Stats(self.preProcess.batch_size, preProcess_.nbImages)
        # handling demo web
        self.mercury = mercury_
        self.batch_count = 0  # helper of the number of batch seen so far

    def end(self):
        """All ending actions that should be called batcher destruction"""
        if self.mercury is not None:
            self.mercury.end()
        if self.classifier.summary:
            self.classifier.summary()

    def run(self, batch_, useMutex_=False, mutex_=None):
        """Run the classifier through one batch"""

        if useMutex_ == True:
            mutex_.acquire(1)

        return self.classifier.run(batch_, self.stats)

    def post_run(
        self, run_outputs, batchOffset_, batchIndex_, batch_, useMutex_=False, mutex_=None
    ):
        log.info(f"Predict time -> {self.stats.predict_time} s")
        log.info(f"vaisw time -> {self.stats.npu_time} s")

        # Post processing
        with self.stats.postprocess_stats():
            results = self.postProcess.postProcess(batch_, run_outputs, batchIndex_)
        log.info(f"PostProcess time -> {self.stats.postprocess_time} s")

        # Handling Demo web
        if self.mercury is not None:
            self._handle_demo_web(batch_, results)

        if self.gold.valid():
            self.gold.sendPredictions(batch_, results)

        if self.display:
            self._display_results(batch_.batch_size, batchOffset_ + batchIndex_, results)

        if useMutex_ == True:
            mutex_.release()

        if os.getenv("VAISW_DEBUG_WAIT_AFTER_FIRST_BATCH"):
            if batchIndex_ == 0:
                sleep(5)
            else:
                sleep(0.1)

    def run_confrontation(self):
        """Confront results of the last run with gold (if it exists)."""
        if self.gold.valid():
            self.gold.confrontation(self.networkName, self.stats)

    def save_stats(self, stats_file):
        """Finnish stats and save them to the corresponding file"""
        self.stats.print_stats(stats_file, self.networkName)

    def _display_results(self, batchSize_, batchIndex_, results_):
        if len(results_) > 0 and isinstance(results_[0], Image.Image):
            # Nothing to display if results is an image.
            # TODO: find more generic way of doing that.
            return
        for imageIndex, res in enumerate(results_):
            print(
                f"{self.networkName} Image {batchSize_ * batchIndex_ + imageIndex} ({batchIndex_}:{imageIndex}) {res.image}",
                file=self.predictFile,
            )
            if self.gold.valid():
                print(
                    self.gold.display(res.image, f"{self.networkName}    GOLD - "),
                    file=self.predictFile,
                )
            print(res.display(f"{self.networkName}    PRED - "), file=self.predictFile)
            print(f"{self.networkName}", file=self.predictFile)

    def _handle_demo_web(self, batch_, results_):
        msg_id = self.batch_count
        self.batch_count += 1
        for img, res_img in zip(batch_, results_):
            self.mercury.pushImage(
                msg_id, img.path, res_img.preds, self.gold[res_img.image].preds[0].label
            )
        self.mercury.increaseTimeNeededOnPredict(msg_id, self.stats.npu_time)
        self.mercury.setModelName(msg_id, self.classifier.get_model_name())
        self.mercury.sendMail(msg_id)

    def train(self):
        """Train the classifier through all the batches"""
        # prepare labels
        # TODO: redo the dirToClass and fileToClass dict which should not be in the
        #       classifier but more in the gold manager.
        labels = []
        log.log("---------- Prepare Labels ----------")
        for batchIndex, batch in enumerate(self.preProcess.file_list):
            crtLabels = []
            prop = float(batchIndex * 100) / self.preProcess.nbBatches
            propInt = int(prop)
            for k in range(len(batch)):
                file = batch[k]
                if self.preProcess.dir_mode:
                    assert file in self.preProcess.fileDirectoryHash
                    dir = self.preProcess.fileDirectoryHash[file]
                    assert dir in self.classifier.dirToClass
                    crtLabels.append(self.classifier.dirToClass[dir])
                else:
                    assert file in self.classifier.fileToClass
                    crtLabels.append(self.classifier.fileToClass[file])
            if (prop % 10 == 0) and (prop == propInt):
                log.log(f"{propInt}% of labels loaded")
            labels.append(crtLabels)
        log.log("labels loaded")

        log.log("---------- Training ----------")
        self.classifier.train(self.preProcess, labels)


def get_sw_batcher(cicero, classifier, networkName):
    # get gold data
    gold = None
    log.log("Gold instantiation...")
    gold = GoldManager.GoldFactory(cicero.predictMode, cicero.goldFile, cicero.displayImages)
    gold.setForm(cicero.gform)
    if cicero.testList:
        for test in cicero.testList.split(":"):
            gold.addTest(test)

    # Create image preprocessor
    proteus = Proteus.Proteus(
        cicero.imgPath,
        cicero.nbImages,
        cicero.batchSize,
        nbThreads=cicero.ppThreads,
        keys=gold,
        randomize=cicero.randomize,
        offset=cicero.imgOffset,
        repeat=cicero.repeat,
        preLoadImages=cicero.preLoadImages,
        imgDtype=cicero.imgDtype,
    )
    proteus.set_imgReader(cicero.imgReader)
    proteus.set_preproc(cicero.preProcessingProg)

    classifier.setup_preprocess(proteus)

    # Create post process
    pp = PostProcess.PostProcessing(cicero.postProcessingProg, classifier.labels)

    # Demo Web
    mercury = None
    if cicero.mode == cicero.RunMode.DEMO_BIN:
        if cicero.demoPath is None:
            raise RuntimeError("You need to set the demoPath when running the Demo mode.")
        if not os.path.isdir(cicero.demoPath):
            os.makedirs(cicero.demoPath)
        mercury = Mercury.FileEmitter(cicero.demoPath)

    return Batcher(
        networkName,
        classifier,
        proteus,
        pp,
        gold,
        mercury,
        cicero.displayResults,
        cicero.predictFile,
    )


class RunOnOneThread(Thread):
    def __init__(self, batcher_, mutex_):
        Thread.__init__(self)
        self.batcher = batcher_
        self.mutex = mutex_
        self.result = 0

    def run(self):
        log.log(f"Run network {self.batcher.networkName}")
        for batchIndex, batch in enumerate(self.batcher.preProcess):
            outputs = self.batcher.run(batch, True, self.mutex)
            self.batcher.post_run(outputs, 0, batchIndex, batch, True, self.mutex)
        self.result = 0

    def join(self):
        Thread.join(self)
        return self.result


def main(framework, argv, exec_name=None):
    """The main function that will parse args, create all objects according
    to options and run the batcher.
    """
    cicero = Cicero.ArgumentReader(argv, name=exec_name)
    if cicero.embedded:
        framework = "embedded"
    batchers = []

    for modelPath, snapshotPath in zip_longest(cicero.modelPathsList, cicero.snapshotPathsList):
        model = Model(modelPath, cicero.labels, cicero.networkVersion)
        classifier = Classifiers.get(
            framework,
            model,
            cicero.imageShape,
            cicero.outputNames,
            embeddedSnapshot=snapshotPath,
            execMode=cicero.execMode,
        )

        batchers.append(get_sw_batcher(cicero, classifier, model.name))

    start_time = time()
    if not cicero.training:
        log.log("Predict mode")

        if len(batchers) > 1:
            if cicero.modelPath.find("+") != -1:
                batchInc = 5
                # same preProcess for all networks
                for superBatchIndex, superBatch in enumerate(
                    grouper(batchers[0].preProcess, batchInc)
                ):
                    for batcher_ in batchers:
                        log.log(f"Run network {batcher_.networkName}")
                        for batchIndex, batch in enumerate(superBatch):
                            if batch != None:
                                outputs = batcher_.run(batch)
                                batcher_.post_run(
                                    outputs, superBatchIndex * batchInc, batchIndex, batch
                                )

            if cicero.modelPath.find("=") != -1:
                threadIds = []
                mutex = Lock()
                for batcher__ in batchers:
                    log.log(f"Run network {batcher__.networkName}")
                    threadIds.append(RunOnOneThread(batcher__, mutex))
                    threadIds[-1].start()

                if sum(thread.join() for thread in threadIds) != 0:
                    return 1
        else:
            for batchIndex, batch in enumerate(batchers[0].preProcess):
                outputs = batchers[0].run(batch)
                batchers[0].post_run(outputs, 0, batchIndex, batch)

            while cicero.mode == cicero.RunMode.DEMO_BIN and time() - start_time < cicero.demoTime:
                # repeat for the demo web
                batchers[0].gold.resetPredictions()
                for batchIndex, batch in enumerate(batchers[0].preProcess):
                    outputs = batchers[0].run(batch)
                    batchers[0].post_run(outputs, 0, batchIndex, batch)

        for batcher in batchers:
            batcher.run_confrontation()
            if cicero.statsFile is not None:
                batcher.save_stats(cicero.statsFile)
    else:
        log.log("Training mode")
        for batcher in batchers:
            batcher.train()

    # Calling all ending stuff now.
    for batcher in batchers:
        batcher.end()
