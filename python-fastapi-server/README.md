# cv-inference-server-python

FastAPI version of the image inference server.

This project mirrors the HTTP contract of the C++ server:

- `GET /` returns `200`
- `GET /test` returns `200`
- `POST /detect` accepts a raw image body with an `image/*` content type and returns detection results as JSON

## Setup

1. Create and activate a Python environment.
2. Install dependencies:

```bash
pip install -r requirements.txt
```

3. Start the server:

```bash
export MODEL_PATH="../models/yolov8n.onnx"
uvicorn app.main:app --host 0.0.0.0 --port 9000
```

## Benchmark

Run the benchmark copy from this folder:

```bash
chmod +x scripts/benchmark_detect.sh
./scripts/benchmark_detect.sh
```
