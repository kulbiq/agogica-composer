# Tracker models

Downloaded by `fetch_models.py` for development builds; **not committed**.
`models.json` lists them with their download and pinned SHA-256; the build checks every file against it (`check_models.cmake`).
Licence status is for evaluation only - **check before shipping any of these**.

| File | Source | Code / weights licence | Training data | Colour order |
|---|---|---|---|---|
| `yolox_n_body_head_hand_192x320.onnx` | PINTO_model_zoo 426_YOLOX-Body-Head-Hand | Apache-2.0 | COCO images, annotations re-made by PINTO | BGR, 0-255 |
| `hfhpe_vitt_ypr_kappa_1x3x128x128.onnx` | PINTO0309/High-Angle_Robust_Fast_Head_Pose_Estimation, release `weights`, `vitt_distill_128_ypr_unified_v19_r180_lr2e4.tar.gz` | code MIT; **weights: no licence stated** - distilled from DINOv3 (DINOv3 License), the README asks to check derived-work terms before distributing; ask the author | synthetic (gpt-image-2) + the author's self-portraits, PINTO-CC-BY-4.0; no non-commercial data | RGB, x / 127.5 - 1 |
