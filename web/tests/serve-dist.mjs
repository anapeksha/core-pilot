import { createReadStream, existsSync } from "node:fs";
import { createServer } from "node:http";
import { extname, join, resolve } from "node:path";

const root = resolve("..", "dist");
const port = Number(process.env.PORT ?? 4173);
const types = new Map([
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".svg", "image/svg+xml"],
  [".png", "image/png"],
]);

const server = createServer((request, response) => {
  const url = new URL(request.url ?? "/", `http://${request.headers.host}`);
  let path = join(root, url.pathname === "/" ? "index.html" : url.pathname);
  if (!path.startsWith(root) || !existsSync(path)) {
    path = join(root, "index.html");
  }
  const gzipPath = `${path}.gz`;
  const servePath = existsSync(path) ? path : gzipPath;
  if (!existsSync(servePath)) {
    response.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    response.end("Not found");
    return;
  }
  const headers = {
    "Content-Type": types.get(extname(path)) ?? "application/octet-stream",
    "Cache-Control": "no-store",
  };
  if (servePath.endsWith(".gz")) {
    headers["Content-Encoding"] = "gzip";
  }
  response.writeHead(200, headers);
  createReadStream(servePath).pipe(response);
});

server.listen(port, "127.0.0.1", () => {
  console.log(`serving ${root} on ${port}`);
});
