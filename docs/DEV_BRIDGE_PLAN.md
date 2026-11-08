# Bug as a QA automation companion: connecting the board to Claude Code, VS Code and your PC

**Goal:** the board on your desk becomes a hands-free control and status panel for your development work. You talk to it; it runs
things on your computer, reads the results, shows and tells you what matters, and takes a screenshot when you ask. It should help a QA
automation engineer: run suites, triage failures, watch pipelines, write and review tests, file tickets.

This document is the research and the plan. Nothing here is built yet. Sections 1 and 2 are the research (with sources); 3 to 6 are the plan.

---

## 1. What exists today (the building blocks, researched)

### 1.1 Claude Code can be driven by a program

- `claude -p "prompt"` runs Claude Code **non-interactively** and exits with code 0 on success. Output formats: `text`, `json` (result, session
  id, cost) and `stream-json` (one JSON event per line; add `--verbose --include-partial-messages` for live text). `--json-schema` forces the
  answer into a given structure (the answer appears in `structured_output`). `--continue` and `--resume <session_id>` keep a conversation
  going between calls, so "and now the second one" works. `--allowedTools "Read,Bash(git diff *)"` pre-approves exactly the tools you list, and
  `--permission-mode dontAsk` denies everything else. `--permission-prompts none` is for unattended runs. `--bare` starts faster and does not read
  the host's hooks, plugins and `CLAUDE.md`; it needs an API key, because bare mode ignores the subscription login.
  Source: [Run Claude Code programmatically](https://code.claude.com/docs/en/headless).
- The same engine is available as the **Claude Agent SDK** for Python (`claude-agent-sdk`) and TypeScript. Sources disagree on login: the
  default is an API key, and a help article cited by secondary sources says `claude -p` and the Agent SDK can still use your subscription
  limits for personal use, while building a product that offers subscription login to others is not allowed. For one person's own PC the
  subscription path is fine; verify the current terms before relying on it
  ([overview of both paths](https://dev.to/aviv_shaked/how-to-use-your-claude-promax-subscription-with-the-agent-sdk-python-typescript-4emi)).
  *Important for this project:* you have no API key, so the bridge uses `claude -p` with your logged-in subscription, not `--bare`.
- **Hooks** run your own commands on Claude Code events (`PreToolUse`, `PostToolUse`, `Stop`, `Notification`, `SessionStart`...), configured in
  `settings.json`. A `Stop` hook that calls the PC server is the simplest way for the board to say "Claude finished" with no polling
  ([hooks guide](https://academy.claude.com/courses/claude-code-101/hooks)).
- **Remote Control** (`claude remote-control`, `claude --rc`) lets you steer a running session from the Claude phone app or claude.ai/code. It is
  a separate official feature for phones, not for custom devices
  ([overview](https://dev.to/ayyazzafar/claude-code-remote-control-control-your-terminal-from-your-phone-55aj)); it does not replace this bridge.

### 1.2 MCP: the standard way to give Claude tools

- The **Model Context Protocol** lets any tool be offered to Claude Code, Claude Desktop and VS Code. A server is a small program with
  `@mcp.tool()` functions; transports are `stdio` (a local process) and *streamable HTTP* (a URL). Python: the official
  [python-sdk](https://github.com/modelcontextprotocol/python-sdk) (check the installed version's import: `FastMCP` or `MCPServer`).
  One trap: a stdio server must never print to stdout, or the JSON stream breaks.
- **Playwright MCP** (Microsoft) lets Claude drive a browser through accessibility snapshots and turn a walked-through flow into a Playwright
  spec; registration is one command, `claude mcp add playwright -- npx @playwright/mcp@latest`
  ([guide](https://www.builder.io/blog/playwright-mcp-server-claude-code)). This is the most useful QA tool to add first.
- Claude Code **in VS Code**: the extension runs a local IDE server (token in `~/.claude/ide/<port>.lock`); from an external terminal `/ide`
  connects to it, and diffs open in VS Code's own diff viewer ([docs](https://code.claude.com/docs/en/vs-code)).

### 1.3 The PC side, for the things the board cannot do

- **Screenshots:** the Python library `mss` is the usual choice on Windows (about 30 times faster than `PIL.ImageGrab`, native multi-monitor);
  check the monitor index on your own setup ([comparison](https://screenshotone.com/blog/mss-python-screen-capture/)). Claude can read an
  image file from a `claude -p` run, so "describe my screen" is a screenshot plus one `claude -p` call.
- **Test results** come as JUnit XML or Playwright JSON. Open-source triage tools show the pattern (read the report, compare with history,
  fingerprint failures, tell flaky from real) — [FlakeTriage](https://github.com/AleksandrJelohhin/flaketriage) is an example to learn from.
  For Jira, Xray has a GitHub Action and a Playwright reporter ([xray-action](https://github.com/marketplace/actions/xray-action),
  [playwright-xray](https://github.com/inluxc/playwright-xray)); both are third-party, so we would talk to Jira's REST API directly.
- **CI status** needs nothing new: the `gh` CLI already on this PC (`gh run list`, `gh run view --log-failed`) gives pipeline state and failed logs.

### 1.4 The board side

- The board already speaks HTTP to the PC and finds it by name on the network (mDNS). For **server-to-board push** the options are
  MQTT over TLS (the natural fit: pub/sub, reconnects by itself, but the TLS handshake needs roughly 30 to 40 KB of free memory),
  a WebSocket client (needs the server certificate checked or it does not verify), or **plain HTTP long polling**, which reuses what exists.
  Source: [ESP-IDF MQTT](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/api-reference/protocols/mqtt.html).
  Because memory is tight on this board, **start with long polling**, and keep MQTT as an upgrade.
- The board's speech path stays as built: the PC turns speech into words, the board decides, and answers are spoken with the recorded words or
  the free online voice. Dev answers are text, so they use the online voice (internet) or the PC's own voice.

### 1.5 Security: the part that must not be skipped

Letting a device trigger commands on your PC is remote command execution, so the research on AI agents applies fully
([OWASP guideline](https://owasp.github.io/DevSecOpsGuideline/2-Process/2-2-Develop/2-2-5-AI-Agent-and-MCP-Security/),
[Trail of Bits](https://blog.trailofbits.com/2025/10/22/prompt-injection-to-rce-in-ai-agents/)). The rules we will follow:

1. **Never a raw shell string from the board.** Only **named actions** from a file on the PC (`smoke`, `regression`, `lint`...), each with fixed arguments.
2. **Permission prompts are not a security boundary; isolation is.** Claude runs in a chosen project folder with a short `--allowedTools` list and
   `--permission-mode dontAsk`, so anything not listed is denied.
3. **Logs, test output, web pages and PR text are untrusted.** They can contain instructions aimed at the AI (prompt injection). Anything
   summarised from them is read-only: the summarising run has no write or network tools.
4. **Approvals show the exact action.** Dangerous actions (anything that writes, pushes, deletes or spends money) need a confirmation **on the board**:
   it shows and speaks "Voy a ejecutar: regresión en proyecto X. ¿Confirmas?" and waits for a tap.
5. **Pair the board with the PC once** (a random token kept on both; every request signed with a timestamp so it cannot be replayed), LAN only,
   rate limited, and **every request logged** with who, what and the result.
6. Secrets (tokens, passwords) stay on the PC and are never sent to the board.

---

## 2. What would be most useful to a QA automation engineer (the skills)

Ranked by value for effort:

| # | Skill (what you say) | What happens | Needs |
|---|---|---|---|
| 1 | "Cómo va el pipeline" / "qué falló anoche" | `gh run list` + failed log summary, spoken and shown (pass/fail counts, first failing test) | `gh` (present) |
| 2 | "Corre las pruebas de humo" / "regresión de checkout" | runs a named suite, status on the board while it runs, spoken result, link to the report | named actions file |
| 3 | "Pregúntale a Claude por qué falló X" | `claude -p` in the project, read-only tools, spoken summary; "y el siguiente" continues the same session | `claude` CLI |
| 4 | "Toma un pantallazo" / "qué ves en mi pantalla" | `mss` capture, thumbnail on the board, optional description | `mss` |
| 5 | Claude finished / needs you (notification) | a `Stop` / `Notification` hook tells the board; it chimes and says it | hooks |
| 6 | "Escribe una prueba para este flujo" | Playwright MCP walks the flow and writes the spec into the repo (writes: needs confirmation) | Playwright MCP |
| 7 | Flaky test triage ("cuáles son flaky esta semana") | a small history database of results by test; reruns suspicious ones | JUnit/JSON history |
| 8 | "Crea un ticket del fallo" | drafts the Jira issue from the failure (title, steps, evidence) and **asks before creating** | Jira REST token |
| 9 | "Revisa el pull request 123" | summary of the diff and the test impact | `gh` |
| 10 | Morning briefing ("buenos días") adds overnight CI, open PRs, today's suites | extends the briefing that exists | — |
| 11 | "Levanta / baja el ambiente" | `docker compose` named actions | Docker |
| 12 | Stand-up notes by voice: "anota que probé el login y falló" | appended to a daily notes file; end-of-day summary | — |

Also useful but later: accessibility scans (axe-core), visual-regression alerts, performance smoke (k6), API collections (Newman), test-data generation.

---

## 3. Architecture

```
  You ──voice──► Board ──words──► PC "bug_server"  ──►  dev bridge (new module)
                  ▲ ▲                                     ├─ named actions (allowlist)      ──► shell, gh, docker
                  │ └─ spoken answer + status card         ├─ claude -p runner (read-only)   ──► Claude Code (subscription)
                  └──── long poll / notifications ◄────────├─ screenshots (mss)
                                                           ├─ results parser (JUnit / Playwright JSON) + history
Claude Code / VS Code ──hooks & MCP "bug-dev" server────►  └─ event queue → board
```

- **Dev bridge** = a new Python module next to `bug_server.py`, with `/dev/*` endpoints: `status`, `actions`, `run`, `ask`, `screenshot`, `notify`, `events`.
- **Board** gets a new local skill group "Dev" (words like *pruebas, pipeline, pantallazo, pregúntale a Claude*), a **status card** screen
  (running / passed / failed with counts, drawn with the Lumina look) and a **confirmation sheet** for risky actions.
- **MCP server `bug-dev`** (stdio, Python) lets Claude Code or VS Code use the board: `bug_say(text)`, `bug_notify(level, text)`,
  `bug_show_status(card)`, `bug_get_events()`. So a long agent task can end with the board telling you the result.
- **VS Code:** start with the `code` command line (`code --goto file:line` opens the file the failure points at) and tasks; a small VS Code
  extension (run test at cursor, send the current error to the board) comes only if the CLI route is not enough.

## 4. Phases

| Phase | Work | Done when |
|---|---|---|
| **0 Foundations** — **built and tested** | pairing token + request signing; `/dev/notify` + `/dev/events` (long poll); board toast + spoken notification; the `Stop`/`Notification` hooks for Claude Code; audit log | Claude Code finishes a task and the board says so |
| **1 Run and watch** | named actions file; `/dev/run`; JUnit / Playwright JSON parser; status card on the board; `gh` pipeline skills (#1, #2) | "corre las pruebas de humo" ends with a spoken result |
| **2 Ask Claude** | `/dev/ask` with `claude -p`, project folders list, read-only tool list, session resume per project (#3, #9) | "pregúntale a Claude por qué falló el último test" is answered aloud |
| **3 Eyes** | `/dev/screenshot` with `mss`, thumbnail screen on the board (raw RGB565, about 1 s), "qué ves" description, privacy rules (which monitor, never while a password manager is focused) (#4) | screenshot and description work |
| **4 MCP + VS Code** | the `bug-dev` MCP server; Playwright MCP registered for test writing with confirmation (#6); `code --goto` actions | Claude Code can speak through the board |
| **5 QA depth** | results history + flaky triage (#7), Jira drafts (#8), briefing extension (#10), stand-up notes (#12) | the weekly flaky report is spoken on Monday |

Each phase ends with host tests (no hardware) and a written acceptance check, as in the rest of this project.

## 5. Risks and how they are handled

| Risk | Handling |
|---|---|
| Prompt injection through logs or PR text | read-only summarising runs; no network tools; confirmation on the board for anything that writes |
| A mis-heard command runs the wrong thing | named actions only; the board repeats the action before running anything that is not read-only |
| Subscription limits used up by many `claude -p` calls | cache answers per run id; cost shown in the `json` output (`total_cost_usd`); a daily cap in the bridge |
| The PC is asleep or off | the bridge replies "tu PC no responde"; the board keeps all its own skills (nothing here is needed for the rest of Bug) |
| Board memory is tight (about 35 KB free while TLS is open) | long polling over plain HTTP on the LAN; no extra TLS session; MQTT only if memory is freed first |
| Claude Code behaviour changes between versions | feature-detect from `system/init` events; pin the tested version in the bridge's check |

## 6. What I need from you (decisions that change the plan, not blockers)

1. **Which stack do you test with?** (Playwright, Selenium, Cypress, API tests with Postman/Newman, mobile with Appium?) The named actions and the
   results parser follow it. My default is Playwright with JUnit and JSON reports.
2. **Where do your repositories and pipelines live?** (GitHub with Actions is assumed; GitLab or Azure DevOps would change the "pipeline" skill.)
3. **Jira, TestRail or Xray?** Only needed for phase 5.
4. **Which actions may the board run without asking you to tap?** My default: read-only ones run directly; anything that writes, pushes or restarts asks first.

Until you answer, phases 0 to 3 can be built on the defaults above.

---

## 7. Phase 0 as built (how to use it)

Files: `server/dev_bridge.py` (the three endpoints), `server/tools/pair_board.py`, `server/tools/bug_notify.py`,
`server/tools/install_claude_hooks.py`, `firmware/main/dev_bridge.c`, and `server/dev_bridge_test.py` (14 checks, no hardware).

1. **Pair once** (the token travels over the USB cable only): `python server/tools/pair_board.py` (or paste `server/dev_token.txt` into
   "Dev bridge token" on the installer page's Set up the board). "What does the board have?" then shows `"dev": true`.
2. **Send a message** from anything on the PC: `python server/tools/bug_notify.py --level ok "Las pruebas de humo pasaron"`. Levels: info (blue),
   ok (green), warn (amber), error (red). `--quiet` shows it without saying it. The board shows it for 7 seconds and says it with the online voice.
3. **Claude Code hooks:** `python server/tools/install_claude_hooks.py` shows what would be added to `~/.claude/settings.json`; `--apply` adds a
   `Stop` hook ("Claude terminó en <project>") and a `Notification` hook ("Claude necesita tu permiso..."); `--remove` takes them out. They are
   **not** applied for you, because they would speak on every turn of every Claude Code session.
4. **Audit:** every request, allowed or refused, is a line in `server/dev_audit.log`.

Tested on the board: the board connected with a signed request (its address appears in the audit log), a message sent from the PC reached it
after a restart, and the online voice played it (`TTS: Online voice piece ... played`, the first time that path was heard working). Not tested:
the hooks inside a real Claude Code session, and the speech quality.

Design notes: the first time the board connects it ignores messages older than two minutes, so a restart does not replay yesterday's news; at most the
newest five are delivered after the board was off; a message the screen has not shown yet when six are waiting is dropped.
