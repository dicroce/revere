Revere Web UI
=============

The web UI is a Vue 3 + Vite single-page application that connects to the
Revere HTTP API (port 8088) to display camera feeds, motion events, and
analytics.

The compiled output (dist/) is checked into git so that Node.js is NOT
required to build Revere. Only developers modifying the UI need Node.

Rebuilding the Web UI
---------------------

Prerequisites:
  Node.js 18 or later  (https://nodejs.org)

Steps:

  cd libs/r_vss/webui
  npm install          # first time only — installs Vue, Vite, etc.
  npm run build        # compiles src/ into dist/

Then do a normal CMake build. The build system will repack dist/ into
ui.rbt automatically using r_bundle and copy it next to the binary.

When Revere starts it loads ui.rbt from the binary directory and serves
the web UI at:

  http://localhost:8088/

Development Mode (live reload)
-------------------------------

With Revere already running, start the Vite dev server:

  cd libs/r_vss/webui
  npm run dev

Then open http://localhost:5173/ in a browser. API calls are proxied to
the running Revere instance on port 8088, so live reload works without
restarting Revere or rebuilding.

Committing UI Changes
---------------------

After rebuilding, commit both the source changes and the updated dist/:

  git add src/ dist/
  git commit -m "..."

The dist/ directory is intentionally tracked in git.
