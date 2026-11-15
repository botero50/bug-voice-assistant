# Legacy: the first (Docker) design

This is the first design of Bug: 12 to 19 Docker containers on a PC with the Claude API behind them. It was replaced by the current
design (the board answers by itself first; the Python server in `server/` is only the last resort, with a free local model). Nothing here is
used by the firmware, the server, the installer or the CI. It is kept for reference only; older documents (`docs/DEPLOYMENT.md`,
`docs/ROADMAP.md`, `DEPLOYMENT_PREP_GUIDE.md`) still describe it and are out of date.
