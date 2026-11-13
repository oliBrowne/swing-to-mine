"""The gesture model: a small 1D CNN on one second of motion.

Input  [batch, 6, 100]   ax ay az gx gy gz, scaled as in dataset.py
Output [batch, classes]  scores (apply softmax to get confidences)

Only uses layers the board's ExecuTorch build already has: convolution,
batch norm, ReLU, average over time, one linear layer. No pooling layers
(the stride-2 convolutions do the shrinking instead).
"""

import torch
from torch import nn

WIDTHS = {
    "tiny": (8, 16, 16),
    "small": (16, 24, 32),
    "medium": (24, 48, 64),
}


class GestureNet(nn.Module):
    def __init__(self, n_classes: int, width: str = "small"):
        super().__init__()
        c1, c2, c3 = WIDTHS[width]

        def block(cin, cout, kernel, stride):
            return [nn.Conv1d(cin, cout, kernel, stride=stride, padding=kernel // 2, bias=False),
                    nn.BatchNorm1d(cout), nn.ReLU()]

        self.features = nn.Sequential(
            *block(6, c1, 7, 2),      # 100 -> 50 samples
            *block(c1, c2, 5, 2),     # 50 -> 25
            *block(c2, c3, 5, 2),     # 25 -> 13
        )
        self.head = nn.Linear(c3, n_classes)

    def forward(self, x):
        x = self.features(x)
        x = x.mean(dim=2)             # average over time
        return self.head(x)


def count_parameters(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters())


if __name__ == "__main__":
    for name in WIDTHS:
        m = GestureNet(6, name)
        out = m(torch.zeros(1, 6, 100))
        n = count_parameters(m)
        print(f"{name:7s} {n:6d} parameters (~{n / 1024:.0f} KB as int8, "
              f"~{4 * n / 1024:.0f} KB as float)  output {tuple(out.shape)}")
