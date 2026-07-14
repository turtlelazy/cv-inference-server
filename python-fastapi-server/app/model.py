from __future__ import annotations

from io import BytesIO
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, UnidentifiedImageError
from ultralytics import YOLO


class Detector:
    def __init__(self, model_path: str | Path, conf_threshold: float = 0.25) -> None:
        self.model_path = Path(model_path)
        self.conf_threshold = conf_threshold
        self.model = YOLO(str(self.model_path))

    def detect_bytes(self, image_bytes: bytes) -> list[dict[str, Any]]:
        if not image_bytes:
            raise ValueError("Request body is empty")

        try:
            image = Image.open(BytesIO(image_bytes)).convert("RGB")
        except UnidentifiedImageError as exc:
            raise ValueError("Request body is not a supported image") from exc

        image_array = np.asarray(image)
        results = self.model.predict(image_array, conf=self.conf_threshold, verbose=False)

        if not results:
            return []

        result = results[0]
        class_names = result.names or self.model.names
        detections: list[dict[str, Any]] = []

        for box in result.boxes:
            class_id = int(box.cls.item())
            xyxy = box.xyxy[0].tolist()
            x1, y1, x2, y2 = [int(round(value)) for value in xyxy]
            detections.append(
                {
                    "class_id": class_id,
                    "class_name": class_names.get(class_id, str(class_id)),
                    "confidence": float(box.conf.item()),
                    "box": {
                        "x": x1,
                        "y": y1,
                        "width": max(0, x2 - x1),
                        "height": max(0, y2 - y1),
                    },
                }
            )

        return detections
