#!/usr/bin/env bash
set -euo pipefail
export UMBRIEL_WINDOW_SCENE_PRODUCTION=1
source "$UMBRIEL_REPO/tests/harness/checks/animation/presentation_overlap.sh"
