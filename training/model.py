"""The gesture model: a small 1D CNN on one second of motion.

Input  [batch, 6, 100]   ax ay az gx gy gz, scaled as in dataset.py
Output [batch, classes]  scores (apply softmax to get confidences)

Only uses layers the board's int8 ExecuTorch/CMSIS-NN path runs: convolution,
batch norm (folded at export), ReLU, one pooling over time, one linear layer.
The stride-2 convolutions do the shrinking.

Pooling over the last 13 time steps (`pool`):
  "max"     largest value of each channel (default since Nov 13: 96.3% vs 95.5%
            for "avg" on unseen people, see docs/results.md). A gesture is a
            short burst in the window; averaging dilutes it with the quiet parts.
  "avg"     the average (the first model; checkpoints without a "pool" entry)
  "avgmax"  both, side by side (96.6%, within noise of "max"; not exported for
            the board yet: it needs a concat op that hasn't been checked there)
"""

from __future__ import annotations

import torch
from torch import nn

WIDTHS = {
    "tiny": (8, 16, 16),
    "small": (16, 24, 32),
    "medium": (24, 48, 64),
}
POOLS = ("max", "avg", "avgmax")


class GestureNet(nn.Module):
    def __init__(self, n_classes: int, width: str = "small", pool: str = "max"):
        super().__init__()
        if pool not in POOLS:
            raise ValueError(f"pool must be one of {POOLS}")
        self.pool = pool
        c1, c2, c3 = WIDTHS[width]

        def block(cin, cout, kernel, stride):
            return [nn.Conv1d(cin, cout, kernel, stride=stride, padding=kernel // 2, bias=False),
                    nn.BatchNorm1d(cout), nn.ReLU()]

        self.features = nn.Sequential(
            *block(6, c1, 7, 2),      # 100 -> 50 samples
            *block(c1, c2, 5, 2),     # 50 -> 25
            *block(c2, c3, 5, 2),     # 25 -> 13
        )
        self.head = nn.Linear(c3 * (2 if pool == "avgmax" else 1), n_classes)

    def forward(self, x):
        x = self.features(x)          # [batch, c3, 13]
        if self.pool == "max":
            x = x.amax(dim=2)
        elif self.pool == "avg":
            x = x.mean(dim=2)
        else:
            x = torch.cat([x.mean(dim=2), x.amax(dim=2)], dim=1)
        return self.head(x)


def load_checkpoint(path) -> tuple[GestureNet, dict]:
    """Rebuild the model saved by train.py. Checkpoints from before the pool
    option (Nov 13 morning) have no "pool" entry and used the average."""
    ck = torch.load(path, map_location="cpu", weights_only=False)
    model = GestureNet(len(ck["classes"]), ck["width"], ck.get("pool", "avg"))
    model.load_state_dict(ck["state_dict"])
    return model.eval(), ck


def count_parameters(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters())


if __name__ == "__main__":
    for name in WIDTHS:
        m = GestureNet(6, name)
        out = m(torch.zeros(1, 6, 100))
        n = count_parameters(m)
        print(f"{name:7s} {n:6d} parameters (~{n / 1024:.0f} KB as int8, "
              f"~{4 * n / 1024:.0f} KB as float)  output {tuple(out.shape)}")
