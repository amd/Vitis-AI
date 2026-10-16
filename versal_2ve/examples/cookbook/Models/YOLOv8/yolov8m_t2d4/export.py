#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import collections
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

WEIGHTS = "yolov8m.pt"
IMGSZ = 640
OPSET = 22
OUT = os.path.join(HERE, "yolov8m_fp32.onnx")


_BN_FOLD_CALLS = []


def _patch_bn_fusion():
    import torch
    import torch.nn.utils.fusion as fusion

    if not hasattr(fusion, "fuse_conv_bn_weights"):
        sys.exit("[export] torch.nn.utils.fusion has no fuse_conv_bn_weights -- "
                 "this torch version moved the BN fold; update _patch_bn_fusion.")
    from ultralytics.utils import torch_utils
    if not hasattr(torch_utils, "fuse_conv_bn_weights"):
        sys.exit("[export] ultralytics.utils.torch_utils no longer imports "
                 "fuse_conv_bn_weights -- patching torch alone would NOT reach it "
                 "and the export would silently use the wrong (rsqrt) fold.")

    original = fusion.fuse_conv_bn_weights
    del _BN_FOLD_CALLS[:]

    def divide_form(conv_w, conv_b, bn_rm, bn_rv, bn_eps, bn_w, bn_b, transpose=False):
        _BN_FOLD_CALLS.append(1)
        if transpose:
            return original(conv_w, conv_b, bn_rm, bn_rv, bn_eps, bn_w, bn_b, transpose)
        conv_weight_dtype = conv_w.dtype
        conv_bias_dtype = conv_b.dtype if conv_b is not None else conv_weight_dtype
        if conv_b is None:
            conv_b = torch.zeros_like(bn_rm)
        if bn_w is None:
            bn_w = torch.ones_like(bn_rm)
        if bn_b is None:
            bn_b = torch.zeros_like(bn_rm)

        w_bn = bn_w.div(torch.sqrt(bn_eps + bn_rv))
        b_bn = bn_b - bn_w.mul(bn_rm).div(torch.sqrt(bn_rv + bn_eps))
        fused_w = (conv_w * w_bn.reshape(-1, 1, 1, 1)).to(dtype=conv_weight_dtype)
        fused_b = (conv_b * w_bn + b_bn).to(dtype=conv_bias_dtype)
        return (
            torch.nn.Parameter(fused_w, conv_w.requires_grad),
            torch.nn.Parameter(fused_b, conv_b.requires_grad),
        )

    fusion.fuse_conv_bn_weights = divide_form
    torch_utils.fuse_conv_bn_weights = divide_form
    return original


def _unpatch_bn_fusion(original):
    import torch.nn.utils.fusion as fusion
    from ultralytics.utils import torch_utils
    fusion.fuse_conv_bn_weights = original
    torch_utils.fuse_conv_bn_weights = original


def _patch_torch2onnx(dynamo):
    import torch
    from ultralytics.utils.export import engine

    if not hasattr(engine, "torch2onnx"):
        sys.exit("[export] ultralytics.utils.export.engine has no torch2onnx -- "
                 "this version restructured its exporter; update the patch.")
    original = engine.torch2onnx

    def patched(model, im, output_file, opset=14, input_names=None,
                output_names=None, dynamic=None):
        torch.onnx.export(
            model, im, output_file,
            opset_version=opset,
            input_names=input_names or ["images"],
            output_names=output_names or ["output0"],
            dynamo=dynamo,
        )
        return str(output_file)

    engine.torch2onnx = patched
    return original


def _unpatch_torch2onnx(original):
    from ultralytics.utils.export import engine
    engine.torch2onnx = original


def run_export(dynamo, dest):
    from ultralytics import YOLO

    original_export = _patch_torch2onnx(dynamo)
    original_bn = _patch_bn_fusion()
    try:
        produced = str(YOLO(WEIGHTS).export(
            format="onnx",
            opset=OPSET,
            imgsz=IMGSZ,
            simplify=True,
            dynamic=False,
            half=False,
            nms=False,
            batch=1,
            device="cpu",
        ))
    finally:
        _unpatch_torch2onnx(original_export)
        _unpatch_bn_fusion(original_bn)

    if not _BN_FOLD_CALLS:
        sys.exit("[export] the divide-form BN fold never ran -- ultralytics "
                 "changed how it folds BN. Refusing to ship an unverified fold.")

    dest = os.path.abspath(dest)
    if os.path.abspath(produced) != dest:
        shutil.move(produced, dest)
    return dest


def _index(graph):
    producer, consumers = {}, collections.defaultdict(list)
    for idx, n in enumerate(graph.node):
        for o in n.output:
            if o:
                producer[o] = idx
        for i in n.input:
            if i:
                consumers[i].append(idx)
    return producer, consumers


def convs_by_weight(graph):
    init = {i.name for i in graph.initializer}
    out = {}
    for idx, n in enumerate(graph.node):
        if n.op_type == "Conv" and len(n.input) > 1 and n.input[1] in init:
            out.setdefault(n.input[1], []).append(idx)
    return {k: v[0] for k, v in out.items() if len(v) == 1}


def build_correspondence(dg, tg):
    d_prod, d_cons = _index(dg)
    t_prod, t_cons = _index(tg)
    d_init = {i.name for i in dg.initializer}
    t_init = {i.name for i in tg.initializer}

    d_conv, t_conv = convs_by_weight(dg), convs_by_weight(tg)
    shared = sorted(set(d_conv) & set(t_conv))
    if not shared:
        sys.exit("[export] no Conv anchors -- the two exports share no parameter "
                 "names, so they are not the same model.")

    pair, taken, queue = {}, set(), []

    def anchor(di, ti):
        if di in pair or ti in taken:
            return
        pair[di] = ti
        taken.add(ti)
        queue.append((di, ti))

    for k in shared:
        anchor(d_conv[k], t_conv[k])

    def unique_by_type(g, idxs):
        by = collections.defaultdict(list)
        for i in idxs:
            by[g.node[i].op_type].append(i)
        return {k: v[0] for k, v in by.items() if len(v) == 1}

    while queue:
        di, ti = queue.pop()
        dn, tn = dg.node[di], tg.node[ti]

        for i in range(min(len(dn.output), len(tn.output))):
            do, to = dn.output[i], tn.output[i]
            if not do or not to:
                continue
            ds = unique_by_type(dg, d_cons.get(do, []))
            ts = unique_by_type(tg, t_cons.get(to, []))
            for op in set(ds) & set(ts):
                anchor(ds[op], ts[op])

        for i in range(min(len(dn.input), len(tn.input))):
            di_t, ti_t = dn.input[i], tn.input[i]
            if not di_t or not ti_t or di_t in d_init or ti_t in t_init:
                continue
            dp, tp = d_prod.get(di_t), t_prod.get(ti_t)
            if dp is not None and tp is not None \
                    and dg.node[dp].op_type == tg.node[tp].op_type:
                anchor(dp, tp)

    return pair


def apply_names(dg, tg, pair):
    protected = ({i.name for i in dg.input}
                 | {o.name for o in dg.output}
                 | {i.name for i in dg.initializer})

    node_rename, tensor_rename, taken = {}, {}, set()
    for di, ti in sorted(pair.items()):
        n, t = dg.node[di], tg.node[ti]
        if t.name and t.name not in taken:
            node_rename[n.name] = t.name
            taken.add(t.name)
        for a, b in zip(n.output, t.output):
            if (a and b and a != b
                    and a not in protected and b not in protected
                    and b not in tensor_rename.values()):
                tensor_rename[a] = b

    for n in dg.node:
        if n.name in node_rename:
            n.name = node_rename[n.name]
        for i, v in enumerate(n.input):
            if v in tensor_rename:
                n.input[i] = tensor_rename[v]
        for i, v in enumerate(n.output):
            if v in tensor_rename:
                n.output[i] = tensor_rename[v]
    for vi in dg.value_info:
        if vi.name in tensor_rename:
            vi.name = tensor_rename[vi.name]


def verify_restore(dg, tg):
    d_conv, t_conv = convs_by_weight(dg), convs_by_weight(tg)
    shared = sorted(set(d_conv) & set(t_conv))

    bad = []
    for w in shared:
        dn, tn = dg.node[d_conv[w]], tg.node[t_conv[w]]
        if dn.name != tn.name:
            bad.append((w, dn.name, tn.name))
        elif dn.output and tn.output and dn.output[0] != tn.output[0]:
            bad.append((w, dn.output[0], tn.output[0]))
    return shared, bad


def restore_names(graph_path, donor_path, out_path):
    import onnx
    dm, tm = onnx.load(graph_path), onnx.load(donor_path)

    pair = build_correspondence(dm.graph, tm.graph)
    apply_names(dm.graph, tm.graph, pair)
    shared, bad = verify_restore(dm.graph, tm.graph)

    onnx.checker.check_model(dm)
    onnx.save(dm, out_path)
    if bad:
        for w, got, want in bad[:6]:
            print("  %-34s got %-28s want %s" % (w, got, want), file=sys.stderr)
        sys.exit("[export] %d/%d Convs disagree with the donor, so downstream name "
                 "matching would silently miss. Model written for inspection."
                 % (len(bad), len(shared)))


def main():
    import onnx

    raw = OUT + ".graph.tmp.onnx"
    donor = OUT + ".donor.tmp.onnx"

    run_export(dynamo=True, dest=raw)
    run_export(dynamo=False, dest=donor)
    restore_names(raw, donor, OUT)

    for t in (raw, donor):
        if os.path.exists(t):
            os.remove(t)

    m = onnx.load(OUT, load_external_data=False)
    got = {o.domain or "ai.onnx": o.version for o in m.opset_import}.get("ai.onnx")
    if got != OPSET:
        sys.exit("[export] OPSET MISMATCH: asked for %s, model declares %s"
                 % (OPSET, got))

    print("[export] %s (%d nodes, opset %d)" % (OUT, len(m.graph.node), got))
    return 0


if __name__ == "__main__":
    sys.exit(main())
