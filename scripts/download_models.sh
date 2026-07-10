#!/usr/bin/env bash

set -e

mkdir -p models

wget \
    "https://huggingface.co/SpotLab/YOLOv8Detection/resolve/3005c6751fb19cdeb6b10c066185908faf66a097/yolov8n.onnx" \
    -O models/yolov8n.onnx