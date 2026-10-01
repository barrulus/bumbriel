#!/usr/bin/env bash
set -euo pipefail
export UMBRIEL_WINDOW_SCENE_1080P=1
export UMBRIEL_WINDOW_SCENE_LIT=1
source "$UMBRIEL_REPO/tests/harness/checks/animation/window_scene.sh"
