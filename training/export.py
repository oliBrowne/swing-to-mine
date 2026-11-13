"""Export the trained gesture model to an ExecuTorch .pte for the board.

    python training/export.py                 # int8, CMSIS-NN ops (what the board runs: 3.4 ms)
    python training/export.py --float         # float32, portable ops only (64 ms at -O3, for comparison)
    python training/export.py --width tiny    # another trained width

Reads training/out/gesture_<width>.pt (from train.py) and writes
training/out/model_test/ (or model_test_float/):

    model.pte          the model, embedded by firmware/model_test
    test_windows.h     one real window per class + the scores the model should give
    ref_scores.npy     the same scores, for scripts/readback.py

Needs the ExecuTorch 1.3.0 Python env (the one from Lab 3, ~/executorch/et-env),
plus pandas for dataset.py.

The int8 version changes the layers' form, not their maths:
  * batch norm is folded into the convolutions;
  * each Conv1d becomes a Conv2d with a (1, k) kernel on a [1, 6, 1, 100]
    channels-last input, because the CMSIS-NN ops are 2D and channels-last;
  * the pooling over time becomes MaxPool2d((1, 13)) or AvgPool2d((1, 13))
    (13 = time steps left after the three stride-2 convs), which CMSIS-NN runs
    in int8. (ExecuTorch's float mean rejects the channels-last tensor the int8
    convs produce.) "avgmax" pooling is float-only for now.
Then post-training quantization (CortexMQuantizer) calibrated on real windows.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.nn.utils.fusion import fuse_conv_bn_eval

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from dataset import load_all  # noqa: E402
from model import GestureNet, load_checkpoint  # noqa: E402


def folded(net: GestureNet) -> list[nn.Conv1d]:
    """The three convs with their batch norms folded in."""
    f = net.features
    return [fuse_conv_bn_eval(f[i], f[i + 1]) for i in range(0, len(f), 3)]


class FoldedNet(nn.Module):
    """Float version: GestureNet with batch norm folded (same output)."""

    def __init__(self, net: GestureNet):
        super().__init__()
        self.convs = nn.ModuleList(folded(net))
        self.pool = net.pool
        self.head = net.head

    def forward(self, x):
        for conv in self.convs:
            x = torch.relu(conv(x))
        if self.pool == "max":
            x = x.amax(dim=2)
        elif self.pool == "avg":
            x = x.mean(dim=2)
        else:
            x = torch.cat([x.mean(dim=2), x.amax(dim=2)], dim=1)
        return self.head(x)


class GestureNet2d(nn.Module):
    """int8 version: Conv2d (1, k) + Max/AvgPool2d, input [B, 6, 1, 100] channels-last."""

    def __init__(self, net: GestureNet):
        super().__init__()
        if net.pool == "avgmax":
            raise SystemExit("avgmax pooling has no int8 export yet: use --float, or train with --pool max")
        layers = []
        for c in folded(net):
            c2 = nn.Conv2d(c.in_channels, c.out_channels, (1, c.kernel_size[0]),
                           stride=(1, c.stride[0]), padding=(0, c.padding[0]))
            c2.weight.data = c.weight.data.unsqueeze(2).clone()
            c2.bias.data = c.bias.data.clone()
            layers += [c2, nn.ReLU()]
        self.features = nn.Sequential(*layers)
        self.pool = nn.MaxPool2d((1, 13)) if net.pool == "max" else nn.AvgPool2d((1, 13))
        self.head = net.head

    def forward(self, x):
        return self.head(torch.flatten(self.pool(self.features(x)), 1))


def to2d(x: torch.Tensor) -> torch.Tensor:
    """[B, 6, 100] -> [B, 6, 1, 100] channels-last."""
    return x.unsqueeze(2).contiguous(memory_format=torch.channels_last)


def quantize(m2: GestureNet2d, X: torch.Tensor, y: np.ndarray, n_classes: int):
    """Post-training int8 quantization; returns (graph module, edge program manager)."""
    from executorch.backends.cortex_m.passes.cortex_m_pass_manager import CortexMPassManager
    from executorch.backends.cortex_m.quantizer.quantizer import CortexMQuantizer
    from executorch.exir import EdgeCompileConfig, to_edge
    from torchao.quantization.pt2e.quantize_pt2e import convert_pt2e, prepare_pt2e

    example = (to2d(X[:1]),)
    gm = prepare_pt2e(torch.export.export(m2, example).module(), CortexMQuantizer())
    rng = np.random.default_rng(0)
    calib = np.unique(np.concatenate([rng.choice(len(X), min(600, len(X)), replace=False)]
                                     + [np.flatnonzero(y == c)[:40] for c in range(n_classes)]))
    with torch.no_grad():
        for i in range(0, len(calib), 32):
            gm(to2d(X[calib[i:i + 32]]))
    gm = convert_pt2e(gm)
    edge = to_edge(torch.export.export(gm, example),
                   compile_config=EdgeCompileConfig(preserve_ops=[torch.ops.aten.linear.default],
                                                    _check_ir_validity=False))
    edge._edge_programs["forward"] = CortexMPassManager(edge.exported_program()).transform()
    return gm, edge


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--width", default="small")
    ap.add_argument("--float", action="store_true", help="float32 with portable ops instead of int8")
    args = ap.parse_args()

    import executorch.exir as exir
    from executorch.exir import ExecutorchBackendConfig

    net, ck = load_checkpoint(HERE / "out" / f"gesture_{args.width}.pt")
    classes = ck["classes"]
    print(f"model '{ck['width']}', {net.pool} pooling")

    X, y, *_ = load_all()
    Xt = torch.from_numpy(X)
    # test windows: the middle window of each class
    picks = [int(np.flatnonzero(y == c)[len(np.flatnonzero(y == c)) // 2]) for c in range(len(classes))]
    with torch.no_grad():
        float_scores = net(Xt).numpy()

    if args.float:
        model = FoldedNet(net).eval()
        edge = exir.to_edge(torch.export.export(model, (Xt[:1],)))
        with torch.no_grad():
            ref = model(Xt[picks]).numpy()
        windows = X[picks]                                  # [N, 6, 100]
        out_dir = HERE / "out" / "model_test_float"
    else:
        m2 = GestureNet2d(net).eval()
        gm, edge = quantize(m2, Xt, y, len(classes))
        with torch.no_grad():
            q_all = torch.cat([gm(to2d(Xt[i:i + 256])) for i in range(0, len(X), 256)]).numpy()
            em = edge.exported_program().module()           # python versions of the board's int8 ops
            ref = np.concatenate([em(to2d(Xt[i:i + 1])).numpy() for i in picks])
        print(f"int8 vs float on all {len(X)} windows: same top class on "
              f"{100 * (q_all.argmax(1) == float_scores.argmax(1)).mean():.2f}%")
        windows = X[picks].transpose(0, 2, 1)               # channels-last: [N, 100, 6]
        out_dir = HERE / "out" / "model_test"

    prog = edge.to_executorch(config=ExecutorchBackendConfig(extract_delegate_segments=False))
    plan = prog.executorch_program.execution_plan[0]
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "model.pte").write_bytes(prog.buffer)
    print("operators:", ", ".join(f"{o.name}.{o.overload}" for o in plan.operators))
    print(f"activation memory (non_const_buffer_sizes): {list(plan.non_const_buffer_sizes)}")
    print(f"wrote {out_dir / 'model.pte'} ({len(prog.buffer)} bytes)")

    for i, c in enumerate(classes):
        print(f"  {c:10s} expected scores", " ".join(f"{v:7.3f}" for v in ref[i]),
              "| float top class:", classes[int(float_scores[picks[i]].argmax())])
    with open(out_dir / "test_windows.h", "w") as f:
        f.write("// generated by training/export.py: one real window per class + expected scores\n")
        f.write(f"#pragma once\n#define N_CLASSES {len(classes)}\n#define N_TESTS {len(picks)}\n")
        if not args.float:
            f.write("#define INPUT_CHANNELS_LAST 1   // windows stored as [100][6]\n")
        f.write("static const float kTestWindows[N_TESTS][6 * 100] = {\n")
        for w in windows:
            f.write("  {" + ",".join(f"{v:.8e}f" for v in w.reshape(-1)) + "},\n")
        f.write("};\nstatic const float kRefScores[N_TESTS][N_CLASSES] = {\n")
        for r in ref:
            f.write("  {" + ",".join(f"{v:.8e}f" for v in r) + "},\n")
        f.write("};\n")
    np.save(out_dir / "ref_scores.npy", ref)
    np.save(out_dir / "float_scores.npy", float_scores[picks])


if __name__ == "__main__":
    main()
