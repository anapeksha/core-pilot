import { Buffer } from "node:buffer";
import type { IncomingMessage, ServerResponse } from "node:http";
import { URL } from "node:url";

import type { Plugin, ViteDevServer } from "vite";

type JsonObject = Record<string, unknown>;
type ModelCollection = "rooms" | "groups" | "scenes" | "automations";
type MockModel = JsonObject & { id: string };
type OtaUpload = {
  version?: unknown;
  size?: unknown;
  written: number;
  restart?: boolean;
};

const json = (
  response: ServerResponse,
  statusCode: number,
  body: unknown,
): void => {
  const payload = JSON.stringify(body);
  response.statusCode = statusCode;
  response.setHeader("Content-Type", "application/json");
  response.setHeader("Cache-Control", "no-store");
  response.end(payload);
};

const readJson = async (request: IncomingMessage): Promise<JsonObject> => {
  const chunks: Uint8Array[] = [];
  for await (const chunk of request) chunks.push(chunk);
  if (chunks.length === 0) return {};
  return JSON.parse(Buffer.concat(chunks).toString("utf8"));
};

const mutation = (status: string, detail: string, accepted = true) => ({
  accepted,
  status,
  detail,
});

export function mockRouterApi(): Plugin {
  const otaUploads = new Map<string, OtaUpload>();
  const models: Record<ModelCollection, MockModel[]> & { nextId: number } = {
    rooms: [{ id: "living-room", name: "Living Room" }],
    groups: [],
    scenes: [],
    automations: [],
    nextId: 1,
  };

  const nextModelId = (prefix: string): string =>
    `${prefix}-${String(models.nextId++).padStart(6, "0")}`;
  const upsertModel = (
    collection: ModelCollection,
    body: JsonObject,
    prefix: string,
  ): MockModel => {
    const id = String(body.id || nextModelId(prefix));
    const item: MockModel = { ...body, id };
    const existing = models[collection].findIndex((entry) => entry.id === id);
    if (existing >= 0) models[collection][existing] = item;
    else models[collection].push(item);
    return item;
  };
  const deleteModel = (collection: ModelCollection, body: JsonObject): void => {
    const id = String(body.id || "");
    models[collection] = models[collection].filter((entry) => entry.id !== id);
  };

  return {
    name: "core-pilot-mock-api",
    apply: "serve",
    configureServer(server: ViteDevServer) {
      server.middlewares.use(async (request, response, next) => {
        const url = new URL(request.url ?? "/", "http://corepilot.local");
        if (!url.pathname.startsWith("/api/v1/")) {
          next();
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/auth") {
          json(response, 200, { authentication_required: false });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/health") {
          json(response, 200, {
            product: "CorePilot",
            hostname: "corepilot.local",
            storage_ok: true,
            ai_enabled: true,
            auth: "none-trusted-lan",
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/system") {
          json(response, 200, {
            product: "CorePilot",
            hostname: "corepilot.local",
            firmware_version: "0.1.0-dev",
            setup_ap: "CorePilot-Setup-A1B2",
            display: "on",
            storage: "mounted",
            matter: "commissioning prerequisites pending",
            thread: "experimental; hardware readiness not validated",
            ai: "provider configured",
            ota: "ready",
            recovery: "armed",
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/network") {
          json(response, 200, {
            mode: "station-with-setup-fallback",
            setup_ssid: "CorePilot-Setup-A1B2",
            station_ssid: "MockHome",
            ipv4: "192.168.4.1",
            station_ipv4: "192.168.1.77",
            hostname: "corepilot.local",
            dhcp: "started",
            http: "started",
            wan_enabled: false,
            cloud_relay_enabled: false,
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/storage") {
          json(response, 200, {
            state: "mounted",
            sd_root: "/core-pilot/",
            encrypted: true,
            mutations_allowed: true,
            device_seed_exportable: false,
            detail:
              "Mock encrypted SD storage is available for frontend development.",
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/matter") {
          json(response, 200, {
            implementation: "rs-matter + Gateway H2 OpenThread RCP mock",
            commissioned: false,
            fabric_configured: false,
            subscriptions_active: 0,
            ipv6_ready: false,
            mdns_ready: false,
            dns_sd_ready: false,
            thread_ready: false,
            wifi_commissioning_ready: false,
            thread_commissioning_ready: false,
            detail:
              "Dev mock only: hardware Matter commissioning is intentionally not faked.",
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/devices") {
          json(response, 200, {
            devices: [],
            detail: "No commissioned devices in the dev mock.",
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/rooms") {
          json(response, 200, {
            rooms: models.rooms,
            detail: "Mock rooms are local to Vite dev server.",
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/groups") {
          json(response, 200, {
            groups: models.groups,
            detail: "Mock groups are local to Vite dev server.",
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/scenes") {
          json(response, 200, {
            scenes: models.scenes,
            detail: "Mock scenes are local to Vite dev server.",
          });
          return;
        }

        if (
          request.method === "GET" &&
          url.pathname === "/api/v1/automations"
        ) {
          json(response, 200, {
            automations: models.automations,
            detail: "Mock automations are local to Vite dev server.",
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/ai") {
          json(response, 200, {
            enabled: true,
            provider_configured: true,
            mcp_enabled: true,
            max_tool_rounds: 4,
            max_repeated_tool_calls: 2,
            max_actions: 8,
            destructive_confirmation: "CONFIRM",
            persistence:
              "Mock provider configuration; production uses encrypted SD records.",
          });
          return;
        }

        if (request.method === "GET" && url.pathname === "/api/v1/ai/memory") {
          json(response, 200, {
            encrypted: true,
            detail: "Mock memory sections for frontend development.",
            sections: [
              {
                id: "home_profile",
                title: "Home profile",
                markdown:
                  "Mock home profile. Production data is encrypted on SD.",
              },
              {
                id: "preferences",
                title: "Preferences",
                markdown:
                  "Prefer safe local automations and explicit confirmations.",
              },
            ],
          });
          return;
        }

        if (
          request.method === "GET" &&
          url.pathname === "/api/v1/ai/mcp/tools"
        ) {
          json(response, 200, {
            tools: [
              {
                name: "ListDevices",
                description: "List commissioned devices",
                destructive: false,
              },
              {
                name: "GetHubStatus",
                description: "Read CorePilot hub status",
                destructive: false,
              },
              {
                name: "ListScenes",
                description: "List local scenes",
                destructive: false,
              },
              {
                name: "ListAutomations",
                description: "List local automations",
                destructive: false,
              },
            ],
          });
          return;
        }

        if (request.method === "POST" && url.pathname === "/api/v1/ai/chat") {
          const body = await readJson(request);
          json(response, 200, {
            accepted: true,
            status: "completed",
            reply: `Mock response for: ${String(body.message ?? "").slice(0, 160)}. I can list hub status/devices/scenes in the dev router.`,
            actions: body.allow_actions
              ? [
                  {
                    tool: "GetHubStatus",
                    result_json: JSON.stringify({ status: "mock-ready" }),
                  },
                ]
              : [],
          });
          return;
        }

        if (
          request.method === "POST" &&
          url.pathname === "/api/v1/ota/prepare"
        ) {
          const body = await readJson(request);
          otaUploads.set("active", {
            version: body.version,
            size: body.size_bytes,
            written: 0,
            restart: body.restart_after_update === true,
          });
          json(
            response,
            200,
            mutation("prepared", "Mock OTA metadata accepted in dev memory."),
          );
          return;
        }

        if (request.method === "POST" && url.pathname === "/api/v1/ota/chunk") {
          const body = await readJson(request);
          const upload = otaUploads.get("active") ?? { written: 0 };
          upload.written = Number(body.offset ?? 0);
          otaUploads.set("active", upload);
          json(
            response,
            200,
            mutation(
              body.final_chunk ? "activated" : "chunk_staged",
              body.final_chunk
                ? "Mock OTA upload completed in dev memory. No image was activated."
                : "Mock OTA chunk accepted in dev memory.",
            ),
          );
          return;
        }

        const modelRoutes: Record<
          string,
          readonly [ModelCollection, string, string]
        > = {
          "/api/v1/rooms/upsert": ["rooms", "room", "Room"],
          "/api/v1/groups/upsert": ["groups", "group", "Group"],
          "/api/v1/scenes/upsert": ["scenes", "scene", "Scene"],
          "/api/v1/automations/upsert": [
            "automations",
            "automation",
            "Automation",
          ],
        };
        if (request.method === "POST" && url.pathname in modelRoutes) {
          const [collection, key, label] = modelRoutes[url.pathname];
          const body = await readJson(request);
          const item = upsertModel(collection, body, key);
          json(response, 200, {
            ...mutation(
              "saved",
              `${label} saved to mock encrypted model database.`,
            ),
            [key]: item,
          });
          return;
        }

        const modelDeleteRoutes: Record<
          string,
          readonly [ModelCollection, string, string]
        > = {
          "/api/v1/rooms/delete": ["rooms", "room", "Room"],
          "/api/v1/groups/delete": ["groups", "group", "Group"],
          "/api/v1/scenes/delete": ["scenes", "scene", "Scene"],
          "/api/v1/automations/delete": [
            "automations",
            "automation",
            "Automation",
          ],
        };
        if (request.method === "POST" && url.pathname in modelDeleteRoutes) {
          const [collection, key, label] = modelDeleteRoutes[url.pathname];
          const body = await readJson(request);
          deleteModel(collection, body);
          json(response, 200, {
            ...mutation(
              "deleted",
              `${label} deleted from mock encrypted model database.`,
            ),
            [key]: null,
          });
          return;
        }

        if (request.method === "POST") {
          await readJson(request).catch(() => ({}));
          const detailByPath: Record<string, string> = {
            "/api/v1/network/wifi": "Mock Wi‑Fi settings saved.",
            "/api/v1/matter/commission":
              "Mock router does not fake Matter commissioning; hardware validation is required.",
            "/api/v1/ai/provider": "Mock AI provider saved.",
            "/api/v1/ai/memory/section": "Mock AI memory section saved.",
            "/api/v1/ai/mcp/invoke": "Mock MCP tool invoked.",
          };
          if (url.pathname in detailByPath) {
            json(
              response,
              200,
              mutation(
                url.pathname === "/api/v1/matter/commission"
                  ? "blocked"
                  : "saved",
                detailByPath[url.pathname],
                url.pathname !== "/api/v1/matter/commission",
              ),
            );
            return;
          }
        }

        json(
          response,
          404,
          mutation("not_found", `No mock route for ${url.pathname}.`, false),
        );
      });
    },
  };
}
