#!/usr/bin/env bash
set -euo pipefail
export UMBRIEL_PRESENTATION_FILTERED=1
source tests/harness/checks/effect/presentation_capture.sh
