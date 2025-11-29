# edac_ai

This repository contains a lightweight C++17 HTTP API and a Grok-styled HTML client for a multimodal assistant with text and audio endpoints.

## Repository layout
- `cpp/`: C++17 HTTP server exposing chat, audio, health triage, web search, session summary, diagnostics, subscription, and tool catalog endpoints.
- `html/`: Grok-styled web client to exercise chat, audio, health 1.0, web arama, oturum, abonelik ve araçlar akışlarını API üzerinden deneyimlemek için.

## Building and running the C++ API
1. Ensure a C++17 toolchain and CMake (>=3.16) are installed.
2. Build the server:
   ```bash
   cd cpp
   cmake -S . -B build
   cmake --build build
   ```
3. Run the API server (listens on port 8080 by default):
   ```bash
   ./build/edac_api
   ```
4. Endpoints:
   - `POST /api/chat` — JSON body with `text`, optional `session_id`, `mode`, and `stream` for structured replies, streaming chunks, intent label, tool suggestions, math çözümü, and recent turns.
   - `POST /api/audio/analyze` — JSON body with base64-encoded `audio_b64` and optional `transcript_hint` for preview + confidence response.
   - `POST /api/search` — JSON body with `query` to fetch synthetic web results (title, URL, snippet, freshness timestamp).
   - `POST /api/health/diagnose` — JSON body with `symptoms` (metin), optional `duration_days` and `age` for Health 1.0 triage suggestions.
   - `GET /api/subscriptions` — retrieve Basic, SuperGrok, and SuperGrok Heavy plans with fiyat and özellik listeleri.
   - `POST /api/account/create` — JSON body with `name`, `email`, optional `plan_id` to create a lightweight hesap kaydı.
   - `POST /api/subscriptions/select` — JSON body with `account_id` and `plan_id` to update the abonelik for an existing account.
   - `GET /health` — simple readiness probe.
   - `GET /api/session/{id}` — retrieve accumulated summary and recent turns for the session.
   - `GET /api/tools` — static catalog of suggested tools.
   - `GET /api/diagnostics` — lightweight status + endpoint list for dashboards, with session and account counters.

## Running the HTML demo
1. Serve the `html/` directory with any static server (for example, Python 3):
   ```bash
   cd html
   python3 -m http.server 8000
   ```
2. Open `http://localhost:8000` in a browser.
3. Use the chat, audio, Health 1.0 (belirti tarama), web arama, abonelik (hesap + planlar), oturum (session), araçlar (tools), and durum (diagnostics) panels in the left menu to interact with the running API at `http://localhost:8080`.

## Downloading the code from GitHub
If you are viewing the repository on GitHub and want to download everything as a ZIP (like the screenshot with the green **Code** button):
1. Click the **Code** dropdown near the top-right of the file list.
2. Choose **Download ZIP** to save the full project locally, then unzip it to see the `cpp/` and `html/` folders.
3. Alternatively, copy the URL from the same **Code** menu and clone via Git:
   ```bash
   git clone <repo-url>
   ```
   Replace `<repo-url>` with the HTTPS/SSH address shown in the menu.

## Notes
- The server is intentionally minimal and self-contained (no external dependencies beyond the standard library and POSIX sockets).
- Update endpoint URLs in `html/app.js` if you host the API on a different address/port.
