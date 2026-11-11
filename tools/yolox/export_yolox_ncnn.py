"""Convert an official YOLOX checkpoint to ncnn (.param/.bin) for yolox_motion_plugin.

The model is exported with decode_in_inference off: the network's output is
[8400, 85] per image (640x640 input), rows ordered stride 8, 16, 32, each
row = raw box regression (dx, dy, log w, log h), objectness (sigmoid) and the
80 COCO class scores (sigmoid). The plugin decodes boxes against the grid.

Input is BGR, 0..255, no normalisation (YOLOX's preprocessing since 0.2.0).

Usage (in a venv with torch, pnnx, ncnn, loguru, tabulate, opencv):
    python export_yolox_ncnn.py --yolox-src C:/dev/yolox_work/YOLOX \
        --exp yolox_s --ckpt yolox_s.pth --out-dir out [--size 640] [--check image.jpg]

YOLOX: https://github.com/Megvii-BaseDetection/YOLOX (Apache-2.0)
"""

import argparse
import os
import shutil
import sys

import numpy as np
import torch


def build_model(yolox_src, exp_name, ckpt, size):
    sys.path.insert(0, yolox_src)
    from yolox.exp import get_exp
    from yolox.models.network_blocks import SiLU
    from yolox.utils import replace_module

    exp = get_exp(None, exp_name)
    exp.test_size = (size, size)
    model = exp.get_model()
    # weights_only: a checkpoint is a pickle; never let one run code.
    state = torch.load(ckpt, map_location="cpu", weights_only=True)
    model.load_state_dict(state["model"] if "model" in state else state)
    model = replace_module(model, torch.nn.SiLU, SiLU)  # as YOLOX's own export does
    model.eval()
    model.head.decode_in_inference = False
    return model, exp.num_classes


def make_focus_ncnn_friendly(model):
    """Swap the Focus stem's strided slices (which pnnx can't turn into ncnn
    layers) for pixel_unshuffle (ncnn Reorg), exactly equivalent.

    Focus: channel k*3 + c, k over (top-left, bottom-left, top-right,
    bottom-right), i.e. k = 2*dx + dy for pixel offset (dy, dx).
    pixel_unshuffle(x, 2): channel c*4 + 2*dy + dx.
    So the first conv's input channels are reordered to match.
    """
    import types
    import torch.nn.functional as F

    focus = model.backbone.backbone.stem
    conv = focus.conv.conv
    w = conv.weight.data
    new_w = torch.empty_like(w)
    for c in range(3):
        for dy in range(2):
            for dx in range(2):
                new_w[:, c * 4 + 2 * dy + dx] = w[:, (2 * dx + dy) * 3 + c]
    conv.weight.data = new_w

    def forward(self, x):
        return self.conv(F.pixel_unshuffle(x, 2))
    focus.forward = types.MethodType(forward, focus)


def letterbox_bgr(img, size):
    """Revere's letterbox: centred, black padding (see r_motion_engine.cpp)."""
    import cv2
    h, w = img.shape[:2]
    s = min(size / w, size / h)
    sw, sh = int(w * s), int(h * s)
    out = np.zeros((size, size, 3), np.uint8)
    px, py = (size - sw) // 2, (size - sh) // 2
    out[py:py + sh, px:px + sw] = cv2.resize(img, (sw, sh))
    return out


def decode(raw, size, conf=0.3):
    """Reference decode, mirrors the plugin's C++."""
    rows = []
    i = 0
    for stride in (8, 16, 32):
        g = size // stride
        for gy in range(g):
            for gx in range(g):
                r = raw[i]
                i += 1
                cls = int(np.argmax(r[5:]))
                score = r[4] * r[5 + cls]
                if score < conf:
                    continue
                cx = (r[0] + gx) * stride
                cy = (r[1] + gy) * stride
                w = np.exp(r[2]) * stride
                h = np.exp(r[3]) * stride
                rows.append((cls, float(score), cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2))
    return sorted(rows, key=lambda t: -t[1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--yolox-src", required=True)
    ap.add_argument("--exp", default="yolox_s")
    ap.add_argument("--ckpt", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--size", type=int, default=640)
    ap.add_argument("--check", help="image to compare torch vs ncnn on")
    args = ap.parse_args()

    model, num_classes = build_model(args.yolox_src, args.exp, args.ckpt, args.size)

    # Focus -> pixel_unshuffle must not change the network's output.
    x = torch.rand(1, 3, args.size, args.size) * 255
    with torch.no_grad():
        before = model(x)
        make_focus_ncnn_friendly(model)
        after = model(x)
    diff = float((before - after).abs().max())
    print(f"focus rewrite: max abs diff {diff:.2e}")
    if diff > 1e-3:
        sys.exit("focus rewrite changed the output; not exporting")

    os.makedirs(args.out_dir, exist_ok=True)
    work = os.path.join(args.out_dir, "pnnx_work")
    os.makedirs(work, exist_ok=True)

    import pnnx
    pt_path = os.path.join(work, f"{args.exp}.pt")
    pnnx.export(model, pt_path, (x,))

    for ext in ("param", "bin"):
        shutil.copy(os.path.join(work, f"{args.exp}.ncnn.{ext}"),
                    os.path.join(args.out_dir, f"{args.exp}.{ext}"))
    print(f"wrote {args.out_dir}/{args.exp}.param/.bin ({num_classes} classes)")

    # Layers ncnn doesn't have are written as-is (e.g. "Tensor.slice") and
    # fail to load in revere; refuse to call that a success.
    with open(os.path.join(args.out_dir, f"{args.exp}.param")) as f:
        bad = sorted({ln.split()[0] for ln in f.read().splitlines()[2:] if "." in ln.split()[0]})
    if bad:
        sys.exit(f"ncnn param still has unconvertible layers: {bad}")

    if args.check:
        import cv2
        import ncnn
        src = cv2.imread(args.check)
        if src is None:
            sys.exit(f"can't read {args.check}")
        img = letterbox_bgr(src, args.size)
        t = torch.from_numpy(img.transpose(2, 0, 1).astype(np.float32))[None]
        with torch.no_grad():
            ref = model(t)[0].numpy()

        net = ncnn.Net()
        net.load_param(os.path.join(args.out_dir, f"{args.exp}.param"))
        net.load_model(os.path.join(args.out_dir, f"{args.exp}.bin"))
        ex = net.create_extractor()
        ex.input("in0", ncnn.Mat(img.transpose(2, 0, 1).astype(np.float32).copy()))
        _, out = ex.extract("out0")
        got = np.array(out).reshape(ref.shape)

        print("output shape", ref.shape, "max abs diff", float(np.abs(ref - got).max()))
        for name, raw in (("torch", ref), ("ncnn", got)):
            print(name, [(c, round(s, 3), [round(v) for v in b]) for c, s, *b in decode(raw, args.size)[:8]])


if __name__ == "__main__":
    main()
