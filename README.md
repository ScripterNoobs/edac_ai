# edac_ai

This repository contains a lightweight C++17 HTTP API and a simple HTML client for a multimodal assistant with text and audio endpoints.

## Repository layout
- `cpp/`: C++17 HTTP server exposing chat, audio, health, and session summary endpoints.
- `html/`: Static web client to exercise chat and audio flows against the API.

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
   - `POST /chat` — JSON body with `message` and optional `session_id`/`intent_debug` for responses and detected intents.
   - `POST /audio` — JSON body with base64-encoded `audio` and optional flags for transcription/summary.
   - `GET /health` — simple readiness probe.
   - `GET /session/{id}` — retrieve the accumulated session summary.

## Running the HTML demo
1. Serve the `html/` directory with any static server (for example, Python 3):
   ```bash
   cd html
   python3 -m http.server 8000
   ```
2. Open `http://localhost:8000` in a browser.
3. Use the chat box or audio upload form to interact with the running API at `http://localhost:8080`.

## Notes
- The server is intentionally minimal and self-contained (no external dependencies beyond the standard library and POSIX sockets).
- Update endpoint URLs in `html/app.js` if you host the API on a different address/port.
