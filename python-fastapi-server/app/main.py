from __future__ import annotations

import os
from pathlib import Path

from fastapi import FastAPI, Request, Response
from fastapi.responses import JSONResponse

from .model import Detector


def _default_model_path() -> Path:
    return Path(__file__).resolve().parents[2] / "models" / "yolov8n.onnx"


MODEL_PATH = Path(os.environ.get("MODEL_PATH", _default_model_path()))

app = FastAPI(title="cv-inference-server-python", version="1.0.0")
detector = Detector(MODEL_PATH)


@app.get("/")
def root() -> Response:
    return Response(status_code=200)


@app.get("/test")
def test() -> Response:
    return Response(status_code=200)


@app.post("/detect")
async def detect(request: Request):
    content_type = request.headers.get("content-type", "")
    if not content_type.startswith("image/"):
        return JSONResponse(status_code=415, content={"detail": "Unsupported image content type"})

    image_bytes = await request.body()
    try:
        detections = detector.detect_bytes(image_bytes)
    except ValueError as exc:
        return JSONResponse(status_code=400, content={"detail": str(exc)})

    return JSONResponse(content={"detections": detections})
