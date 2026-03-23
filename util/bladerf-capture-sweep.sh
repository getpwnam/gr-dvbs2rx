#!/usr/bin/env bash
set -euo pipefail

# Capture one AGC recording plus one recording per manual gain value.
# Example:
#   ./util/bladerf-capture-sweep.sh \
#     --freq 741500000 --sample-rate 12000000 --bandwidth 8000000 \
#     --num-samples 240000000 --gains 34,36,38,40 --prefix cap_741500

FREQ=741500000
SAMPLE_RATE=12000000
BANDWIDTH=8000000
NUM_SAMPLES=240000000
GAINS_CSV="34,36,38"
OUT_DIR="/tmp"
PREFIX="cap_sc16"
DEVICE=""
DRY_RUN=0

usage() {
  cat <<'EOF'
Capture bladeRF IQ files sequentially (AGC + named manual gains).

Options:
  --freq HZ              RX center frequency in Hz (default: 741500000)
  --sample-rate HZ       RX sample rate in Hz (default: 12000000)
  --bandwidth HZ         RX bandwidth in Hz (default: 8000000)
  --num-samples N        Number of complex samples to capture per file (default: 240000000)
  --gains LIST           Comma-separated manual gains in dB (default: 34,36,38)
  --out-dir PATH         Output directory (default: /tmp)
  --prefix NAME          Output filename prefix (default: cap_sc16)
  --device SPEC          Optional bladeRF device spec passed to open
  --dry-run              Print commands only
  -h, --help             Show this help

Output files:
  <out-dir>/<prefix>_agc.bin
  <out-dir>/<prefix>_g<gain>.bin
EOF
}

log() {
  printf '[%s] %s\n' "$(date '+%H:%M:%S')" "$*"
}

run_cli() {
  local cmd_script="$1"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    log "DRY RUN: bladeRF-cli -e \"$cmd_script\""
  else
    bladeRF-cli -e "$cmd_script"
  fi
}

capture_agc() {
  local out_file="$1"
  log "Capturing AGC -> $out_file"
  run_cli "${DEVICE:+open $DEVICE; }set frequency rx $FREQ; set samplerate rx $SAMPLE_RATE; set bandwidth rx $BANDWIDTH; set agc rx off; set agc rx on; rx config file=$out_file format=bin n=$NUM_SAMPLES; rx start; print agc rx; print gain rx; rx wait; rx stop; quit"
}

capture_gain() {
  local gain="$1"
  local out_file="$2"
  log "Capturing gain=${gain} dB -> $out_file"
  run_cli "${DEVICE:+open $DEVICE; }set frequency rx $FREQ; set samplerate rx $SAMPLE_RATE; set bandwidth rx $BANDWIDTH; set agc rx off; set gain rx $gain; rx config file=$out_file format=bin n=$NUM_SAMPLES; rx start; print agc rx; print gain rx; rx wait; rx stop; quit"
}

main() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --freq)
        FREQ="$2"
        shift 2
        ;;
      --sample-rate)
        SAMPLE_RATE="$2"
        shift 2
        ;;
      --bandwidth)
        BANDWIDTH="$2"
        shift 2
        ;;
      --num-samples)
        NUM_SAMPLES="$2"
        shift 2
        ;;
      --gains)
        GAINS_CSV="$2"
        shift 2
        ;;
      --out-dir)
        OUT_DIR="$2"
        shift 2
        ;;
      --prefix)
        PREFIX="$2"
        shift 2
        ;;
      --device)
        DEVICE="$2"
        shift 2
        ;;
      --dry-run)
        DRY_RUN=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        echo "Unknown option: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
  done

  if ! command -v bladeRF-cli >/dev/null 2>&1; then
    echo "bladeRF-cli not found in PATH" >&2
    exit 127
  fi

  mkdir -p "$OUT_DIR"

  local agc_file="$OUT_DIR/${PREFIX}_agc.bin"
  capture_agc "$agc_file"

  IFS=',' read -r -a gains <<<"$GAINS_CSV"
  for g in "${gains[@]}"; do
    g="${g// /}"
    [[ -z "$g" ]] && continue
    local out_file="$OUT_DIR/${PREFIX}_g${g}.bin"
    capture_gain "$g" "$out_file"
  done

  log "Done. Files written under: $OUT_DIR"
}

main "$@"
