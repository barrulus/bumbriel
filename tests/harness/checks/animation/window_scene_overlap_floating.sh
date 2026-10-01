#!/usr/bin/env bash
set -euo pipefail
export UMBRIEL_WINDOW_SCENE_PRODUCTION=1
export UMBRIEL_PRESENTATION_FLOATING=1
source "$UMBRIEL_REPO/tests/harness/checks/animation/presentation_overlap.sh"
