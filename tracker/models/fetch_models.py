"""Downloads the Agogica Tracker's models (HFHPE + the YOLOX head detector) for development builds;
the files are not committed - see MODELS.md for sources, licences and training-data terms.

    python3 tracker/models/fetch_models.py

The archives are streamed and abandoned as soon as the needed file has been read, so only a
fraction of each is downloaded. The build checks every file against check_models.cmake.
"""

from __future__ import annotations  # `str | None` on the macOS system Python (3.9)

import pathlib
import tarfile
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
PINTO = "https://s3.ap-northeast-2.wasabisys.com/pinto-model-zoo"
HFHPE = "https://github.com/PINTO0309/High-Angle_Robust_Fast_Head_Pose_Estimation/releases/download/weights"

# name in tracker/models -> (url, member inside a .tar.gz or None for a plain file)
MODELS = {
    "yolox_n_body_head_hand_192x320.onnx": (
        f"{PINTO}/426_YOLOX-Body-Head-Hand/resources_n.tar.gz",
        "yolox_n_body_head_hand_post_0461_0.4428_1x3x192x320.onnx",
    ),
    "hfhpe_vitt_ypr_kappa_1x3x128x128.onnx": (
        f"{HFHPE}/vitt_distill_128_ypr_unified_v19_r180_lr2e4.tar.gz",
        "hfhpe_vitt_ypr_kappa_1x3x128x128.onnx",
    ),
}


def fetch(name: str, url: str, member: str | None):
    target = HERE / name
    if target.exists():
        return
    print(f"{name}: downloading…", flush=True)
    with urllib.request.urlopen(url) as response:
        if member is None:
            data = response.read()
        else:
            data = None
            with tarfile.open(fileobj=response, mode="r|gz") as tar:
                for entry in tar:
                    if entry.name.endswith(member):
                        data = tar.extractfile(entry).read()
                        break
            if data is None:
                raise SystemExit(f"{member} not found in {url}")
    if not data.startswith(b"\x08"):  # ONNX files are protobufs starting with the ir_version field
        raise SystemExit(f"{name}: doesn't look like an ONNX model")
    temp = target.with_suffix(".part")
    temp.write_bytes(data)
    temp.rename(target)
    print(f"{name}: {len(data) / 1e6:.1f} MB")


if __name__ == "__main__":
    for name, (url, member) in MODELS.items():
        fetch(name, url, member)
