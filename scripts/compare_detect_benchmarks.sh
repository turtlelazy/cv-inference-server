#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
CPP_BENCHMARK="$ROOT_DIR/scripts/benchmark_detect.sh"
PY_BENCHMARK="$ROOT_DIR/python-fastapi-server/scripts/benchmark_detect.sh"
RESULTS_ROOT="${RESULTS_ROOT:-$ROOT_DIR/benchmark-results}"
TRIALS="${TRIALS:-1}"
REQUESTS="${REQUESTS:-200}"
CONCURRENCY="${CONCURRENCY:-4}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$RESULTS_ROOT/detect-$RUN_ID"
CPP_LOG="$RUN_DIR/cpp-benchmark.log"
PY_LOG="$RUN_DIR/python-benchmark.log"
SUMMARY_LOG="$RUN_DIR/summary.txt"
CPP_TRIAL_DIR="$RUN_DIR/cpp-trials"
PY_TRIAL_DIR="$RUN_DIR/python-trials"
FAILED=0

mkdir -p "$RUN_DIR" "$CPP_TRIAL_DIR" "$PY_TRIAL_DIR"

if ! [[ "$TRIALS" =~ ^[0-9]+$ && "$TRIALS" -gt 0 ]]; then
    printf 'Error: TRIALS must be a positive integer.\n' >&2
    exit 1
fi

while getopts ':n:c:h' opt; do
    case "$opt" in
        n) REQUESTS="$OPTARG" ;;
        c) CONCURRENCY="$OPTARG" ;;
        h)
            printf 'Usage: bash %s [-n requests] [-c concurrency]\n' "$0"
            exit 0
            ;;
        *)
            printf 'Usage: bash %s [-n requests] [-c concurrency]\n' "$0" >&2
            exit 1
            ;;
    esac
done

if ! [[ "$REQUESTS" =~ ^[0-9]+$ && "$REQUESTS" -gt 0 ]]; then
    printf 'Error: REQUESTS must be a positive integer.\n' >&2
    exit 1
fi

if ! [[ "$CONCURRENCY" =~ ^[0-9]+$ && "$CONCURRENCY" -gt 0 ]]; then
    printf 'Error: CONCURRENCY must be a positive integer.\n' >&2
    exit 1
fi

run_trial() {
    local label=$1
    local log_file=$2
    shift 2

    printf 'Running %s benchmark...\n' "$label"
    if "$@" 2>&1 | tee "$log_file"; then
        return 0
    fi

    printf '%s benchmark failed. See %s\n' "$label" "$log_file" >&2
    return 1
}


parse_benchmark_metric() {
    local file_path=$1
    local section=$2
    local metric=$3

    python3 - "$file_path" "$section" "$metric" <<'PY'
import re
import sys

path = sys.argv[1]
section = sys.argv[2]
metric = sys.argv[3]

with open(path, encoding="utf-8") as f:
    text = f.read()

if section == "sequential":
    block = re.search(
        r"Sequential run.*?(?=Concurrent run)",
        text,
        re.S
    )
elif section == "concurrent":
    block = re.search(
        r"Concurrent run.*",
        text,
        re.S
    )
else:
    sys.exit(1)

if not block:
    sys.exit(1)

block = block.group(0)

patterns = {
    "wall_time": r"wall time:\s*([0-9]+)",
    "throughput": r"throughput:\s*([0-9.]+)"
}

match = re.search(patterns[metric], block)

if not match:
    sys.exit(1)

print(match.group(1))
PY
}


parse_metric() {
    local file_path=$1
    local pattern=$2
    python3 - "$file_path" "$pattern" <<'PY'
import re
import sys

path = sys.argv[1]
pattern = sys.argv[2]

with open(path, 'r', encoding='utf-8') as handle:
    for line in handle:
        if pattern not in line:
            continue
        match = re.search(r'([0-9]+(?:\.[0-9]+)?)', line)
        if match:
            print(match.group(1))
            raise SystemExit(0)
raise SystemExit(1)
PY
}

stats_for_values() {
    local label=$1
    shift
    python3 - "$label" "$@" <<'PY'
import statistics
import sys

label = sys.argv[1]
values = [float(value) for value in sys.argv[2:]]

if not values:
    print(f'{label}: n/a')
    raise SystemExit(0)

print(
    f"{label}: trials={len(values)} "
    f"avg={statistics.mean(values):.2f} "
    f"median={statistics.median(values):.2f} "
    f"min={min(values):.2f} "
    f"max={max(values):.2f}"
)
PY
}

collect_trials() {
    local label=$1
    local benchmark_cmd=$2
    local trial_dir=$3
    local combined_log=$4
    shift 4
    local trial label_slug

    : >"$combined_log"
    label_slug=$(printf '%s' "$label" | tr '[:upper:]' '[:lower:]')

    for trial in $(seq 1 "$TRIALS"); do
        local trial_log="$trial_dir/${label_slug}-trial-${trial}.log"
        printf '\n=== %s trial %s/%s ===\n' "$label" "$trial" "$TRIALS" | tee -a "$combined_log"
        if ! eval "$benchmark_cmd" 2>&1 | tee "$trial_log"; then
            FAILED=1
        fi
        cat "$trial_log" >>"$combined_log"
    done
}

if [[ ! -f "$CPP_BENCHMARK" ]]; then
    printf 'Error: missing C++ benchmark script at %s\n' "$CPP_BENCHMARK" >&2
    exit 1
fi

if [[ ! -f "$PY_BENCHMARK" ]]; then
    printf 'Error: missing Python benchmark script at %s\n' "$PY_BENCHMARK" >&2
    exit 1
fi

collect_trials "cpp" "bash \"$CPP_BENCHMARK\" -n \"$REQUESTS\" -c \"$CONCURRENCY\"" "$CPP_TRIAL_DIR" "$CPP_LOG"
collect_trials "python" "env PATH=\"$ROOT_DIR/.venv/bin:$PATH\" bash \"$PY_BENCHMARK\" -n \"$REQUESTS\" -c \"$CONCURRENCY\"" "$PY_TRIAL_DIR" "$PY_LOG"

cpp_startups=()
cpp_seq_walls=()
cpp_seq_throughputs=()
cpp_conc_walls=()
cpp_conc_throughputs=()

py_startups=()
py_seq_walls=()
py_seq_throughputs=()
py_conc_walls=()
py_conc_throughputs=()

for trial_log in "$CPP_TRIAL_DIR"/*.log; do
    [[ -f "$trial_log" ]] || continue
    cpp_startups+=("$(parse_metric "$trial_log" 'Server ready in')")
    cpp_seq_walls+=("$(parse_benchmark_metric "$trial_log" sequential wall_time)")
    cpp_seq_throughputs+=("$(parse_benchmark_metric "$trial_log" sequential throughput)")
    cpp_conc_walls+=("$(parse_benchmark_metric "$trial_log" concurrent wall_time)")
    cpp_conc_throughputs+=("$(parse_benchmark_metric "$trial_log" concurrent throughput)")
done

for trial_log in "$PY_TRIAL_DIR"/*.log; do
    [[ -f "$trial_log" ]] || continue
    py_startups+=("$(parse_metric "$trial_log" 'Server ready in')")
    py_seq_walls+=("$(parse_benchmark_metric "$trial_log" sequential wall_time)")
    py_seq_throughputs+=("$(parse_benchmark_metric "$trial_log" sequential throughput)")
    py_conc_walls+=("$(parse_benchmark_metric "$trial_log" concurrent wall_time)")
    py_conc_throughputs+=("$(parse_benchmark_metric "$trial_log" concurrent throughput)")
done

{
    printf 'Comparison run: %s\n' "$RUN_ID"
    printf 'Results directory: %s\n' "$RUN_DIR"
    printf 'Trials per benchmark: %s\n' "$TRIALS"
    printf 'Requests per run: %s\n' "$REQUESTS"
    printf 'Concurrent requests: %s\n' "$CONCURRENCY"
    printf '\nC++ benchmark:\n'
    stats_for_values 'startup' "${cpp_startups[@]}"
    stats_for_values 'sequential wall time' "${cpp_seq_walls[@]}"
    stats_for_values 'sequential throughput' "${cpp_seq_throughputs[@]}"
    stats_for_values 'concurrent wall time' "${cpp_conc_walls[@]}"
    stats_for_values 'concurrent throughput' "${cpp_conc_throughputs[@]}"
    printf '\nPython benchmark:\n'
    stats_for_values 'startup' "${py_startups[@]}"
    stats_for_values 'sequential wall time' "${py_seq_walls[@]}"
    stats_for_values 'sequential throughput' "${py_seq_throughputs[@]}"
    stats_for_values 'concurrent wall time' "${py_conc_walls[@]}"
    stats_for_values 'concurrent throughput' "${py_conc_throughputs[@]}"
    printf '\nLogs:\n'
    printf '  %s\n' "$CPP_LOG"
    printf '  %s\n' "$PY_LOG"
} | tee "$SUMMARY_LOG"

printf '\nSummary saved to: %s\n' "$SUMMARY_LOG"
printf 'Full logs saved in: %s\n' "$RUN_DIR"

if [[ "$FAILED" -ne 0 ]]; then
    exit 1
fi
