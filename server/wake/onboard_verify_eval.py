"""The whole wake check on the board, honestly measured: the "Hola Bug" detector fires, the board's verifier (wake/train_verifier.py) looks at the audio
around the firing and must agree. No PC, no internet, no Whisper.

    python wake/onboard_verify_eval.py [--after 20261109_164550] [--levels 0.7,0.8,0.9] [--thresholds 0.5,0.7,0.8]

Greetings: every clip recorded at or after --after (your two test sessions: the detector and the verifier never trained on them).
False alarms: the not-greeting clips recorded at or after --after too (audio the board heard after the training data was frozen), counted per hour of that audio.
"""
import argparse
import sys
from pathlib import Path

import numpy as np
import torch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import baseline  # noqa: E402
import mine_windows as mw  # noqa: E402
import train as T  # noqa: E402
import train_verifier as V  # noqa: E402

SR = 16000


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--after", default="20261109_164550")
    parser.add_argument("--levels", default="0.7,0.8,0.9")
    parser.add_argument("--thresholds", default="0.5,0.7,0.8")
    parser.add_argument("--model", default=str(HERE / "verify_model.pt"))
    args = parser.parse_args()
    model = V.Net().to(T.device)
    model.load_state_dict(torch.load(args.model, map_location=T.device))
    model.eval()
    thresholds = [float(t) for t in args.thresholds.split(",")]

    def probability(windows):
        if not windows:
            return np.zeros(0)
        x = torch.tensor(np.array(windows, dtype=np.float32) / 32768.0).to(T.device)
        with torch.no_grad():
            return torch.sigmoid(model(T.features(x))).cpu().numpy()

    pos = baseline.read_clips(baseline.DATA / "pos", args.after) + baseline.read_clips(baseline.DATA.parent / "commands" / "wake", args.after)
    neg = baseline.read_clips(baseline.DATA / "neg", args.after)
    hours = sum(len(x) for _, x in neg) / SR / 3600
    print(f"{len(pos)} greetings, {len(neg)} other clips ({hours * 60:.1f} minutes) none of them seen in training")
    header = "  ".join(f"verifier>={t:<4}" for t in thresholds)
    print(f"level  detector alone (caught / FA per hour)   {header}   <- caught of greetings, false alarms per hour")
    rng = np.random.default_rng(3)
    for level in map(float, args.levels.split(",")):
        exe = baseline.build_detector(level)
        pos_found = mw.stream_windows(exe, pos, rng)
        neg_found = mw.stream_windows(exe, neg, rng)
        exe.unlink(missing_ok=True)
        pos_by_clip = {}
        for index, window in pos_found:
            pos_by_clip.setdefault(index, []).append(window)
        pp = {i: probability(ws) for i, ws in pos_by_clip.items()}
        np_ = probability([w for _, w in neg_found])
        cells = []
        for t in thresholds:
            caught = sum(1 for p in pp.values() if (p >= t).any())
            cells.append(f"{caught:>3}/{len(pos)} ({100 * caught / len(pos):3.0f}%) {float((np_ >= t).sum()) / hours:5.1f}/h")
        print(f"{level:<6} {len(pp):>3}/{len(pos)} ({100 * len(pp) / len(pos):3.0f}%)  {len(neg_found) / hours:6.1f}/h        " + "   ".join(cells), flush=True)


if __name__ == "__main__":
    main()
