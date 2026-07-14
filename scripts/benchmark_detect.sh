#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR="$ROOT_DIR/build"
SERVER_BIN="$BUILD_DIR/cvserve"
MODEL_PATH="$ROOT_DIR/models/yolov8n.onnx"
BASE_URL="${BASE_URL:-http://127.0.0.1:9000}"
IMAGE_DIR="${IMAGE_DIR:-$ROOT_DIR/tests/detect-images}"
REQUESTS=200
CONCURRENCY=50
WARMUP_REQUESTS=1
STARTUP_TIMEOUT_SECONDS=600
FAILED=0

usage() {
    cat <<'EOF'
Usage: benchmark_detect.sh [-n requests] [-c concurrency] [-w warmup] [-t startup_timeout_seconds] [-d image_dir]

Options:
  -n  Total number of benchmark requests per run. Default: 200
  -c  Parallel requests to issue during the concurrent run. Default: 50
  -w  Warmup requests to send before measuring. Default: 1
  -t  Startup timeout in seconds while waiting for /test. Default: 600
  -d  Folder containing test images. Default: tests/detect-images
  -h  Show this help message

Environment:
  BASE_URL   Base URL for the running server. Default: http://127.0.0.1:9000
  IMAGE_DIR  Folder containing test images. Default: tests/detect-images
EOF
}

now_ms() {
    python3 - <<'PY'
import time
print(int(time.perf_counter() * 1000))
PY
}

content_type_for_file() {
    local image_path=$1
    case "${image_path##*.}" in
        bmp) printf 'image/bmp' ;;
        gif) printf 'image/gif' ;;
        jpeg|jpg) printf 'image/jpeg' ;;
        pgm) printf 'image/x-portable-graymap' ;;
        png) printf 'image/png' ;;
        ppm) printf 'image/x-portable-pixmap' ;;
        pbm) printf 'image/x-portable-bitmap' ;;
        webp) printf 'image/webp' ;;
        *) return 1 ;;
    esac
}

print_error() {
    printf 'Error: %s\n' "$1" >&2
    if [[ -f "${SERVER_LOG:-}" ]]; then
        printf '\nServer log:\n' >&2
        tail -n 40 "$SERVER_LOG" >&2 || true
    fi
}

cleanup() {
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi

    if [[ "$FAILED" -eq 0 ]]; then
        rm -f "${SERVER_LOG:-}" "${SEQ_RESULTS_FILE:-}" "${CONC_RESULTS_FILE:-}" "${WARMUP_FILE:-}"
        rm -rf "${RESPONSE_DIR:-}" 2>/dev/null || true
    else
        printf 'Benchmark failed; preserving temp files for inspection:\n' >&2
        printf '  server log: %s\n' "${SERVER_LOG:-}" >&2
        printf '  sequential results: %s\n' "${SEQ_RESULTS_FILE:-}" >&2
        printf '  concurrent results: %s\n' "${CONC_RESULTS_FILE:-}" >&2
        printf '  warmup results: %s\n' "${WARMUP_FILE:-}" >&2
        printf '  response dir: %s\n' "${RESPONSE_DIR:-}" >&2
    fi
}

trap cleanup EXIT

while getopts ':n:c:w:t:d:h' opt; do
    case "$opt" in
        n) REQUESTS="$OPTARG" ;;
        c) CONCURRENCY="$OPTARG" ;;
        w) WARMUP_REQUESTS="$OPTARG" ;;
        t) STARTUP_TIMEOUT_SECONDS="$OPTARG" ;;
        d) IMAGE_DIR="$OPTARG" ;;
        h)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 1
            ;;
    esac
done

if [[ ! -x "$SERVER_BIN" ]]; then
    print_error "Missing server binary at $SERVER_BIN. Build the project first."
    FAILED=1
    exit 1
fi

if [[ ! -f "$MODEL_PATH" ]]; then
    print_error "Missing model file at $MODEL_PATH. Run scripts/download_models.sh first."
    FAILED=1
    exit 1
fi

if [[ ! -d "$IMAGE_DIR" ]]; then
    print_error "Missing image directory at $IMAGE_DIR."
    FAILED=1
    exit 1
fi

if ! [[ "$REQUESTS" =~ ^[0-9]+$ && "$REQUESTS" -gt 0 ]]; then
    print_error "-n must be a positive integer."
    FAILED=1
    exit 1
fi

if ! [[ "$CONCURRENCY" =~ ^[0-9]+$ && "$CONCURRENCY" -gt 0 ]]; then
    print_error "-c must be a positive integer."
    FAILED=1
    exit 1
fi

if ! [[ "$WARMUP_REQUESTS" =~ ^[0-9]+$ && "$WARMUP_REQUESTS" -ge 0 ]]; then
    print_error "-w must be a non-negative integer."
    FAILED=1
    exit 1
fi

if ! [[ "$STARTUP_TIMEOUT_SECONDS" =~ ^[0-9]+$ && "$STARTUP_TIMEOUT_SECONDS" -gt 0 ]]; then
    print_error "-t must be a positive integer."
    FAILED=1
    exit 1
fi

IMAGE_FILES=()
while IFS= read -r image_path; do
    if [[ -n "$image_path" ]] && content_type_for_file "$image_path" >/dev/null 2>&1; then
        IMAGE_FILES+=("$image_path")
    fi
done < <(find "$IMAGE_DIR" -maxdepth 1 -type f | sort)

if [[ "${#IMAGE_FILES[@]}" -eq 0 ]]; then
    print_error "No image files found in $IMAGE_DIR."
    FAILED=1
    exit 1
fi

SERVER_LOG=$(mktemp -t cvserve-benchmark-server.XXXXXX.log)
SEQ_RESULTS_FILE=$(mktemp -t cvserve-benchmark-seq.XXXXXX.txt)
CONC_RESULTS_FILE=$(mktemp -t cvserve-benchmark-conc.XXXXXX.txt)
WARMUP_FILE=$(mktemp -t cvserve-benchmark-warmup.XXXXXX.txt)
RESPONSE_DIR=$(mktemp -d -t cvserve-benchmark-responses.XXXXXX)

request_detect() {
    local image_path=$1
    local output_path=$2
    local content_type

    if ! content_type=$(content_type_for_file "$image_path"); then
        printf 'Unsupported file extension for %s\n' "$image_path" >&2
        return 1
    fi

    curl -sS -o /dev/null -w '%{http_code} %{time_total}\n' \
        -X POST \
        -H "Content-Type: $content_type" \
        --data-binary "@$image_path" \
        "$BASE_URL/detect" >"$output_path"
}

run_batch() {
    local label=$1
    local parallelism=$2
    local total_requests=$3
    local output_file=$4
    local batch_start_ms batch_end_ms batch_ms throughput
    local request_index=0
    local request_failures=0
    local active_jobs=0
    local pids=""

    : >"$output_file"
    rm -f "$RESPONSE_DIR"/* 2>/dev/null || true

    batch_start_ms=$(now_ms)

    while (( request_index < total_requests )); do
        local image_path response_path response_prefix
        image_path=${IMAGE_FILES[$((request_index % ${#IMAGE_FILES[@]}))]}

        case "$label" in
            Sequential) response_prefix="sequential" ;;
            Concurrent) response_prefix="concurrent" ;;
            *) response_prefix="batch" ;;
        esac

        response_path="$RESPONSE_DIR/${response_prefix}-${request_index}.txt"
        (request_detect "$image_path" "$response_path") &
        pids="$pids $!"
        request_index=$((request_index + 1))
        active_jobs=$((active_jobs + 1))

        if (( active_jobs == parallelism )); then
            local pid
            for pid in $pids; do
                if ! wait "$pid"; then
                    request_failures=1
                fi
            done
            pids=""
            active_jobs=0
        fi
    done

    local pid
    for pid in $pids; do
        if ! wait "$pid"; then
            request_failures=1
        fi
    done

    batch_end_ms=$(now_ms)

    for response_path in "$RESPONSE_DIR"/*.txt; do
        [[ -f "$response_path" ]] || continue
        cat "$response_path" >>"$output_file"
    done

    if [[ "$request_failures" -ne 0 ]]; then
        FAILED=1
        print_error "$label run failed while issuing requests."
        exit 1
    fi

    if ! awk '$1 != 200 { exit 1 }' "$output_file"; then
        FAILED=1
        print_error "$label run returned at least one non-200 response."
        exit 1
    fi

    local count min_time max_time avg_time p95_time
    count=$(wc -l <"$output_file" | tr -d ' ')
    if [[ "$count" -ne "$total_requests" ]]; then
        FAILED=1
        print_error "$label run returned $count responses for $total_requests requests."
        exit 1
    fi

    min_time=$(awk 'NR == 1 || $2 < min { min = $2 } END { printf "%.6f", min }' "$output_file")
    max_time=$(awk 'NR == 1 || $2 > max { max = $2 } END { printf "%.6f", max }' "$output_file")
    avg_time=$(awk '{ sum += $2 } END { printf "%.6f", sum / NR }' "$output_file")
    p95_time=$(awk '{ print $2 }' "$output_file" | sort -n | awk -v n="$count" 'NR == int((n * 95 + 99) / 100) { print; exit }')

    batch_ms=$((batch_end_ms - batch_start_ms))
    if (( batch_ms <= 0 )); then
        batch_ms=1
    fi

    throughput=$(awk -v n="$count" -v ms="$batch_ms" 'BEGIN { printf "%.2f", (n * 1000) / ms }')

    printf '\n%s run (%s parallel, %s requests)\n' "$label" "$parallelism" "$count"
    printf '  wall time: %s ms\n' "$batch_ms"
    printf '  throughput: %s req/s\n' "$throughput"
    printf '  latency: min=%s s avg=%s s p95=%s s max=%s s\n' "$min_time" "$avg_time" "$p95_time" "$max_time"
}

cd "$BUILD_DIR"
printf 'Starting server...\n'
"$SERVER_BIN" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

startup_start_ms=$(now_ms)
startup_timeout_ms=$((STARTUP_TIMEOUT_SECONDS * 1000))

until curl -fsS "$BASE_URL/test" >/dev/null 2>&1; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        FAILED=1
        print_error "Server exited before becoming ready."
        exit 1
    fi

    current_ms=$(now_ms)
    if (( current_ms - startup_start_ms > startup_timeout_ms )); then
        FAILED=1
        print_error "Timed out waiting for server readiness at $BASE_URL/test."
        exit 1
    fi
done

startup_end_ms=$(now_ms)
startup_ms=$((startup_end_ms - startup_start_ms))

printf 'Server ready in %s ms\n' "$startup_ms"
printf 'Using image folder: %s\n' "$IMAGE_DIR"
printf 'Warming up /detect...\n'

if (( WARMUP_REQUESTS > 0 )); then
    request_index=0
    : >"$WARMUP_FILE"
    while (( request_index < WARMUP_REQUESTS )); do
        image_path=${IMAGE_FILES[$((request_index % ${#IMAGE_FILES[@]}))]}
        if ! request_detect "$image_path" "$WARMUP_FILE"; then
            FAILED=1
            print_error "Warmup request failed."
            exit 1
        fi
        request_index=$((request_index + 1))
    done
fi

run_batch "Sequential" 1 "$REQUESTS" "$SEQ_RESULTS_FILE"
run_batch "Concurrent" "$CONCURRENCY" "$REQUESTS" "$CONC_RESULTS_FILE"

printf '\nServer log stored at: %s\n' "$SERVER_LOG"