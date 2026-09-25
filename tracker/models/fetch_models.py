"""Downloads the head-pose models the Agogica Tracker compares (dev builds only; the files
are not committed - see MODELS.md for sources, licences and training-data terms).

    python3 tracker/models/fetch_models.py

PINTO's archives are streamed and abandoned as soon as the needed file has been read, so
only a fraction of each (up to ~250 MB) is downloaded.
"""

import pathlib
import tarfile
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
PINTO = "https://s3.ap-northeast-2.wasabisys.com/pinto-model-zoo"

# name in tracker/models -> (url, member inside a .tar.gz or None for a plain file)
MODELS = {
    "yolox_n_body_head_hand_192x320.onnx": (
        f"{PINTO}/426_YOLOX-Body-Head-Hand/resources_n.tar.gz",
        "yolox_n_body_head_hand_post_0461_0.4428_1x3x192x320.onnx",
    ),
    "whenet_1x3x224x224_prepost.onnx": (
        "https://github.com/PINTO0309/HeadPoseEstimation-WHENet-yolov4-onnx-openvino/releases/download/v1.0.4/whenet_1x3x224x224_prepost.onnx",
        None,
    ),
    "sixdrepnet360_1x3x224x224_full.onnx": (
        f"{PINTO}/423_6DRepNet360/resources_full.tar.gz",
        "sixdrepnet360_1x3x224x224_full.onnx",
    ),
    "deimv2_atto_wholebody34_320x320.onnx": (
        f"{PINTO}/472_DEIMv2-Wholebody34/resources.tar.gz",
        "deimv2_hgnetv2_atto_wholebody34_340query_n_batch_320x320.onnx",
    ),
    "yolov8n-pose.onnx": ("https://huggingface.co/Xenova/yolov8-pose-onnx/resolve/main/yolov8n-pose.onnx", None),
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
