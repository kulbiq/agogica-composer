# Tracker models

Downloaded by `fetch_models.py` for development builds; **not committed**.
Licence status is for evaluation only - **check before shipping any of these**.

| File | Source | Code / weights licence | Training data | Colour order |
|---|---|---|---|---|
| `yolox_n_body_head_hand_192x320.onnx` | PINTO_model_zoo 426_YOLOX-Body-Head-Hand | Apache-2.0 | COCO images, annotations re-made by PINTO | BGR, 0-255 |
| `whenet_1x3x224x224_prepost.onnx` | PINTO0309/HeadPoseEstimation-WHENet-yolov4-onnx-openvino v1.0.4 (from Ascend-Research/HeadPoseEstimation-WHENet) | BSD-3-Clause | 300W-LP + CMU Panoptic - **non-commercial / research only** | RGB, 0-255 (normalises inside) |
| `sixdrepnet360_1x3x224x224_full.onnx` | PINTO_model_zoo 423_6DRepNet360 (from thohemp/6DRepNet360) | MIT | 300W-LP + CMU Panoptic - **non-commercial / research only** | RGB, ImageNet mean/std |
| `deimv2_atto_wholebody34_320x320.onnx` | PINTO_model_zoo 472_DEIMv2-Wholebody34 | Apache-2.0 | COCO images, annotations re-made by PINTO | BGR |
| `yolov8n-pose.onnx` | Hugging Face Xenova/yolov8-pose-onnx (Ultralytics YOLOv8n-pose) | **AGPL-3.0** | COCO keypoints (CC BY 4.0) | RGB, /255 |

MediaPipe Face/Pose Landmarker (the web engine) are downloaded by `tracker/ui/scripts/copy-assets.mjs`: Apache-2.0 (confirm the model cards before release).
