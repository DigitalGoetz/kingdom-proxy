"use strict";

// A minimal example service to route through kingdom-proxy: a to-do list
// with both a UI and a JSON API in one container, using nothing but
// Node's built-in `http` module (no npm dependencies, so there's nothing
// to `npm install` -- just `node server.js`).
//
// State is in-memory only -- it resets on restart and there's no
// persistence, auth, or input sanitization beyond what's shown below. This
// is a routing demo, not a template for a real app.
//
// The one thing that *does* matter for anything routed through a
// path-stripping proxy like kingdom-proxy: index.html below fetches its
// API with *relative* paths ("api/todos", not "/api/todos"). A
// leading-slash path is resolved against the origin root by the browser,
// ignoring whatever prefix the page itself was reached under (e.g.
// "/demo/"); a relative one resolves against the current page's own URL,
// so it keeps working no matter what prefix kingdom-proxy mounts this
// app's route at.

const http = require("http");

const PORT = process.env.PORT || 8080;

let todos = [
  { id: 1, text: "Try kingdom-proxy", done: true },
  { id: 2, text: "Route this app through it", done: false },
];
let nextId = 3;

function sendJson(res, status, body) {
  const payload = JSON.stringify(body);
  res.writeHead(status, {
    "Content-Type": "application/json",
    "Content-Length": Buffer.byteLength(payload),
  });
  res.end(payload);
}

function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    let data = "";
    req.on("data", (chunk) => {
      data += chunk;
      if (data.length > 1_000_000) req.destroy(new Error("body too large"));
    });
    req.on("end", () => {
      if (!data) return resolve({});
      try {
        resolve(JSON.parse(data));
      } catch (e) {
        reject(e);
      }
    });
    req.on("error", reject);
  });
}

const INDEX_HTML = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>kingdom-proxy demo</title>
<style>
  :root { color-scheme: light dark; }
  body { font-family: system-ui, sans-serif; max-width: 480px; margin: 48px auto; padding: 0 16px; }
  h1 { font-size: 1.25rem; }
  p.subtitle { color: gray; margin-top: -8px; }
  form { display: flex; gap: 8px; margin-bottom: 20px; }
  input[type=text] { flex: 1; padding: 8px; font-size: 1rem; }
  button { padding: 8px 14px; font-size: 1rem; cursor: pointer; }
  ul { list-style: none; padding: 0; }
  li { display: flex; align-items: center; gap: 10px; padding: 8px 0; border-bottom: 1px solid #8883; }
  li.done span { text-decoration: line-through; opacity: 0.6; }
  li span { flex: 1; }
  li button { background: none; border: none; font-size: 1.1rem; cursor: pointer; }
  #error { color: #c0392b; min-height: 1.2em; }
</style>
</head>
<body>
<h1>kingdom-proxy demo</h1>
<p class="subtitle">A tiny to-do app -- API + UI in one container, routed through kingdom-proxy.</p>
<form id="form">
  <input type="text" id="text" placeholder="Add a to-do..." required>
  <button type="submit">Add</button>
</form>
<div id="error"></div>
<ul id="list"></ul>
<script>
  // Relative, not root-relative -- see the comment at the top of server.js
  // for why that's the one thing that matters for routing this correctly.
  const API = "api/todos";

  const list = document.getElementById("list");
  const form = document.getElementById("form");
  const input = document.getElementById("text");
  const errorBox = document.getElementById("error");

  async function api(path, options) {
    const res = await fetch(path, {
      ...options,
      headers: { "content-type": "application/json", ...options?.headers },
    });
    if (!res.ok) {
      const body = await res.json().catch(() => ({}));
      throw new Error(body.error || \`request failed (\${res.status})\`);
    }
    return res.status === 204 ? undefined : res.json();
  }

  function render(todos) {
    list.innerHTML = "";
    for (const todo of todos) {
      const li = document.createElement("li");
      li.className = todo.done ? "done" : "";

      const checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.checked = todo.done;
      checkbox.onchange = () => toggle(todo.id, checkbox.checked);

      const span = document.createElement("span");
      span.textContent = todo.text;

      const del = document.createElement("button");
      del.textContent = "\\u2715";
      del.title = "Delete";
      del.onclick = () => remove(todo.id);

      li.append(checkbox, span, del);
      list.append(li);
    }
  }

  async function refresh() {
    try {
      render(await api(API));
      errorBox.textContent = "";
    } catch (e) {
      errorBox.textContent = e.message;
    }
  }

  async function toggle(id, done) {
    try {
      await api(\`\${API}/\${id}\`, { method: "PATCH", body: JSON.stringify({ done }) });
      await refresh();
    } catch (e) {
      errorBox.textContent = e.message;
    }
  }

  async function remove(id) {
    try {
      await api(\`\${API}/\${id}\`, { method: "DELETE" });
      await refresh();
    } catch (e) {
      errorBox.textContent = e.message;
    }
  }

  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    const text = input.value.trim();
    if (!text) return;
    try {
      await api(API, { method: "POST", body: JSON.stringify({ text }) });
      input.value = "";
      await refresh();
    } catch (e) {
      errorBox.textContent = e.message;
    }
  });

  refresh();
</script>
</body>
</html>
`;

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, "http://internal");
  const path = url.pathname;

  try {
    if (req.method === "GET" && path === "/") {
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      res.end(INDEX_HTML);
      return;
    }

    if (req.method === "GET" && path === "/api/healthz") {
      return sendJson(res, 200, { status: "ok" });
    }

    if (req.method === "GET" && path === "/api/todos") {
      return sendJson(res, 200, todos);
    }

    if (req.method === "POST" && path === "/api/todos") {
      const body = await readJsonBody(req);
      const text = typeof body.text === "string" ? body.text.trim() : "";
      if (!text) return sendJson(res, 422, { error: "text is required" });
      const todo = { id: nextId++, text, done: false };
      todos.push(todo);
      return sendJson(res, 201, todo);
    }

    const itemMatch = path.match(/^\/api\/todos\/(\d+)$/);
    if (itemMatch) {
      const id = Number(itemMatch[1]);
      const todo = todos.find((t) => t.id === id);

      if (req.method === "PATCH") {
        if (!todo) return sendJson(res, 404, { error: "no to-do with that id" });
        const body = await readJsonBody(req);
        if (typeof body.done === "boolean") todo.done = body.done;
        if (typeof body.text === "string" && body.text.trim()) todo.text = body.text.trim();
        return sendJson(res, 200, todo);
      }

      if (req.method === "DELETE") {
        const before = todos.length;
        todos = todos.filter((t) => t.id !== id);
        if (todos.length === before) return sendJson(res, 404, { error: "no to-do with that id" });
        res.writeHead(204);
        return res.end();
      }
    }

    sendJson(res, 404, { error: "not found" });
  } catch (e) {
    sendJson(res, 400, { error: `invalid request: ${e.message}` });
  }
});

server.listen(PORT, () => {
  console.log(`kingdom-proxy demo app listening on :${PORT}`);
});
