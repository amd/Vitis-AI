# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

"""
Module to handle gold files and makes comparison with results from network.
"""

import itertools
import json
import os
from enum import Enum
from tempfile import NamedTemporaryFile

import numpy as np

from . import Image
from . import utils
from .PostProcess import Prediction


def GoldFactory(predictMode, goldFile=None, displayImages=False):
    """Factory to create and parse the gold object according to the predict mode."""
    gf = {
        "TOP_N": PredictGold,
        "TOP_N_DEMO": PredictGoldDemo,
        "BOXED": BoxGold,
        "IMAGE_COMPARISON": ImageGold,
        "COCO": CocoGold,
    }
    return gf[predictMode](goldFile, displayImages)


class Gold:
    """Base class for all gold analyse classes"""

    class Form(Enum):
        """Is the gold predictor "en forme" ?.
        This is only valid for image predictor, not boxed.
        """

        PESSIMISTIC = 1
        OPTIMISTIC = 2

        def __str__(self):
            return self.name

    def __init__(self, gold_file=None):
        # gold will associate image name to gold results
        self.gold = {}
        self.gold_file = gold_file
        if gold_file is not None:
            with open(gold_file) as f:
                self._parse_gold(f)
        self.results = []

    def valid(self):
        """gold is valid"""
        return self.gold_file is not None

    def _parse_gold(self, gold_file):
        """Virtual interface to read the gold file"""
        raise NotImplementedError()

    def __getitem__(self, key):
        """Allow to use the Gold class as a dict."""
        return self.gold[key]

    def __iter__(self):
        """Allow to iter on the Gold class keys."""
        return iter(self.gold)

    def __contains__(self, key):
        """Check if the key is valid for this gold object"""
        return key in self.gold

    def display(self, im, prefix=""):
        """Return string representing the results. prefix is placed in front of all lines."""
        if im in self.gold:
            return self.gold[im].display(prefix)
        else:
            # No gold
            return prefix + "N/A"

    def resetPredictions(self):
        self.results = []

    def setForm(self, form):
        """Do nothing, overridden by child classes when necessary."""
        pass

    def addTest(self, test):
        """Add test to the GoldManager for the confrontation."""
        raise NotImplementedError("Child classes must overwrite this function.")


class PredictGold(Gold):
    """Class that represent gold file for detection image set (imagenet)."""

    class __Test:
        """The Predict gold test functions."""

        @staticmethod
        def top1(res):
            nb_images = len(res)
            return float(res.count(0)) / nb_images

        @staticmethod
        def top5(res):
            nb_images = len(res)
            return float(len([x for x in res if x >= 0])) / nb_images

        @staticmethod
        def bestPredIsInResults(res):
            # This is equivalent to top5 and kept for legacy purpose.
            # In the older version of top5, the top5 was limited to the 5 first result
            # but that did not make a lot of sense in the case of equality at the 5th position.
            # This is why this function was used.
            nb_images = len(res)
            return float(len([x for x in res if x >= 0])) / nb_images

    _TEST_LIST = {
        "top1": __Test.top1,
        "top5": __Test.top5,
        "bestPredIsInResults": __Test.bestPredIsInResults,
    }

    def __init__(self, gold_file, *_args):
        super().__init__(gold_file)
        self.form = Gold.Form.PESSIMISTIC
        self.tests = []

    def setForm(self, form):
        self.form = form

    def addTest(self, test):
        """Add new test in the confrontation"""
        if test not in self._TEST_LIST:
            raise ValueError(f"Unsupported test {test}")
        self.tests.append((test, self._TEST_LIST[test]))

    def _parse_gold(self, gold_file):
        """Get gold file as open file and parse it to fill the object.
        Support only gold file from topn prediction.
        """
        SEPARATOR = "|"
        SUBSEPARATOR = ";"

        # First line
        gold_file.seek(0)
        line1 = gold_file.readline().strip(" \r\n" + SEPARATOR).split(SEPARATOR)
        cls = {}
        for e in line1:
            v, k = e.split(SUBSEPARATOR)
            cls[k] = v

        # All other lines
        for l in gold_file:
            im, cl = l.strip().split(SEPARATOR)
            cl = cl.split("_")[0]
            self.gold.setdefault(im, Prediction.Classification(im)).add_prediction(cls[cl], 1.0)

    def confrontation(self, networkName, stats=None):
        """Confront result to the gold by running the list of tests setup in the initialisation"""
        with utils.Tee(utils.getVaiswLogFile(), mode="a") as f:
            r = []
            for test_name, test_func in self.tests:
                r.append(test_func(self.results))
                print(
                    f"[AMD] [{networkName} TEST {test_name}] {r[-1] * 100:.1f}% passed.",
                    file=f,
                )
            if len(r) > 0:
                print(f"[AMD] [{networkName} ALL TESTS] {min(r) * 100:.1f}% passed.", file=f)
                if stats is not None:
                    stats.save_confrontation(r)

    def sendPredictions(self, _batch, predict):
        """Allow to send predictions at each batch to the gold Manager.
        The predict must be a list of list of predictions
        """
        assert isinstance(predict[0], Prediction.Classification), (
            "PredictGold should receive Classification predictions"
        )
        self.results.extend(self._checkTOP(predict))

    def _checkTOP(self, results):
        """Check results with the gold. RESULTS must be a list of TOPNPrediction.
        Return a list of index of the correct prediction or -1 if None is correct.
        The form of the result may be optimistic or pessimistic for the top 1 depending
        on the object option.type filter text
        """
        r = []
        for topn in results:
            g = self.gold.get(topn.image)
            waited_label = g[0].label if g is not None else None
            for i, p in enumerate(topn):
                if p.label == waited_label:
                    if self.form == Gold.Form.OPTIMISTIC and p.probability == topn[0].probability:
                        r.append(0)
                    elif (
                        self.form == Gold.Form.PESSIMISTIC
                        and i == 0
                        and topn[0].probability == topn[1].probability
                    ):
                        # pessimist invalid top1
                        r.append(1)
                    else:
                        r.append(i)
                    break
            else:
                r.append(-1)
        return r


class PredictGoldDemo(PredictGold):
    def _parse_gold(self, gold_file):
        # Simple parsing
        for l in gold_file:
            im, label = l.strip().split(None, 1)
            self.gold.setdefault(im, Prediction.Classification(im)).add_prediction(label, 1.0)


class BoxGold(Gold):
    """Class that represent gold file for boxed image set (pascal, coco, ...)."""

    def __init__(self, gold_file=None, *_args):
        super().__init__(gold_file)
        self.classes = set()

    def _parse_gold_legacy(self, gold_file):
        """Get gold file as open file and parse it to fill the object.
        Support only gold file from box prediction.
        """
        SEPARATOR = "|"
        SUBSEPARATOR = ";"

        # First line
        line1 = gold_file.readline().strip(" \r\n" + SEPARATOR).split(SEPARATOR)
        cls = {}
        for e in line1:
            v, k = e.split(SUBSEPARATOR)
            cls[k] = v

        # All other lines
        for l in gold_file:
            img, x, y, w, h, im_w, im_h, cl = l.split()
            # Normalize the box
            bp = utils.Box(
                float(x) / float(im_w),
                float(y) / float(im_h),
                float(w) / float(im_w),
                float(h) / float(im_h),
            )
            self.gold.setdefault(img, Prediction.Detection(img)).add_prediction(cls[cl], 1.0, bp)

    def _parse_gold_json(self, gold_file):
        js = json.load(gold_file)
        images = {i["id"]: (i["file_name"], i["width"], i["height"]) for i in js["images"]}
        cls = {c["id"]: c["name"] for c in js["categories"]}
        for a in js["annotations"]:
            im, im_w, im_h = images[a["image_id"]]
            x, y, w, h = a["bbox"]
            # Normalize the box
            bp = utils.Box(
                float(x) / float(im_w),
                float(y) / float(im_h),
                float(w) / float(im_w),
                float(h) / float(im_h),
            )
            self.gold.setdefault(im, Prediction.Detection(im)).add_prediction(
                cls[a["category_id"]], 1.0, bp
            )

    def _parse_gold(self, gold_file):
        if gold_file.name.endswith(".json"):
            self._parse_gold_json(gold_file)
        else:
            self._parse_gold_legacy(gold_file)

    def resetPredictions(self):
        super().resetPredictions()
        self.classes.clear()

    def addTest(self, test):
        """For now the BoxGold class does not support setting tests. Thus this function does nothing."""
        pass

    def sendPredictions(self, _batch, predict):
        """Allow to send predictions at each batch to the gold Manager.
        The predict must be a list of predictions
        """
        if len(predict) > 0:
            assert isinstance(predict[0], Prediction.Detection), (
                "BoxGold should receive Detection predictions"
            )
            self.results.extend(predict)
            for p in predict:
                self.classes.update([x.label for x in p])

    class Detection:
        """Detection class to store if a detect box is a true positive
        (iou > threshold with a reference in the gold)
        """

        def __init__(self, conf, TP):
            self.confidence = conf
            self.TP = TP
            self.precision = 0
            self.recall = 0

        def __repr__(self):
            return f"({self.confidence}, {self.TP}, {self.precision}, {self.recall})"

    class Interpolation:
        """Interpolation functions to calculate AP from detections.
        Need to use subclass to be able to use methods as default argument in the BoxGold class.
        """

        @staticmethod
        def ElevenPoints(detections):
            """Implement the interpolation on eleven points to calculate the average precision."""
            recalls = [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1]
            s = 0
            for rec in recalls:
                # Use chain as default max argument is only available after python 3.4
                s += max(itertools.chain((d.precision for d in detections if d.recall >= rec), [0]))
            return s / 11.0

        @staticmethod
        def EveryPoints(detections):
            """Implement the interpolation on every points to calculate the average precision."""
            i = 0
            s = 0
            rec = 0
            while i < len(detections):
                # find next element with highest precision
                n = max(detections[i:], key=lambda x: x.precision)
                # interpolated precision for current recall value
                s += n.precision * (n.recall - rec)
                rec = n.recall
                # update i
                n = next((x for x in detections[i:] if x.recall > rec), None)
                i = detections.index(n) if n is not None else len(detections)
            return s

    def _setPrecisionAndRecall(self, detections, gt_count):
        """Set precision and recall in the detections. Detections need to be sorted."""
        accTP = 0
        for count, d in enumerate(detections):
            if d.TP:
                accTP += 1
            d.precision = float(accTP) / float(count + 1)
            d.recall = float(accTP) / float(gt_count) if gt_count != 0 else 0

    def mAP(self, results, iou_threshold=0.5, method=Interpolation.EveryPoints):
        """Calcultate the pascal \"mean Average Precision\" from the predictions sent to this
        object. The iou thrshold is a parameter. The method differenciate the two possible
        pascal interpolation.
        """
        if len(self.classes) == 0:
            # Nothing detected (There should be something wrong)
            return 0
        detect = {}
        gt_count = {}
        for cls in self.classes:
            detect[cls] = []
            gt_count[cls] = 0

        # get detections
        for bp in results:
            if bp.image in self.gold:
                gt = self.gold[bp.image][:]
                for g in gt:
                    gt_count[g.label] = gt_count.get(g.label, 0) + 1
                for p in bp:
                    for g in gt:
                        if p.label == g.label and p.box.iou(g.box) > iou_threshold:
                            detect[p.label].append(BoxGold.Detection(p.probability, True))
                            gt.remove(g)
                            break
                    else:
                        detect[p.label].append(BoxGold.Detection(p.probability, False))

        # Calculate mAP
        cumAP = 0
        for l in detect:
            # get AP for each labels
            d = detect[l]
            d.sort(key=lambda x: x.TP, reverse=True)
            d.sort(key=lambda x: x.confidence, reverse=True)
            self._setPrecisionAndRecall(d, gt_count[l])
            cumAP += method(d)
        return cumAP / len(detect)

    def confrontation(self, networkName, stats=None):
        # TODO: make the list of test in the init parameteres
        """Confront result to the gold by running the list of tests setup in the initialisation."""
        mAP50 = self.mAP(self.results, 0.5)
        with utils.Tee(utils.getVaiswLogFile(), mode="a") as f:
            print(f"[AMD] [{networkName} TEST mAP-50] {mAP50 * 100:.1f}% passed.", file=f)
            print(f"[AMD] [{networkName} TEST top1_box] {0:.1f}% passed.", file=f)
            print(f"[AMD] [{networkName} ALL TESTS] {mAP50 * 100:.1f}% passed.", file=f)
            if stats is not None:
                stats.save_confrontation([mAP50, 0])


class CocoGold(Gold):
    """Specific class for Coco prediction."""

    def __init__(self, gold_file=None, *_args):
        super().__init__(gold_file)
        self.image_ids = []

    def _parse_gold(self, gold_file):
        # lazy import in case we do not use coco prediction
        from pycocotools.coco import COCO
        from pycocotools.cocoeval import COCOeval

        self.ground_truth = COCO(gold_file.name)
        js = json.load(gold_file)
        images = {i["id"]: (i["file_name"], i["width"], i["height"]) for i in js["images"]}
        cls = {c["id"]: c["name"] for c in js["categories"]}
        for a in js["annotations"]:
            im, im_w, im_h = images[a["image_id"]]
            bp = utils.Box(*a["bbox"])
            # Normalize the box
            bp /= (float(im_w), float(im_h))
            self.gold.setdefault(im, Prediction.Detection(im)).add_prediction(
                cls[a["category_id"]], 1.0, bp
            )
        self.coco_eval = COCOeval(self.ground_truth, None, "bbox")

    def addTest(self, test):
        """Not supported."""
        pass

    def resetPredictions(self):
        super().resetPredictions()
        self.image_ids = []

    def sendPredictions(self, _batch, predict):
        """Allow to send predictions at each batch to the gold Manager.
        The predict must be a list of predictions
        """
        if len(predict) > 0:
            assert isinstance(predict[0], Prediction.Detection), (
                "CocoGold should receive Detection predictions"
            )
            for p in predict:
                image_id = int(os.path.splitext(p.image)[0].split("_")[-1])
                self.image_ids.append(image_id)

                orig_w, orig_h = (
                    self.ground_truth.imgs[image_id]["width"],
                    self.ground_truth.imgs[image_id]["height"],
                )
                for d in p:
                    self.results.append({
                        "image_id": image_id,
                        "category_id": d.label.id,
                        "bbox": list(d.box * (orig_w, orig_h)),
                        "score": d.probability,
                    })

    def confrontation(self, networkName, stats=None):
        """Use coco to confront the results with annontations."""
        if len(self.results) > 0:
            with NamedTemporaryFile("w") as f:
                json.dump(self.results, f, indent=2)
                f.flush()
                coco_dt = self.ground_truth.loadRes(f.name)
            self.coco_eval.cocoDt = coco_dt
            self.coco_eval.params.imgIds = self.image_ids
            self.coco_eval.evaluate()
            self.coco_eval.accumulate()
            self.coco_eval.summarize()
            mAP, mAP50 = self.coco_eval.stats[0], self.coco_eval.stats[1]
        else:
            mAP, mAP50 = 0, 0  # empty results
        with utils.Tee(utils.getVaiswLogFile(), mode="a") as f:
            print(f"[AMD] [{networkName} TEST mAP-50] {mAP50 * 100:.1f}% passed.", file=f)
            print(f"[AMD] [{networkName} TEST top1_box] {mAP * 100:.1f}% passed.", file=f)
            print(f"[AMD] [{networkName} ALL TESTS] {mAP50 * 100:.1f}% passed.", file=f)
            if stats is not None:
                stats.save_confrontation([mAP50, mAP])


def showImage(image):
    from PIL import Image as PILImage

    pilImage = PILImage.fromarray(image.convert(Image.Color.RGB).data.astype(np.uint8), "RGB")
    pilImage.show()


class ImageComparison:
    def __init__(self, image, psnr, ssmid):
        self.image = image
        self.psnr = psnr
        self.ssmid = ssmid

    def display(self, prefix=""):
        return f"{prefix} psnr {self.psnr} ssmid {self.ssmid}"

    def __len__(self):
        return len(self.preds)

    def __repr__(self):
        return f"{self.image} psnr {self.psnr} ssmid {self.ssmid}"


class ImageGold(Gold):
    def __init__(self, gold_file=None, displayImages=False):
        super().__init__(gold_file)
        self.displayImages = displayImages

    def addTest(self, test):
        pass

    def valid(self):
        return True

    def __contains__(self, key):
        # All images are valid for this test.
        return True

    def _parse_gold(self, gold_file):
        pass

    def confrontation(self, networkName, stats=None):
        # TODO: make the list of test in the init parameteres
        psnr = [x.psnr for x in self.results]
        psnr = sum(psnr) / len(psnr)
        ssmid = [x.ssmid for x in self.results]
        ssmid = sum(ssmid) / len(ssmid)
        with utils.Tee(utils.getVaiswLogFile(), mode="a") as f:
            print(f"[AMD] [{networkName} TEST psnr] {psnr:.1f}% passed.", file=f)
            print(f"[AMD] [{networkName} TEST ssmid] {ssmid * 100:.1f}% passed.", file=f)
            print(f"[AMD] [{networkName} ALL TESTS] {psnr:.1f}% passed.", file=f)
            if stats is not None:
                stats.save_confrontation([psnr / 100, ssmid])

    def sendPredictions(self, batch, predict):
        """Allow to send predictions at each batch to the gold Manager.
        The predict must be a list of list of predictions
        """
        if len(predict) > 0:
            assert isinstance(predict[0], Image.Image), "Image comparison should receive Images"
            pred = self.getImageComparison(batch, predict)
            self.results.extend(pred)

    def getImageComparison(self, batch, predict):
        from skimage.metrics import peak_signal_noise_ratio as psnr
        from skimage.metrics import structural_similarity as ssim

        ret = []
        for image, output in zip(batch, predict):
            if self.displayImages:
                showImage(image)
                showImage(output)
            img = (
                image
                .astype(np.uint8)
                .resize(output.shape.width, output.shape.height, Image.ResampleMethod.Bicubic)
                .tdata(np.uint8)
            )
            out = output.convert(image.color).tdata(np.uint8)
            data_range = np.max(img) - np.min(img)
            ret.append(
                ImageComparison(
                    image.file,
                    psnr(img, out),
                    ssim(img, out, multichannel=True, channel_axis=2, data_range=data_range),
                )
            )
        return ret
