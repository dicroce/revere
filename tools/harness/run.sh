#!/usr/bin/env bash
# Runs a harness suite: ./run.sh smoke [--revere-dir DIR] [--out DIR]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python="python3"
if [ -x "$here/.venv/bin/python" ]; then
    python="$here/.venv/bin/python"
fi

export PYTHONPATH="$here${PYTHONPATH:+:$PYTHONPATH}"
exec "$python" -m revere_harness "$@"
