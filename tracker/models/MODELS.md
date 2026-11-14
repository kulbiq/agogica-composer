# Tracker models

Downloaded by `fetch_models.py` for development builds; **not committed**.
The build checks both files against the SHA-256 pinned in `check_models.cmake`.
Licence status is for evaluation only - **check before shipping any of these**.

| File | Source | Code / weights licence | Training data | Colour order |
|---|---|---|---|---|
| `yolox_n_body_head_hand_192x320.onnx` | PINTO_model_zoo 426_YOLOX-Body-Head-Hand | Apache-2.0 | COCO images, annotations re-made by PINTO | BGR, 0-255 |
| `sixdrepnet360_1x3x224x224_full.onnx` | PINTO_model_zoo 423_6DRepNet360 (from thohemp/6DRepNet360) | MIT | 300W-LP + CMU Panoptic - **non-commercial / research only** | RGB, ImageNet mean/std |

