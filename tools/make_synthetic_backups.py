#!/usr/bin/env python3
"""生成「集中且反复变更」的合成备份序列（用于验证论文 2.3 的 P1/P2 前提）。

做法：
  backup_00 = 基线原始字节切片；
  在基线上固定若干 ROI（region of interest，等距分布）；
  backup_i 只修改每个 ROI 的**内部**一小段，保留 ROI 两侧的“稳定两翼”不变。
这样相邻备份间绝大部分字节相同（P1），变更集中在少数固定区域（P2），
且每个 ROI 的两翼跨多个备份重复出现——正是 breaking-apart 期望的数据形态。

固定随机种子，保证可复现。输出默认写到 Dataset/DataSet_4/。
"""

import argparse
import os
import random


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate recurring localized-change synthetic backups.")
    parser.add_argument("--base", default="Dataset/DataSet_3/emacs-22.1.tar",
                        help="用作基线的原始文件。")
    parser.add_argument("--out", default="Dataset/DataSet_4",
                        help="输出目录。")
    parser.add_argument("--size", type=int, default=32 * 1024 * 1024,
                        help="基线保留的字节数。")
    parser.add_argument("--backups", type=int, default=8,
                        help="生成的备份个数（含 backup_00）。")
    parser.add_argument("--rois", type=int, default=64,
                        help="固定 ROI 个数。")
    parser.add_argument("--roi-size", type=int, default=8 * 1024,
                        help="单个 ROI 的字节数。")
    parser.add_argument("--edit-inner", type=int, default=4 * 1024,
                        help="每次在 ROI 内部替换的字节数（其余两翼保持不变）。")
    parser.add_argument("--seed", type=int, default=20260917,
                        help="随机种子。")
    args = parser.parse_args()

    if args.edit_inner > args.roi_size:
        raise SystemExit("--edit-inner 不能大于 --roi-size")

    with open(args.base, "rb") as handle:
        base = bytes(handle.read(args.size))

    # ROI 等距分布，保证有足够空间放置。
    margin = args.roi_size
    span = len(base) - 2 * margin
    roi_starts = [margin + (span * i) // max(args.rois, 1) for i in range(args.rois)]
    # 每个 ROI 的替换窗口固定居中，两翼各保留 (roi_size - edit_inner) / 2 字节的稳定内容。
    half_slack = (args.roi_size - args.edit_inner) // 2
    inner_starts = [start + half_slack for start in roi_starts]

    os.makedirs(args.out, exist_ok=True)
    rng = random.Random(args.seed)

    prev = base
    for index in range(args.backups):
        if index == 0:
            data = base
        else:
            buf = bytearray(prev)
            for inner_start in inner_starts:
                fresh = bytes(rng.randrange(256) for _ in range(args.edit_inner))
                buf[inner_start:inner_start + args.edit_inner] = fresh  # 只替换，不改长度。
            data = bytes(buf)

        path = os.path.join(args.out, f"backup_{index:02d}.bin")
        with open(path, "wb") as handle:
            handle.write(data)
        print(f"{path}: {len(data)} bytes")
        prev = data


if __name__ == "__main__":
    main()
