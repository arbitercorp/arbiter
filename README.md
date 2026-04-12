<h1 align="center">Claudius</h1>

<p align="center">
  <strong>Lean agent orchestration runtime for Claude</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/tylerreckart/claudius?style=flat" alt="License"></a>
</p>

![Claudius Demo](./content/claudius.gif)

**Lightweight C++ agent orchestrator for the Claude API.**

- Talks to the Claude API over raw TLS (no libcurl, no HTTP library)
- Enforces a master constitution — formal, terse, token-efficient (derived from [JuliusBrussee/caveman](https://github.com/JuliusBrussee/caveman)) — cutting ~75% of output tokens
- Supports per-agent constitutions with custom personality, goals, rules, and three brevity levels: `lite`, `full`, `ultra`
- Agents can invoke `/fetch` and `/mem` commands autonomously — the orchestrator executes them and feeds results back in an agentic dispatch loop
- Runs as an interactive REPL, a TCP server for remote access, or a one-shot CLI
- Authenticates remote clients with SHA-256 hashed tokens
- Tracks token usage globally and per-agent

## Install

### Homebrew (macOS)

```bash
brew tap tylerreckart/tap
brew install claudius
```

Then:

```bash
export ANTHROPIC_API_KEY="sk-ant-..."
claudius --init
claudius
```

### Manual build

#### macOS

```bash
brew bundle
mkdir build && cd build
cmake .. -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
make -j$(sysctl -n hw.ncpu)
sudo cp claudius /usr/local/bin/
```

#### Linux (Ubuntu/Debian)

```bash
sudo apt install cmake libssl-dev build-essential
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo cp claudius /usr/local/bin/
```

## Quick Start

```bash
# Set your API key
export ANTHROPIC_API_KEY="sk-ant-..."

# Initialize config, generate auth token, create example agents
claudius --init

# Interactive mode
claudius

# Or start server for remote access
claudius --serve --port 9077
```

## Usage Modes

### Interactive REPL

```
$ claudius
[claudius] > hello, what agents are available?
Three agents loaded: reviewer, researcher, devops.
  [in:342 out:15]

[claudius] > /use researcher
Switched to: researcher

[researcher] > fetch the content at https://example.com and summarize it
/fetch https://example.com
[TOOL RESULTS]
[/fetch https://example.com]
<!doctype html>...
[END FETCH]
[END TOOL RESULTS]
Example Domain is a reserved domain maintained by IANA for illustrative purposes.
  [in:890 out:32]
```

Agents use `/fetch` automatically when asked to retrieve web content, and write to `/mem` when they learn something worth keeping.

### Agent Commands

All agents know about and can autonomously invoke system commands. Commands appear on their own line in the agent's response; the orchestrator executes them and feeds results back (up to 6 turns per message).

| Command | Description |
|---------|-------------|
| `/fetch <url>` | Fetch a webpage; result returned in next turn |
| `/mem write <text>` | Append a note to the agent's persistent memory |
| `/mem read` | Load the agent's memory into context |
| `/mem show` | Display raw memory file |
| `/mem clear` | Delete the agent's memory file |

You can also issue these as REPL commands yourself (e.g. `/fetch <url>` to manually inject content into the current agent's context).

Memory is stored per-agent at `~/.claudius/memory/<agent-id>.md`.

### Background Agent Loops

```
[claudius] > /loop researcher Research the latest Rust release notes.
Loop started: loop-0 (agent: researcher)

[claudius] > /loops
  loop-0  agent:researcher  state:running  iter:3  elapsed:8s
    last: Rust 1.78 introduces...

[claudius] > /log loop-0
[loop-0/researcher #1]
Rust 1.78 introduces...

[claudius] > /kill loop-0
Killed: loop-0
```

| Loop Command | Description |
|-------------|-------------|
| `/loop <agent> <prompt>` | Start agent in a background loop |
| `/loops` | List all running/suspended loops |
| `/log <id> [N]` | Show buffered output (last N entries) |
| `/kill <id>` | Stop a loop |
| `/suspend <id>` | Pause a loop |
| `/resume <id>` | Resume a paused loop |
| `/inject <id> <msg>` | Send a message into a running loop |

### Server Mode (remote access)

```bash
# Start server
claudius --serve --port 9077

# From another machine
claudius-cli myserver.local 9077 <your-token>

# Or manually with nc
nc myserver.local 9077
AUTH <your-token>
SEND researcher summarize the state of async Rust
QUIT
```

### One-shot

```bash
claudius --send reviewer "review: if (arr.length = 0) return;"
```

## Server Protocol

Line-based TCP protocol. All commands are newline-terminated.

| Command | Description |
|---------|-------------|
| `AUTH <token>` | Authenticate (required first) |
| `SEND <agent> <msg>` | Send message to agent (agent may use /fetch and /mem autonomously) |
| `ASK <query>` | Ask Claudius master about system state |
| `LIST` | List agents |
| `STATUS` | Full system status |
| `CREATE <id> [json]` | Create agent with optional JSON config |
| `REMOVE <id>` | Remove agent |
| `RESET <id>` | Clear agent history |
| `TOKENS` | Global token usage |
| `HELP` | Command list |
| `QUIT` | Disconnect |

Responses prefixed with `OK` or `ERR`.

## Agent Constitution

Each agent is defined by a JSON file in `~/.claudius/agents/`:

```json
{
  "name": "reviewer",
  "role": "code-reviewer",
  "personality": "Senior engineer. Finds fault efficiently. Praises only what deserves it.",
  "brevity": "ultra",
  "max_tokens": 512,
  "temperature": 0.2,
  "model": "claude-sonnet-4-20250514",
  "goal": "Inspect code. Identify defects. Prescribe remedies.",
  "rules": [
    "Defects first, style second.",
    "Prescribe the concrete fix, never vague counsel.",
    "If the code is sound, say so in one sentence and move on."
  ]
}
```

### Brevity Levels

| Level | Style | Token Savings |
|-------|-------|--------------|
| `lite` | Full grammar, no filler or hedging. Professional prose. | ~40% |
| `full` | Drop articles, fragments permitted. Short, declarative. | ~65% |
| `ultra` | Maximum compression. Abbreviations, arrows, minimal words. | ~75% |

### Constitution Layering

Every agent's system prompt is built in layers:

1. **Constitution** — voice rules, compression doctrine, brevity mode, exception handling, command capabilities
2. **Identity** — agent name and role
3. **Goal** — the agent's governing objective
4. **Rules** — explicit behavioral constraints

The master Claudius agent uses the same system but is configured for orchestration and meta-queries about system state.

## License

MIT
