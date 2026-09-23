# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


from itertools import chain
from time import perf_counter

from . import utils


class Stats:
    """Class to handle all stats of classifier run"""

    VAISW_RUNTIME_NAME = "VAISW_lastRunElapsedTime"

    class Timer:
        """To allowing the use of the with statement for statistics"""

        def __init__(self, update_func, *args, **kwargs):
            self.up = lambda t: update_func(t, *args, **kwargs)
            self.start_time = 0.0

        def __enter__(self):
            self.start_time = perf_counter()

        def __exit__(self, type, value, traceback):
            self.up(perf_counter() - self.start_time)

    def __init__(self, batchSize=-1, nbImages=-1):
        self.batchSize = batchSize
        self.nbImages = nbImages
        self.count = 0
        self.predict_time = 0.0
        self.npu_time = 0.0
        self.full_time = 0.0
        self.total_npu_time = 0.0
        self.postprocess_time = 0.0
        self.waisted_time = 0.0
        self.confrontations = []
        self._logInfo = None

    @property
    def logInfo(self):
        if self._logInfo is None:
            try:
                import vaisw

                self._logInfo = vaisw.logInfo
            except Exception:
                self._logInfo = lambda x: print(x, end="")
        return self._logInfo

    def predict_stats(self, log_time=False):
        return self.Timer(self._update_predict_stats, log_time=log_time)

    def postprocess_stats(self):
        return self.Timer(self._update_postprocess_stats)

    def _update_predict_stats(self, predict_time, log_time=False):
        """Internal function to update the prediction stats"""
        self.predict_time = predict_time
        self.npu_time = float(utils.get_libc_env(Stats.VAISW_RUNTIME_NAME, 0))
        if self.count > 0:
            # Ignore first run
            self.full_time += self.predict_time
            self.total_npu_time += self.npu_time
        self.count += 1
        if log_time:
            self.logInfo(f"[USER] Batch duration : {predict_time * 1e3:.4}ms\n")

    def _update_postprocess_stats(self, pp_time):
        self.postprocess_time = pp_time
        self.waisted_time += pp_time

    def save_confrontation(self, confrontations):
        self.confrontations.append(confrontations)

    def print_stats(self, outfile, networkName):
        if self.total_npu_time != 0:
            total_fpga_time = (self.nbImages - self.batchSize) / self.total_npu_time
        else:
            total_fpga_time = 0
        if self.count > 1:
            cpu_time = (self.full_time - self.total_npu_time) / (self.count - 1)
        else:
            cpu_time = 0
        conf = ",".join(map(str, chain.from_iterable(self.confrontations)))
        print(
            networkName,
            self.batchSize,
            self.nbImages,
            total_fpga_time,
            cpu_time,
            conf,
            sep=",",
            file=outfile,
        )
