#!/usr/bin/env bash
# Same native close/open overlap and resource handoff with floating participants.
set -euo pipefail
export UMBRIEL_PRESENTATION_FLOATING=1
source tests/harness/checks/animation/presentation_overlap.sh
