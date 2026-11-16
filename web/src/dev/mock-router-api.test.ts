// @vitest-environment node
import { createServer, request as httpRequest } from "node:http";
import type { AddressInfo } from "node:net";
import type { Connect, ViteDevServer } from "vite";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { ActivityPage } from "../lib/api";
import { loadActivityPage } from "../lib/api";
import { mockRouterApi } from "./mock-router-api";

const cleanups: Array<() => Promise<void>> = [];
afterEach(async () => {
  vi.unstubAllGlobals();
  await Promise.all(cleanups.splice(0).map((cleanup) => cleanup()));
});

async function fixture() {
  let middleware: Connect.NextHandleFunction | undefined;
  const plugin = mockRouterApi();
  const configure = plugin.configureServer;
  if (typeof configure !== "function")
    throw new Error("Missing configure hook");
  configure.call(
    {} as never,
    {
      middlewares: {
        use(handler: Connect.NextHandleFunction) {
          middleware = handler;
        },
      },
    } as ViteDevServer,
  );
  const server = createServer((request, response) => {
    if (!middleware) throw new Error("Missing middleware");
    middleware(request, response, () => {
      response.statusCode = 404;
      response.end();
    });
  });
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  cleanups.push(
    () =>
      new Promise<void>((resolve, reject) => {
        server.close((error) => (error ? reject(error) : resolve()));
      }),
  );
  const { port } = server.address() as AddressInfo;
  return (body = "{}", method = "POST", contentType = "application/json") =>
    new Promise<{ status: number; body: ActivityPage; allow?: string }>(
      (resolve, reject) => {
        const request = httpRequest(
          {
            hostname: "127.0.0.1",
            port,
            path: "/api/v1/activity/page",
            method,
            headers: {
              "Content-Type": contentType,
              "Content-Length": Buffer.byteLength(body),
            },
          },
          (response) => {
            let payload = "";
            response.setEncoding("utf8");
            response.on("data", (chunk: string) => {
              payload += chunk;
            });
            response.on("end", () =>
              resolve({
                status: response.statusCode!,
                body: JSON.parse(payload),
                allow: response.headers.allow,
              }),
            );
          },
        );
        request.on("error", reject);
        request.end(body);
      },
    );
}

describe("mock activity middleware", () => {
  it("serves deterministic exclusive cursor pages through the final null cursor", async () => {
    const send = await fixture();
    const first = await send(
      JSON.stringify({ before_sequence: null, limit: 20 }),
    );
    expect(first.status).toBe(200);
    expect(first.body.events).toHaveLength(20);
    expect(first.body.detail).toContain("mock");
    expect(await send()).toEqual(first);
    const pages = [first.body];
    while (pages.at(-1)!.next_before_sequence !== null) {
      const result = await send(
        JSON.stringify({
          before_sequence: pages.at(-1)!.next_before_sequence,
          limit: 20,
        }),
      );
      expect(result.status).toBe(200);
      pages.push(result.body);
      expect(pages.length).toBeLessThan(5);
    }
    const events = pages.flatMap((page) => page.events);
    expect(events).toHaveLength(first.body.retained);
    expect(new Set(events.map((event) => event.sequence)).size).toBe(
      events.length,
    );
    expect(events.map((event) => event.sequence)).toEqual(
      events.map((event) => event.sequence).sort((a, b) => b - a),
    );
    expect(pages.at(-1)!.next_before_sequence).toBeNull();
    expect((await send('{"before_sequence":0}')).body.events).toEqual([]);
    expect((await send('{"limit":32}')).body.events).toHaveLength(32);
  });

  it("returns a response accepted by the actual activity client schema", async () => {
    const send = await fixture();
    const result = await send();
    vi.stubGlobal("window", { setTimeout, clearTimeout });
    const originalFetch = globalThis.fetch;
    globalThis.fetch = async () => new Response(JSON.stringify(result.body));
    try {
      await expect(loadActivityPage({ limit: 20 })).resolves.toEqual(
        result.body,
      );
    } finally {
      globalThis.fetch = originalFetch;
    }
  });

  it.each([
    "",
    "{",
    "null",
    "[]",
    '{"limit":0}',
    '{"limit":33}',
    '{"limit":1.5}',
    '{"limit":"20"}',
    '{"before_sequence":-1}',
    '{"before_sequence":4294967296}',
    '{"before_sequence":1.5}',
    '{"unsupported":true}',
  ])("rejects malformed or unsupported input %s", async (body) => {
    const send = await fixture();
    const result = await send(body);
    expect(result.status).toBe(400);
    expect(result.body).not.toHaveProperty("accepted");
    expect((await send()).body.events).toHaveLength(20);
  });

  it("bounds request bytes and rejects unsupported media types and methods", async () => {
    const send = await fixture();
    expect(
      (await send(JSON.stringify({ extra: "x".repeat(1024) }))).status,
    ).toBe(413);
    expect((await send("{}", "POST", "text/plain")).status).toBe(415);
    for (const method of ["GET", "PUT", "DELETE"]) {
      const result = await send("", method);
      expect(result.status).toBe(405);
      expect(result.allow).toBe("POST");
      expect(result.body).not.toHaveProperty("accepted");
    }
  });
});
