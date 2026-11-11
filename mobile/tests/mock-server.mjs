// Minimal stand-in for the EverEye gateway: serves the exported PWA and a
// small stateful API so Playwright can exercise the real page without a laptop.
import { createServer } from "node:http";
import { readFile, stat } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../out/", import.meta.url));
const port = 3108;
const token = "test-token";

const seed = () => ({
  active: true,
  jobs: [
    { id: "j1", name: "claude", status: "Running", exitCode: 0, startedAt: 1, endedAt: 0, bytesTotal: 20 },
  ],
});
let state = seed();

const types = {
  ".html": "text/html; charset=utf-8",
  ".js": "application/javascript",
  ".css": "text/css",
  ".svg": "image/svg+xml",
  ".json": "application/json",
  ".webmanifest": "application/manifest+json",
  ".woff2": "font/woff2",
};

const json = (response, status, body) => {
  response.writeHead(status, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" });
  response.end(JSON.stringify(body));
};

const server = createServer(async (request, response) => {
  const url = new URL(request.url ?? "/", `http://127.0.0.1:${port}`);

  if (request.method === "OPTIONS") {
    response.writeHead(204, {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Headers": "authorization, content-type",
      "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    });
    response.end();
    return;
  }

  if (url.pathname === "/api/__reset") {
    state = seed();
    return json(response, 200, { ok: true });
  }

  if (url.pathname.startsWith("/api/")) {
    if ((request.headers.authorization ?? "") !== `Bearer ${token}`) {
      json(response, 401, { ok: false, error: "Unauthorized" });
      return;
    }
    const status = {
      ok: true,
      active: state.active,
      remainingSeconds: state.active ? 3725 : 0,
      runningJobs: state.jobs.filter((job) => job.status === "Running").length,
      batteryPercent: 82,
    };
    if (url.pathname === "/api/status") return json(response, 200, status);
    if (url.pathname === "/api/tasks") return json(response, 200, { ok: true, tasks: [{ name: "claude" }], allowArbitraryCommands: false });
    if (url.pathname === "/api/jobs" && request.method === "GET") return json(response, 200, { ok: true, jobs: state.jobs });
    if (url.pathname === "/api/session/end") { state.active = false; return json(response, 200, { ...status, active: false }); }
    if (url.pathname === "/api/session/extend") return json(response, 200, status);
    if (url.pathname === "/api/jobs" && request.method === "POST") {
      const job = { id: "j2", name: "claude", status: "Running", exitCode: 0, startedAt: Date.now(), endedAt: 0, bytesTotal: 0 };
      state.jobs.unshift(job);
      return json(response, 200, { ok: true, id: "j2", ...job });
    }
    const match = url.pathname.match(/^\/api\/jobs\/([^/]+)\/(stop|stream)$/);
    if (match) {
      const job = state.jobs.find((entry) => entry.id === match[1]);
      if (!job) return json(response, 404, { ok: false, error: "Unknown job" });
      if (match[2] === "stop") { job.status = "Killed"; return json(response, 200, { ok: true, ...job }); }
      response.writeHead(200, {
        "Content-Type": "text/event-stream",
        "Access-Control-Allow-Origin": "*",
        "Cache-Control": "no-cache",
        Connection: "keep-alive",
      });
      response.write(`data: ${JSON.stringify({ ok: true, data: "hello from mock\n", nextOffset: 15, eof: false })}\n\n`);
      setTimeout(() => {
        response.write("event: end\ndata: {}\n\n");
        response.end();
      }, 120);
      return;
    }
    return json(response, 404, { ok: false, error: "Not found" });
  }

  let path = normalize(url.pathname).replace(/^(\.\.(\/|\\|$))+/, "");
  if (path === "/" || path === "") path = "/index.html";
  const file = join(root, path);
  try {
    const info = await stat(file);
    if (!info.isFile()) throw new Error("not a file");
    const body = await readFile(file);
    response.writeHead(200, { "Content-Type": types[extname(file)] ?? "application/octet-stream" });
    response.end(body);
  } catch {
    const fallback = await readFile(join(root, "index.html")).catch(() => null);
    if (fallback) {
      response.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      response.end(fallback);
    } else {
      response.writeHead(404, { "Content-Type": "text/plain" });
      response.end("Build the app first: npm run build");
    }
  }
});

server.listen(port, "127.0.0.1", () => console.log(`mock gateway on http://127.0.0.1:${port}`));
