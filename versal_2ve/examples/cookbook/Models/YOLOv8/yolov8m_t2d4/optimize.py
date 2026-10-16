#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import collections
import os
import re
import sys

import numpy as np
import onnx
from onnx import helper, numpy_helper as nh

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.join(HERE, "yolov8m_fp32.onnx")
OUT = os.path.join(HERE, "yolov8m_optimized_fp32.onnx")

NBINS, NCOORD = 16, 4
SEEDS, TOL = 3, 1e-3

BOX_CONV_RE = re.compile(
    r"^/(?P<head>model\.\d+)/cv2\.(?P<s>\d+)/cv2\.(?P=s)\.2/Conv$")


def get_attr(node, key, default=None):
    for a in node.attribute:
        if a.name == key:
            return helper.get_attribute_value(a)
    return default


def topo_sort(graph):
    ready = {i.name for i in graph.initializer} | {i.name for i in graph.input}
    pending, out = list(graph.node), []
    while pending:
        stuck = []
        for n in pending:
            if all((i in ready) or i == "" for i in n.input):
                out.append(n)
                ready.update(n.output)
            else:
                stuck.append(n)
        if len(stuck) == len(pending):
            raise RuntimeError("cycle/unresolved: %s" % [n.name for n in stuck][:3])
        pending = stuck
    return out


def prune(graph, keep_outputs):
    prod = {o: n for n in graph.node for o in n.output}
    need, stack = set(), list(keep_outputs)
    while stack:
        t = stack.pop()
        if t in need:
            continue
        need.add(t)
        n = prod.get(t)
        if n is not None:
            stack.extend(n.input)
    keep = [n for n in graph.node if any(o in need for o in n.output)]
    del graph.node[:]
    graph.node.extend(keep)
    used = {i for n in graph.node for i in n.input}
    ki = [i for i in graph.initializer if i.name in used]
    del graph.initializer[:]
    graph.initializer.extend(ki)


def find_cv1_sites(graph):
    prod = {o: n for n in graph.node for o in n.output}
    inits = {i.name for i in graph.initializer}
    consumers = collections.Counter(i for n in graph.node for i in n.input)

    sites = []
    for split in graph.node:
        if split.op_type != "Split" or len(split.output) != 2:
            continue
        if get_attr(split, "axis", 0) != 1:
            continue

        sizes = get_attr(split, "split")
        if sizes is None and len(split.input) > 1 and split.input[1] in inits:
            t = next(i for i in graph.initializer if i.name == split.input[1])
            sizes = nh.to_array(t).tolist()
        if sizes is not None and len(set(sizes)) != 1:
            continue

        mul = prod.get(split.input[0])
        if mul is None or mul.op_type != "Mul" or len(mul.input) != 2:
            continue
        sig = next((prod.get(i) for i in mul.input
                    if prod.get(i) is not None and prod[i].op_type == "Sigmoid"), None)
        if sig is None or sig.input[0] not in mul.input:
            continue
        conv = prod.get(sig.input[0])
        if conv is None or conv.op_type != "Conv" or len(conv.input) < 3:
            continue
        if conv.input[1] not in inits or conv.input[2] not in inits:
            continue
        if consumers[mul.output[0]] != 1 or consumers[sig.output[0]] != 1:
            continue
        if consumers[conv.output[0]] != 2:
            continue
        sites.append((split, mul, sig, conv))
    return sites


def split_one(graph, site, inits):
    split, mul, sig, conv = site
    nm = conv.name
    prefix = nm.rsplit("/cv1/", 1)[0] if "/cv1/" in nm else nm.rsplit("/", 1)[0]

    w = nh.to_array(inits[conv.input[1]])
    b = nh.to_array(inits[conv.input[2]])
    oc = w.shape[0]
    assert oc % 2 == 0, "cv1 output channels not even: %d" % oc
    half = oc // 2

    new_nodes, new_inits, outs = [], [], []
    for tag, sl in (("A", slice(0, half)), ("B", slice(half, oc))):
        p = "%s/cv1_%s" % (prefix, tag)
        wn, bn = "%s.weight" % p, "%s.bias" % p
        new_inits += [nh.from_array(np.ascontiguousarray(w[sl]), name=wn),
                      nh.from_array(np.ascontiguousarray(b[sl]), name=bn)]

        cout, sout, mout = "%s/conv_out" % p, "%s/sig_out" % p, "%s/act/Mul_out" % p
        c = helper.make_node("Conv", [conv.input[0], wn, bn], [cout], name="%s/conv" % p)
        c.attribute.extend(conv.attribute)
        new_nodes.append(c)
        new_nodes.append(helper.make_node("Sigmoid", [cout], [sout],
                                          name="%s/act/Sigmoid" % p))
        new_nodes.append(helper.make_node("Mul", [cout, sout], [mout],
                                          name="%s/act/Mul" % p))
        outs.append(mout)

    remap = {split.output[0]: outs[0], split.output[1]: outs[1]}
    for n in graph.node:
        for k, i in enumerate(n.input):
            if i in remap:
                n.input[k] = remap[i]
    for o in graph.output:
        if o.name in remap:
            o.name = remap[o.name]
    return new_nodes, new_inits, {split.name, mul.name, sig.name, conv.name}


def apply_cv1split(model):
    g = model.graph
    inits = {i.name: i for i in g.initializer}
    sites = find_cv1_sites(g)
    print("[cv1split] %d C2f cv1 split site(s)" % len(sites))
    if not sites:
        sys.exit("[cv1split] no sites found -- no C2f Split fed by a SiLU. Already "
                 "split, or exported differently?")

    all_new, all_init, dead = [], [], set()
    for site in sites:
        n, i, d = split_one(g, site, inits)
        all_new += n
        all_init += i
        dead |= d

    keep = [n for n in g.node if n.name not in dead]
    del g.node[:]
    g.node.extend(keep + all_new)
    g.initializer.extend(all_init)


def dfl_conv_weight():
    w = np.zeros((NCOORD, NCOORD * NBINS), dtype=np.float32)
    for k in range(NCOORD):
        w[k, k * NBINS:(k + 1) * NBINS] = np.arange(NBINS, dtype=np.float32)
    return w.reshape(NCOORD, NCOORD * NBINS, 1, 1)


def locate_head(graph):
    found, head = {}, None
    for n in graph.node:
        m = BOX_CONV_RE.match(n.name or "")
        if m:
            head = m.group("head")
            found[int(m.group("s"))] = n
    if not found:
        sys.exit("[tok4] could not locate the detect head: no node matches\n"
                 "       %s\n       Was this model produced by export.py?"
                 % BOX_CONV_RE.pattern)
    scales = sorted(found)
    if scales != list(range(len(scales))):
        sys.exit("[tok4] box conv scales are not contiguous: %s" % scales)
    box_out = [found[s].output[0] for s in scales]

    dfl = [n for n in graph.node if (n.name or "").startswith("/%s/dfl/" % head)]
    if not dfl:
        sys.exit("[tok4] no /%s/dfl/ nodes -- is the DFL decode present?" % head)
    consumers = collections.defaultdict(list)
    for n in graph.node:
        for i in n.input:
            consumers[i].append(n)
    dfl_names = {n.name for n in dfl}
    exits = [n for n in dfl
             if any(c.name not in dfl_names
                    for o in n.output for c in consumers.get(o, []))]
    if len(exits) != 1:
        sys.exit("[tok4] expected exactly one node leaving the DFL chain, found "
                 "%d: %s" % (len(exits), [n.name for n in exits]))
    return head, box_out, exits[0]


def find_grids(model, tensors):
    inferred = onnx.shape_inference.infer_shapes(model)
    vi = {v.name: v for v in list(inferred.graph.value_info)
          + list(inferred.graph.output) + list(inferred.graph.input)}
    grids = []
    for t in tensors:
        if t not in vi:
            sys.exit("[tok4] shape inference produced no shape for %s" % t)
        d = vi[t].type.tensor_type.shape.dim
        h, w = d[2].dim_value, d[3].dim_value
        if not h or not w:
            sys.exit("[tok4] no static grid for %s (got %sx%s)" % (t, h, w))
        grids.append((h, w))
    return grids


def build_tok4(box_out, grids):
    nodes, inits = [], []
    ntok = sum(h * w for h, w in grids)

    def k(name, arr):
        inits.append(nh.from_array(arr, name))
        return name

    def n(op, ins, out, name, **kw):
        nodes.append(helper.make_node(op, ins, [out], name=name, **kw))
        return out

    rows = []
    for s, (h, w) in enumerate(grids):
        t = n("Transpose", [box_out[s]], "/tm%d/t" % s,
              "/tm%d/Transpose" % s, perm=[0, 2, 3, 1])
        rows.append(n("Reshape",
                      [t, k("/tm%d/sh" % s,
                            np.array([1, h * w * NCOORD, NBINS], np.int64))],
                      "/tm%d/r" % s, "/tm%d/Reshape" % s))
    cat = n("Concat", rows, "/tm/cat", "/tm/Concat", axis=1)
    p = n("Softmax", [cat], "/tm/p", "/tm/Softmax", axis=-1)
    g4 = n("Reshape", [p, k("/tm/s416",
                            np.array([1, ntok, NCOORD, NBINS], np.int64))],
           "/tm/p4", "/tm/Reshape416")
    tp = n("Transpose", [g4], "/tm/tc", "/tm/TransposeC", perm=[0, 2, 3, 1])
    c4 = n("Reshape", [tp, k("/tm/s64c",
                             np.array([1, NCOORD * NBINS, ntok, 1], np.int64))],
           "/tm/c4", "/tm/ReshapeC")
    d4 = n("Conv", [c4, k("/tm/wdfl", dfl_conv_weight())], "/tm/d4", "/tm/Conv",
           dilations=[1, 1], group=1, kernel_shape=[1, 1],
           pads=[0, 0, 0, 0], strides=[1, 1])
    n("Reshape", [d4, k("/tm/s3", np.array([1, NCOORD, ntok], np.int64))],
      "/tm/d3", "/tm/ReshapeOut")
    return nodes, inits


def apply_tok4(model):
    g = model.graph
    head, box_out, old = locate_head(g)
    grids = find_grids(model, box_out)
    print("[tok4] head /%s, grids %s (ntok=%d)"
          % (head, grids, sum(h * w for h, w in grids)))

    new_nodes, new_inits = build_tok4(box_out, grids)

    old_out = old.output[0]
    n_rewired = 0
    for n in g.node:
        for k, i in enumerate(n.input):
            if i == old_out:
                n.input[k] = "/tm/d3"
                n_rewired += 1
    if not n_rewired:
        sys.exit("[tok4] nothing consumed %s -- refusing to graft a dead tail"
                 % old_out)

    g.node.extend(new_nodes)
    g.initializer.extend(new_inits)



def verify(src, out):
    import onnxruntime as ort

    so = ort.SessionOptions()
    so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    a = ort.InferenceSession(src, so, providers=["CPUExecutionProvider"])
    b = ort.InferenceSession(out, so, providers=["CPUExecutionProvider"])
    ia = a.get_inputs()[0]
    worst, scale = 0.0, 0.0
    for s in range(SEEDS):
        x = np.random.RandomState(1234 + s).rand(*ia.shape).astype(np.float32)
        ra = a.run(None, {ia.name: x})[0]
        rb = b.run(None, {b.get_inputs()[0].name: x})[0]
        worst = max(worst, float(np.abs(ra.astype(np.float64)
                                        - rb.astype(np.float64)).max()))
        scale = max(scale, float(ra.std()))
    rel = worst / scale if scale else float("inf")
    if worst > TOL:
        sys.exit("[verify] max|delta| %g (output std %.1f -> relative %.1e) exceeds "
                 "%g -- too large to be reassociation; a rewrite is wrong. The bad "
                 "model is at %s." % (worst, scale, rel, TOL, out))
    print("[verify] max|delta| %g (relative %.1e) -- fp32 reassociation"
          % (worst, rel))


def main():
    if not os.path.exists(MODEL):
        sys.exit("[optimize] input not found: %s -- run export.py first." % MODEL)

    model = onnx.load(MODEL)
    apply_cv1split(model)
    apply_tok4(model)

    prune(model.graph, [o.name for o in model.graph.output])
    live = {o for n in model.graph.node for o in n.output}
    vi = [v for v in model.graph.value_info if v.name in live]
    del model.graph.value_info[:]
    model.graph.value_info.extend(vi)

    ordered = topo_sort(model.graph)
    del model.graph.node[:]
    model.graph.node.extend(ordered)

    onnx.checker.check_model(model)
    onnx.save(model, OUT)

    verify(MODEL, OUT)
    print("[optimize] %s (%d nodes)" % (OUT, len(model.graph.node)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
