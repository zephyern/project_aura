#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Daniel Abramowitz
# SPDX-License-Identifier: GPL-3.0-or-later

# Linux counterpart of scripts/build_idf.ps1: builds one hardware profile with
# ESP-IDF 6.1 through the same idf_prepare.py and idf.py steps. It only builds;
# it never flashes, opens a serial port or resets a board.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/build_idf.sh [--profile 4_3|7_dual_i2c] [--idf-path DIR]
                            [--tools-path DIR] [--jobs 1-32]
                            [--action build|reconfigure]

  --profile     Hardware profile (default: 4_3).
  --idf-path    ESP-IDF 6.1 checkout (default: $IDF_PATH; one of them is required).
  --tools-path  ESP-IDF tools directory (default: $IDF_TOOLS_PATH, if set).
  --jobs        Parallel compilation jobs, 1-32 (default: 4).
  --action      build or reconfigure (default: build).
EOF
}

fail() {
    echo "build_idf.sh: $*" >&2
    exit 2
}

need_value() {
    [[ $# -ge 2 && -n "$2" ]] || fail "$1 needs a value"
}

profile="4_3"
idf_path="${IDF_PATH:-}"
tools_path="${IDF_TOOLS_PATH:-}"
jobs=4
action="build"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --profile) need_value "$@"; profile="$2"; shift 2 ;;
        --idf-path) need_value "$@"; idf_path="$2"; shift 2 ;;
        --tools-path) need_value "$@"; tools_path="$2"; shift 2 ;;
        --jobs) need_value "$@"; jobs="$2"; shift 2 ;;
        --action) need_value "$@"; action="$2"; shift 2 ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; fail "unknown argument '$1'" ;;
    esac
done

case "$profile" in
    4_3 | 7_dual_i2c) ;;
    *) fail "--profile must be 4_3 or 7_dual_i2c (got '$profile')" ;;
esac
[[ "$jobs" =~ ^([1-9]|[12][0-9]|3[0-2])$ ]] || fail "--jobs must be 1-32 (got '$jobs')"
case "$action" in
    build | reconfigure) ;;
    *) fail "--action must be build or reconfigure (got '$action')" ;;
esac
[[ -n "$idf_path" ]] || fail "Set IDF_PATH or pass --idf-path to an ESP-IDF 6.1 checkout."

idf_root="$(realpath -e -- "$idf_path")" || fail "ESP-IDF path not found: $idf_path"
project_root="$(realpath -e -- "$(dirname -- "${BASH_SOURCE[0]}")/..")"
if [[ -n "$tools_path" ]]; then
    IDF_TOOLS_PATH="$(realpath -e -- "$tools_path")" || fail "ESP-IDF tools path not found: $tools_path"
    export IDF_TOOLS_PATH
fi
export IDF_PATH="$idf_root"
export IDF_PY_BUILD_JOBS="$jobs"

# export.sh is not written for "set -u"; activation failure is still checked.
set +u
# shellcheck source=/dev/null
. "$idf_root/export.sh" || fail "ESP-IDF environment activation failed."
set -u
[[ -n "${IDF_PYTHON_ENV_PATH:-}" ]] || fail "ESP-IDF environment activation did not set IDF_PYTHON_ENV_PATH."
python_exe="$IDF_PYTHON_ENV_PATH/bin/python"
[[ -x "$python_exe" ]] || fail "ESP-IDF Python not found at $python_exe."
build_dir="$project_root/build-idf-$profile"

cd -- "$project_root"
# Refresh identity before Ninja evaluates header dependencies after a commit.
"$python_exe" "$project_root/scripts/idf_prepare.py" --profile "$profile" --build-dir "$build_dir" \
    || fail "Aura native build preparation failed."
status=0
"$python_exe" "$idf_root/tools/idf.py" -B "$build_dir" \
    -D "AURA_PROFILE=$profile" -D "Python3_EXECUTABLE=$python_exe" "$action" || status=$?
(( status == 0 )) || fail "ESP-IDF $action failed with exit code $status"

if [[ "$action" == "build" ]]; then
    # build_idf.ps1 also writes generated/release-artifacts.json here. That stamp
    # only feeds the Windows installer packaging scripts, so this wrapper does
    # not produce installer-release builds.
    echo "build_idf.sh: built $build_dir (development build; installer packaging and its release stamp are Windows-only)."
fi
