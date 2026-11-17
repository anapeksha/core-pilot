import { Buffer } from "node:buffer";

import AxeBuilder from "@axe-core/playwright";
import { expect, test, type Page, type Route } from "@playwright/test";
import type { NetworkStatus } from "../src/lib/api";

type Json = Record<string, unknown>;

type FirmwareState = ReturnType<typeof createFirmwareState>;

function createFirmwareState() {
  const network: NetworkStatus = {
    mode: "station",
    setup_ssid: "CorePilot-Setup-1234",
    station_ssid: "Lab_WiFi",
    station_bssid: "aa:bb:cc:dd:ee:ff",
    ipv4: "192.168.29.2",
    station_ipv4: "192.168.29.2",
    hostname: "corepilot.local",
    dhcp: "started",
    http: "started",
    wan_enabled: false,
    cloud_relay_enabled: false,
  };
  return {
    health: {
      product: "Core Pilot",
      hostname: "corepilot.local",
      storage_ok: true,
      ai_enabled: true,
      auth: "none-trusted-lan",
    },
    network,
    system: {
      product: "Core Pilot",
      hostname: "corepilot.local",
      firmware_version: "0.1.0-test",
      setup_ap: "CorePilot-Setup-1234",
      display: "on",
      storage: "mounted",
      matter: "setup needed",
      thread: "setup needed",
      ai: "HTTP provider transport ready",
      ota: "local updates",
      ota_phase: "idle",
      ota_progress_bytes: 0,
      ota_total_bytes: 0,
      ota_restart_after_update: false,
      recovery: "on",
    },
    time: {
      ntp_server_id: "cloudflare",
      ntp_server_host: "time.cloudflare.com",
      timezone_id: "UTC",
      timezone_label: "UTC · UTC+00:00",
      timezone_offset_minutes: 0,
      timezone_offset_label: "UTC+00:00",
      dns: "ready",
      ntp: "synced",
      rtc: "synced",
      last_sync_unix_seconds: 1_700_000_000,
      current_unix_seconds: 1_700_000_000,
      current_date: "2023-11-14",
      current_time: "22:13:20",
      detail: "Time synchronized.",
    },
    storage: {
      state: "mounted",
      sd_root: "/core-pilot/",
      encrypted: true,
      mutations_allowed: true,
      device_seed_exportable: false,
      detail: "SD card is mounted.",
    },
    matter: {
      implementation: "rs-matter adapter",
      commissioned: false,
      fabric_configured: false,
      subscriptions_active: 0,
      ipv6_ready: true,
      mdns_ready: true,
      dns_sd_ready: true,
      thread_ready: false,
      thread_dataset_configured: false,
      wifi_commissioning_ready: false,
      thread_commissioning_ready: false,
      adapter_status: "validation-only",
      parser_ready: true,
      transport_adapter_ready: true,
      fabric_credentials_ready: false,
      pase_ready: true,
      case_ready: false,
      subscriptions_ready: true,
      operation: { active: null, pending: null, last_terminal: null },
      detail:
        "Matter setup is modeled but commissioning is blocked until hardware validation.",
    },
    ai: {
      enabled: true,
      provider_configured: true,
      mcp_enabled: true,
      max_tool_rounds: 8,
      max_repeated_tool_calls: 3,
      max_actions: 8,
      destructive_confirmation: "CONFIRM",
      persistence: "encrypted SD only",
      provider_transport: {
        http_ipv4_literal: false,
        dns: true,
        tls: true,
        websockets: false,
        certificate_validation: true,
        detail: "Official HTTPS provider transport ready.",
      },
      provider_transport_requirements: [],
    },
    matterOperations: {
      operations: [
        {
          cursor: "7",
          operation_id: 42,
          kind: "set_on_off",
          state: "complete",
          device_id: "0000000000001234",
          endpoint_id: 1,
          error: "ESP_OK",
        },
      ],
      cursor: "7",
      limit: 8,
      retained: 1,
      detail: "Matter terminal operations returned.",
    },
    devices: [
      {
        id: "matter-light-1",
        name: "Bench Lamp",
        protocol: "Matter",
        availability: "online",
        room: "lab",
        capabilities: ["On/Off", "Level"],
        node_id: "0000000000001234",
        endpoint_id: 1,
        subscription: {
          health: "active",
          case_ready: true,
          recovery_pending: false,
          configured: true,
          active: true,
          report_known: true,
        },
      },
    ],
    rooms: [{ id: "lab", name: "Lab" }],
    groups: [
      {
        id: "bench-lights",
        name: "Bench lights",
        device_ids: ["matter-light-1"],
      },
    ],
    scenes: [
      { id: "evening", name: "Evening", actions: ["on|matter-light-1|1"] },
    ],
    activity: [
      {
        sequence: 12,
        unix_seconds: 1_700_000_000,
        uptime_seconds: 400,
        kind: "automation_queued",
        detail: "Manual automation was queued for local evaluation.",
      },
      {
        sequence: 11,
        unix_seconds: null,
        uptime_seconds: 380,
        kind: "configuration_saved",
        detail: "Configuration was saved to encrypted storage.",
      },
      {
        sequence: 10,
        unix_seconds: null,
        uptime_seconds: 350,
        kind: "wifi_join_failed",
        detail: "Wi-Fi join failed.",
      },
    ],
    automations: [
      {
        id: "morning",
        name: "Morning",
        enabled: true,
        trigger: "weekly|31|07:00|0",
        actions: ["scene|evening"],
      },
      {
        id: "welcome-home",
        name: "Welcome home",
        enabled: true,
        trigger: "manual",
        actions: ["notify|Welcome home"],
      },
    ],
    savedWifiNetworks: [
      { ssid: "Lab_WiFi", connected: true },
      { ssid: "Old_Network", connected: false },
    ],
    wifiNetworks: [
      {
        id: "lab-wifi",
        ssid: "Lab_WiFi",
        bssid: "aa:bb:cc:dd:ee:ff",
        signal_dbm: -45,
        channel: 6,
        auth: "WPA2",
        secured: true,
      },
      {
        id: "mesh-iot",
        ssid: "Mesh_IoT",
        bssid: "11:22:33:44:55:66",
        signal_dbm: -61,
        channel: 11,
        auth: "WPA2",
        secured: true,
      },
    ],
    aiMemory: {
      encrypted: true,
      detail: "AI memory is encrypted on SD.",
      sections: [
        { id: "home", title: "Home facts", markdown: "Known safe devices." },
        {
          id: "prefs",
          title: "Preferences",
          markdown: "Prefer local actions.",
        },
      ],
    },
    aiPrompts: {
      encrypted: true,
      detail: "Prompt files are encrypted on SD after edits.",
      files: [
        {
          path: "assistant/system.md",
          title: "System prompt",
          markdown: "# System\n- Keep actions bounded.",
          baseline: true,
        },
      ],
    },
    mcpTools: {
      tools: [
        {
          id: "devices.list",
          name: "devices.list",
          title: "List devices",
          description: "Read known device state.",
          category: "Devices",
          enabled: true,
          available: true,
          destructive: false,
        },
        {
          id: "ota.apply",
          name: "ota.apply",
          title: "Apply OTA",
          description: "Modeled destructive OTA action.",
          category: "System",
          enabled: false,
          available: false,
          destructive: true,
        },
      ],
    },
    mcpContext: {
      detail: "MCP context available.",
      tools_enabled: 1,
      resources: [],
    },
    posts: [] as { path: string; body: Json }[],
  };
}

async function json(route: Route, body: unknown, status = 200) {
  await route.fulfill({
    status,
    contentType: "application/json",
    body: JSON.stringify(body),
  });
}

function slug(value: string) {
  return (
    value
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-|-$/g, "") || "item"
  );
}

function responseForCollection(state: FirmwareState, path: string) {
  if (path === "/api/v1/devices")
    return { devices: state.devices, detail: "Devices loaded." };
  if (path === "/api/v1/rooms")
    return { rooms: state.rooms, detail: "Rooms loaded." };
  if (path === "/api/v1/groups")
    return { groups: state.groups, detail: "Groups loaded." };
  if (path === "/api/v1/scenes")
    return { scenes: state.scenes, detail: "Scenes loaded." };
  if (path === "/api/v1/automations") {
    return { automations: state.automations, detail: "Automations loaded." };
  }
  return null;
}

async function handleMutation(
  route: Route,
  state: FirmwareState,
  path: string,
  body: Json,
  options: MockFirmwareOptions = {},
) {
  state.posts.push({ path, body });

  if (path === "/api/v1/activity/page") {
    const before =
      typeof body.before_sequence === "number" ? body.before_sequence : null;
    const limit = typeof body.limit === "number" ? body.limit : 20;
    const events = state.activity
      .filter((event) => before === null || event.sequence < before)
      .slice(0, Math.min(limit, options.activityPageSize ?? 3));
    const last = events.at(-1);
    return json(route, {
      events,
      next_before_sequence:
        last && state.activity.some((event) => event.sequence < last.sequence)
          ? last.sequence
          : null,
      retained: state.activity.length,
      dropped_events: 0,
      detail: "Newest events first.",
    });
  }

  if (path === "/api/v1/rooms/upsert") {
    const id = String(body.id ?? slug(String(body.name)));
    state.rooms = [
      ...state.rooms.filter((item) => item.id !== id),
      { id, name: String(body.name).trim() },
    ];
    return json(route, {
      accepted: true,
      detail: "Room saved.",
      room: state.rooms.at(-1),
    });
  }
  if (path === "/api/v1/rooms/delete") {
    state.rooms = state.rooms.filter((item) => item.id !== body.id);
    return json(route, { accepted: true, detail: "Room deleted." });
  }
  if (path === "/api/v1/groups/upsert") {
    const id = String(body.id ?? slug(String(body.name)));
    const group = {
      id,
      name: String(body.name).trim(),
      device_ids: Array.isArray(body.device_ids)
        ? body.device_ids.map(String)
        : [],
    };
    state.groups = [...state.groups.filter((item) => item.id !== id), group];
    return json(route, { accepted: true, detail: "Group saved.", group });
  }
  if (path === "/api/v1/groups/delete") {
    state.groups = state.groups.filter((item) => item.id !== body.id);
    return json(route, { accepted: true, detail: "Group deleted." });
  }
  if (path === "/api/v1/scenes/upsert") {
    const id = String(body.id ?? slug(String(body.name)));
    const scene = {
      id,
      name: String(body.name).trim(),
      actions: Array.isArray(body.actions) ? body.actions.map(String) : [],
    };
    state.scenes = [...state.scenes.filter((item) => item.id !== id), scene];
    return json(route, { accepted: true, detail: "Scene saved.", scene });
  }
  if (path === "/api/v1/scenes/delete") {
    const referenced = state.automations.some((automation) =>
      automation.actions.includes(`scene|${String(body.id)}`),
    );
    if (referenced)
      return json(route, {
        accepted: false,
        status: "in_use",
        detail: "Scene is referenced by an automation and cannot be deleted.",
      });
    state.scenes = state.scenes.filter((item) => item.id !== body.id);
    return json(route, { accepted: true, detail: "Scene deleted." });
  }
  if (path === "/api/v1/automations/upsert") {
    const id = String(body.id ?? slug(String(body.name)));
    const automation = {
      id,
      name: String(body.name).trim(),
      enabled: Boolean(body.enabled),
      trigger: String(body.trigger ?? "manual"),
      actions: Array.isArray(body.actions) ? body.actions.map(String) : [],
    };
    state.automations = [
      ...state.automations.filter((item) => item.id !== id),
      automation,
    ];
    return json(route, {
      accepted: true,
      detail: "Automation saved.",
      automation,
    });
  }
  if (path === "/api/v1/automations/run") {
    return json(route, {
      accepted: true,
      status: "queued",
      detail: "Automation queued for bounded local evaluation",
      operation_id: 101,
    });
  }
  if (path === "/api/v1/automations/operations/status") {
    return json(route, {
      operation_id: Number(body.operation_id),
      state: "complete",
      action_index: 1,
      action_count: 1,
      failure: null,
      error: null,
    });
  }
  if (path === "/api/v1/automations/delete") {
    state.automations = state.automations.filter((item) => item.id !== body.id);
    return json(route, { accepted: true, detail: "Automation deleted." });
  }
  if (path === "/api/v1/network/wifi/forget") {
    const ssid = String(body.ssid ?? "");
    state.savedWifiNetworks = state.savedWifiNetworks.filter(
      (network) => network.ssid !== ssid,
    );
    return json(route, { accepted: true, detail: "Saved network forgotten." });
  }
  if (path === "/api/v1/network/wifi") {
    state.network.station_ssid = String((body.credentials as Json).ssid);
    state.network.station_bssid = "";
    return json(route, { accepted: true, detail: "Wi‑Fi settings saved." });
  }
  if (path === "/api/v1/time") {
    state.time.ntp_server_id = String(body.ntp_server_id);
    state.time.timezone_id = String(body.timezone_id);
    return json(route, { accepted: true, detail: "Time settings saved." });
  }
  if (path === "/api/v1/matter/thread/dataset") {
    state.matter.thread_dataset_configured = true;
    return json(route, {
      accepted: true,
      status: "applying",
      detail:
        "The encrypted Thread dataset was saved and queued for the OpenThread host.",
    });
  }
  if (path === "/api/v1/matter/commission") {
    return json(route, {
      accepted: true,
      detail: `${body.transport} pairing request received.`,
      manual_code: String(body.setup_payload),
    });
  }
  if (path === "/api/v1/ai/provider") {
    state.ai.provider_configured = true;
    return json(route, { accepted: true, detail: "AI provider encrypted." });
  }
  if (path === "/api/v1/ai/chat") {
    return json(route, {
      accepted: true,
      operation_id: "1",
      status: "complete",
      reply: "Core Pilot status looks healthy.",
      actions: [],
    });
  }
  if (path === "/api/v1/ai/memory/section") {
    state.aiMemory.sections = state.aiMemory.sections.map((section) =>
      section.id === body.id
        ? { ...section, markdown: String(body.markdown) }
        : section,
    );
    return json(route, { accepted: true, detail: "Memory saved." });
  }
  if (path === "/api/v1/ai/prompts/file") {
    return json(route, { accepted: true, detail: "Prompt file saved." });
  }
  if (path === "/api/v1/ai/mcp/tool") {
    state.mcpTools.tools = state.mcpTools.tools.map((tool) =>
      tool.name === body.tool
        ? { ...tool, enabled: Boolean(body.enabled) }
        : tool,
    );
    return json(route, { accepted: true, detail: "Tool permission saved." });
  }
  if (path === "/api/v1/ai/mcp/invoke") {
    return json(route, { accepted: true, detail: "Tool invoked." });
  }
  if (path === "/api/v1/network/wifi/scan") {
    if (options.pagedWifiScan) {
      state.posts.push({ path: "__paged_wifi_scan__", body: {} });
      return json(route, {
        networks: [],
        scanning: false,
        scan_state: "idle",
        detail: "Scan complete; loading pages.",
      });
    }
    return json(route, {
      networks: [],
      scanning: true,
      scan_state: "scanning",
      detail: "Scanning nearby networks.",
    });
  }
  if (path === "/api/v1/network/wifi/scan/page") {
    if (state.posts.some((post) => post.path === "__paged_wifi_scan__")) {
      const lastSeen =
        typeof body.last_seen === "string" ? body.last_seen : null;
      const firstPage = state.wifiNetworks.slice(0, 1);
      const secondPage = state.wifiNetworks.slice(1);
      return json(route, {
        networks: lastSeen ? secondPage : firstPage,
        scanning: false,
        scan_state: "idle",
        next_last_seen: lastSeen ? null : firstPage.at(-1)?.id,
        detail: lastSeen
          ? "2 nearby networks."
          : "Loading more nearby networks…",
      });
    }
    return json(route, {
      networks: state.wifiNetworks,
      scanning: false,
      scan_state: "idle",
      next_last_seen: null,
      detail: "2 nearby networks.",
    });
  }
  if (path === "/api/v1/ota/prepare") {
    state.system.ota = "staging encrypted chunks";
    state.system.ota_phase = "prepared";
    state.system.ota_total_bytes = Number(body.size_bytes ?? 0);
    return json(route, {
      accepted: true,
      detail: "OTA metadata accepted.",
      upload_token: "a".repeat(64),
    });
  }
  if (path === "/api/v1/ota/chunk") {
    state.system.ota_progress_bytes = Number(body.offset ?? 0) + 1;
    if (body.final_chunk) state.system.ota_phase = "activated";
    return json(route, { accepted: true, detail: "OTA chunk staged." });
  }

  return json(route, { accepted: true, detail: "Mutation accepted." });
}

type MockFirmwareOptions = {
  pagedWifiScan?: boolean;
  activityPageSize?: number;
};

async function mockFirmwareApi(page: Page, options: MockFirmwareOptions = {}) {
  const state = createFirmwareState();
  await page.route("**/api/v1/**", async (route) => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    const method = request.method();

    if (path === "/api/v1/auth")
      return json(route, { authentication_required: false });

    if (method === "POST") {
      const body = request.postDataJSON() as Json;
      return handleMutation(route, state, path, body ?? {}, options);
    }

    const collection = responseForCollection(state, path);
    if (collection) return json(route, collection);

    if (path === "/api/v1/health") return json(route, state.health);
    if (path === "/api/v1/network") return json(route, state.network);
    if (path === "/api/v1/network/wifi/saved")
      return json(route, {
        networks: state.savedWifiNetworks,
        detail: `${state.savedWifiNetworks.length} saved networks.`,
      });
    if (path === "/api/v1/system") return json(route, state.system);
    if (path === "/api/v1/time") return json(route, state.time);
    if (path === "/api/v1/storage") return json(route, state.storage);
    if (path === "/api/v1/matter") return json(route, state.matter);
    if (path === "/api/v1/matter/operations")
      return json(route, state.matterOperations);
    if (path === "/api/v1/ai") return json(route, state.ai);
    if (path === "/api/v1/ai/memory") return json(route, state.aiMemory);
    if (path === "/api/v1/ai/prompts") return json(route, state.aiPrompts);
    if (path === "/api/v1/ai/mcp/tools") return json(route, state.mcpTools);
    if (path === "/api/v1/ai/mcp/context") return json(route, state.mcpContext);
    if (path === "/api/v1/network/wifi/scan") {
      return json(route, {
        networks: state.wifiNetworks,
        scanning: false,
        scan_state: "idle",
        detail: "2 nearby networks.",
      });
    }

    return json(route, { detail: `Unhandled ${path}` }, 404);
  });
  return state;
}

async function expectAccessible(page: Page) {
  // Let mocked query responses and portal transitions settle before scanning.
  await page.waitForLoadState("networkidle");
  const results = await new AxeBuilder({ page })
    .withTags(["wcag2a", "wcag2aa", "wcag21a", "wcag21aa"])
    .analyze();
  expect(
    results.violations,
    JSON.stringify(
      results.violations.map(({ id, impact, help, nodes }) => ({
        rule: id,
        impact,
        help,
        nodes: nodes.map(({ html, target, failureSummary }) => ({
          html,
          target,
          failureSummary,
        })),
      })),
      null,
      2,
    ),
  ).toEqual([]);
}

async function expectHeading(page: Page, name: string) {
  await expect(page.getByRole("heading", { name, exact: true })).toBeVisible();
}

// Integration positive/negative: crawls every SPA feature route with mocked firmware
// data and verifies live values replace fallback placeholders.
test("routes every dashboard feature page and renders live firmware status", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/");

  await expectHeading(page, "Home");
  await expect(
    page.getByText("Lab_WiFi", { exact: false }).first(),
  ).toBeVisible();
  await expect(page.getByText("192.168.29.2").first()).toBeVisible();
  await expect(page.getByText("CorePilot-Setup-XXXX")).toHaveCount(0);
  await expect(page.getByText("Battery", { exact: true })).toHaveCount(0);

  const routes = [
    ["/#/activity", "Activity"],
    ["/#/devices", "Devices"],
    ["/#/devices/add", "Add Matter Device"],
    ["/#/devices/configure", "Device Setup"],
    ["/#/rooms", "Rooms"],
    ["/#/groups", "Groups"],
    ["/#/scenes", "Scenes"],
    ["/#/automations", "Automations"],
    ["/#/ai", "Optional Assistant"],
    ["/#/ai/tools", "Assistant Permissions"],
    ["/#/ai/prompts", "Assistant Prompts"],
    ["/#/system", "System"],
  ] as const;

  for (const [path, heading] of routes) {
    await page.goto(path);
    await expectHeading(page, heading);
  }
});

test("exposes connection status and logs out visibly", async ({
  page,
  context,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/");
  await expectHeading(page, "Home");

  const logout = page.getByRole("button", { name: "Log out" });
  await expect(logout).toBeVisible();
  await expect(page.getByRole("status", { name: "Online" })).toBeVisible();

  await context.setOffline(true);
  await expect(page.getByRole("status", { name: "Offline" })).toBeVisible();
  await context.setOffline(false);
  await expect(page.getByRole("status", { name: "Online" })).toBeVisible();

  await logout.click();
  await expect(
    page.getByRole("heading", { name: "Unlock your dashboard" }),
  ).toBeVisible();
});

test("shows Home card skeletons until its APIs resolve", async ({ page }) => {
  await mockFirmwareApi(page);
  let release!: () => void;
  const ready = new Promise<void>((resolve) => {
    release = resolve;
  });
  const heldPaths = new Set([
    "health",
    "system",
    "network",
    "storage",
    "devices",
    "rooms",
    "scenes",
    "automations",
  ]);
  await page.route("**/api/v1/**", async (route) => {
    if (
      heldPaths.has(new URL(route.request().url()).pathname.split("/").at(-1)!)
    )
      await ready;
    await route.fallback();
  });

  await page.goto("/");
  try {
    await expect(
      page.getByRole("button", { name: "Edit favorites" }),
    ).toBeDisabled();
    for (const [label, count] of [
      ["home status", 15],
      ["quick actions", 4],
      ["favorites", 4],
      ["rooms", 6],
      ["device availability", 4],
    ] as const) {
      const skeleton = page.locator(`[data-home-card-skeleton="${label}"]`);
      await expect(skeleton).toBeVisible();
      await expect(skeleton.locator("p").first()).toHaveText(
        `Loading ${label}…`,
      );
      await expect(skeleton.locator("p").first()).toBeVisible();
      await expect(skeleton).toHaveAttribute("aria-busy", "true");
      const bars = skeleton.locator('[aria-hidden="true"] [data-visible]');
      await expect(bars).toHaveCount(count);
      expect(
        await bars.evaluateAll((elements) =>
          elements.every((element) => {
            const box = element.getBoundingClientRect();
            return box.width > 0 && box.height > 0;
          }),
        ),
        `${label} placeholders must have actual geometry`,
      ).toBe(true);
    }
    const tiles = page.locator("[data-home-status-skeleton]");
    await expect(tiles).toHaveCount(4);
    for (const tile of await tiles.all()) {
      const bars = await tile
        .locator("[data-visible]")
        .evaluateAll((elements) =>
          elements.map((element) => {
            const { width, height } = element.getBoundingClientRect();
            return { width, height };
          }),
        );
      expect(bars.map((bar) => bar.height)).toEqual([12, 28, 20]);
      expect(bars[1].width).toBe(28);
      expect(bars[2].width).toBeGreaterThan(bars[0].width);
    }
    await expect(page.getByText("Lab_WiFi", { exact: false })).toHaveCount(0);
    await expect(page.getByText("Clear", { exact: true })).toHaveCount(0);
    await expect(page.getByText(/No active network, storage/)).toHaveCount(0);
  } finally {
    release();
  }
  await expect(
    page.getByText("Lab_WiFi", { exact: false }).first(),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Welcome home" }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Pin favorites" }),
  ).toBeVisible();
  await expect(page.getByText("1 reported devices")).toBeVisible();
  await expect(page.locator("[data-home-card-skeleton]")).toHaveCount(0);
});

test("shows a Home card error instead of fallback status data", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.route("**/api/v1/health", (route) =>
    route.fulfill({
      status: 400,
      contentType: "application/json",
      body: JSON.stringify({ detail: "Health unavailable" }),
    }),
  );

  await page.goto("/");
  await expect(page.getByRole("alert")).toContainText(
    "Could not load home status",
  );
  await expect(page.getByText("Lab_WiFi", { exact: false })).toHaveCount(0);
  await expect(page.getByText("Clear", { exact: true })).toHaveCount(0);
  await expect(
    page.getByRole("heading", { name: "Alerts", exact: true }),
  ).toHaveCount(0);
  await expect(
    page.locator('[data-home-card-skeleton="home status"]'),
  ).toHaveCount(0);
  await expect(
    page.getByRole("button", { name: "Welcome home" }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Pin favorites" }),
  ).toBeVisible();
  await expect(page.getByText("1 reported devices")).toBeVisible();
});

for (const width of [320, 390, 768, 1024, 1440]) {
  test(`keeps Home card geometry bounded at ${width}px with long names`, async ({
    page,
  }) => {
    await page.emulateMedia({ reducedMotion: "reduce" });
    await page.setViewportSize({ width, height: 1000 });
    const state = await mockFirmwareApi(page);
    const automationName = "A".repeat(64);
    const roomName = "R".repeat(64);
    const deviceName = "D".repeat(64);
    state.automations[0].name = automationName;
    state.automations[0].trigger = "manual";
    state.automations.push({
      ...state.automations[0],
      id: "second-manual",
      name: "Second manual action",
    });
    state.rooms[0].name = roomName;
    state.rooms.push({ id: "second-room", name: "Second room" });
    state.devices[0].name = deviceName;
    state.devices[0].availability = "offline";
    state.scenes[0].name = "S".repeat(64);
    await page.goto("/");
    await page.getByRole("button", { name: "Pin favorites" }).click();
    await page
      .getByRole("button", { name: `${deviceName} · device`, exact: true })
      .click();
    await page
      .getByRole("button", {
        name: `${state.scenes[0].name} · scene`,
        exact: true,
      })
      .click();
    // Wait for real content in every affected card before measuring layout.
    await expect(
      page.getByRole("button", { name: automationName, exact: true }),
    ).toBeVisible();
    await expect(
      page.getByRole("button", { name: `${deviceName} · device`, exact: true }),
    ).toBeVisible();
    await expect(
      page.locator("p[title]").filter({ hasText: roomName }),
    ).toBeVisible();
    await expect(
      page.locator("li span[title]").filter({ hasText: deviceName }),
    ).toBeVisible();
    await expect(
      page.getByText("Lab_WiFi", { exact: false }).first(),
    ).toBeVisible();
    await expect(page.locator("[data-home-card-skeleton]")).toHaveCount(0);

    for (const favoriteMode of ["editor", "saved"] as const) {
      if (favoriteMode === "saved") {
        await page.getByRole("button", { name: "Done", exact: true }).click();
        await expect(
          page
            .getByRole("region", { name: "Saved favorites" })
            .getByRole("link"),
        ).toHaveCount(2);
      }
      for (const sidebarState of ["expanded", "collapsed"] as const) {
        if (width >= 768) {
          const sidebar = page.locator("[data-state][data-collapsible]");
          if ((await sidebar.getAttribute("data-state")) !== sidebarState)
            await page.locator('[data-sidebar="trigger"]').click();
          await expect(sidebar).toHaveAttribute("data-state", sidebarState);
        } else if (sidebarState === "expanded") {
          await page.getByRole("button", { name: "Toggle navigation" }).click();
          await expect(
            page.locator('[data-slot="sheet-content"]'),
          ).toBeVisible();
          await page.getByRole("button", { name: "Close navigation" }).click();
          await expect(
            page.locator('[data-slot="sheet-content"]'),
          ).toBeHidden();
        }

        await expect
          .poll(
            () =>
              page.locator("#main-content").evaluate((main) => {
                const failures: string[] = [];
                const contentWidth = (element: Element) => {
                  const style = getComputedStyle(element);
                  return (
                    element.clientWidth -
                    parseFloat(style.paddingLeft) -
                    parseFloat(style.paddingRight)
                  );
                };
                for (const card of main.querySelectorAll(
                  '[class~="@container/card"]',
                )) {
                  const grid = card.querySelector('[class*="grid-cols-"]');
                  if (!grid) {
                    if (!card.querySelector("#home-favorites-panel"))
                      failures.push("missing card grid");
                    continue;
                  }
                  const columns =
                    getComputedStyle(grid).gridTemplateColumns.split(
                      " ",
                    ).length;
                  const expected = contentWidth(card) >= 416 ? 2 : 1;
                  if (columns !== expected)
                    failures.push(
                      `card ${contentWidth(card)}px: ${columns} columns, expected ${expected}`,
                    );
                }
                for (const room of main.querySelectorAll(
                  '[class~="@container/room"]',
                )) {
                  const row = room.firstElementChild!;
                  const expected = contentWidth(room) < 288 ? "column" : "row";
                  if (getComputedStyle(row).flexDirection !== expected)
                    failures.push(`room badge should use ${expected}`);
                  const text = row.firstElementChild!.getBoundingClientRect();
                  const badge = row.lastElementChild!.getBoundingClientRect();
                  if (expected === "column" && badge.top < text.bottom)
                    failures.push("room badge overlaps text");
                }
                for (const element of main.querySelectorAll(
                  'button, a, li, [class~="@container/card"], [class~="@container/room"]',
                )) {
                  const box = element.getBoundingClientRect();
                  if (element.scrollWidth > element.clientWidth + 1)
                    failures.push(`${element.tagName} local overflow`);
                  if (
                    box.left < -1 ||
                    box.right > document.documentElement.clientWidth + 1
                  )
                    failures.push(`${element.tagName} outside viewport`);
                }
                if (
                  document.documentElement.scrollWidth >
                  document.documentElement.clientWidth
                )
                  failures.push("page overflow");
                return failures;
              }),
            { message: `${width}px, ${sidebarState} sidebar` },
          )
          .toEqual([]);
      }
    }
  });
}

test("provides a bounded operational Home dashboard", async ({ page }) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/");

  await expectHeading(page, "Home");
  await expect(
    page.getByRole("heading", { name: "Home status", exact: true }),
  ).toBeVisible();
  await expect(page.getByRole("heading", { name: "Alerts" })).toBeVisible();
  await expect(page.getByRole("heading", { name: "Hub health" })).toHaveCount(
    0,
  );
  await expect(
    page.getByText(/No active network, storage, device-state, or OTA warnings/),
  ).toBeVisible();
  await expect(page.getByRole("list", { name: "Active alerts" })).toHaveCount(
    0,
  );
  await expect(page.getByText("Clear", { exact: true })).toHaveCount(0);
  await expect(page.getByText("1 reported devices")).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Pin favorites" }),
  ).toBeVisible();
  await expect(page.locator("button[aria-pressed]")).toHaveCount(0);
  const main = page.locator("#main-content");
  await expect(main.locator("header").first()).toContainText("Home");
  await expect(
    main.locator("header").first().locator('[data-slot="card"]'),
  ).toHaveCount(0);
  const titles = await main.getByRole("heading").allTextContents();
  expect(titles.indexOf("Favorites")).toBeLessThan(
    titles.indexOf("Quick actions"),
  );
  await page.getByRole("button", { name: "Welcome home" }).click();
  await expect
    .poll(() => state.posts.at(-1)?.path)
    .toBe("/api/v1/automations/operations/status");
  expect(
    state.posts.some((post) => post.path === "/api/v1/automations/run"),
  ).toBe(true);
  await expect(
    page.getByRole("link", { name: "View activity history" }),
  ).toBeVisible();
});

test("Home favorites persist editor pin/remove and navigate without mutations", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/");
  const toggle = page.getByRole("button", {
    name: "Edit favorites",
    exact: true,
  });
  await expect(toggle).toHaveAttribute("aria-expanded", "false");
  await expect(toggle).toHaveAttribute("aria-controls", "home-favorites-panel");
  await page.getByRole("button", { name: "Pin favorites" }).click();
  const editor = page.getByRole("region", {
    name: "Edit favorites",
    exact: true,
  });
  await expect(editor).toBeFocused();
  await expect(
    page.getByRole("button", { name: "Done", exact: true }),
  ).toHaveAttribute("aria-expanded", "true");
  for (const name of ["Bench Lamp · device", "Evening · scene"]) {
    await editor.getByRole("button", { name, exact: true }).click();
    await expect(
      editor.getByRole("button", { name, exact: true }),
    ).toHaveAttribute("aria-pressed", "true");
  }
  await expect
    .poll(() =>
      page.evaluate(() => localStorage.getItem("core-pilot-home-favorites")),
    )
    .toBe(JSON.stringify(["device:matter-light-1", "scene:evening"]));
  await page.getByRole("button", { name: "Done", exact: true }).click();
  await expect(toggle).toBeFocused();
  await expect(page.locator("button[aria-pressed]")).toHaveCount(0);
  await page.reload();
  for (const [name, href, heading] of [
    ["Bench Lamp", "#/devices", "Devices"],
    ["Evening", "#/scenes", "Scenes"],
  ]) {
    const link = page
      .getByRole("region", { name: "Saved favorites" })
      .getByRole("link", { name: new RegExp(name) });
    await expect(link).toHaveAttribute("href", href);
    await link.click();
    await expectHeading(page, heading);
    await page.goto("/");
  }
  await toggle.click();
  await editor
    .getByRole("button", { name: "Bench Lamp · device", exact: true })
    .click();
  await expect(
    editor.getByRole("button", { name: "Bench Lamp · device", exact: true }),
  ).toHaveAttribute("aria-pressed", "false");
  await page.reload();
  await expect(page.locator("[data-home-favorite]")).toHaveCount(1);
  await expect(
    page.locator('[data-home-favorite="scene:evening"]'),
  ).toBeVisible();
  expect(state.posts).toEqual([]);
});

test("Home favorites limit includes missing items and permits removal in both views", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.addInitScript(() =>
    localStorage.setItem(
      "core-pilot-home-favorites",
      JSON.stringify([
        "device:matter-light-1",
        ...Array.from({ length: 7 }, (_, i) => `scene:missing-${i}`),
      ]),
    ),
  );
  await page.goto("/");
  await expect(page.locator("[data-home-favorite]")).toHaveCount(8);
  await page
    .getByRole("button", { name: "Edit favorites", exact: true })
    .click();
  await expect(
    page.getByText("8 of 8 selected.", { exact: false }),
  ).toBeVisible();
  const scene = page.getByRole("button", {
    name: "Evening · scene",
    exact: true,
  });
  await expect(scene).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Bench Lamp · device", exact: true }),
  ).toBeEnabled();
  await page
    .getByRole("button", {
      name: "Remove unavailable favorite: scene:missing-0",
      exact: true,
    })
    .click();
  await expect(scene).toBeEnabled();
  await scene.click();
  await expect(
    page.getByText("8 of 8 selected.", { exact: false }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Done", exact: true }).click();
  await page
    .getByRole("button", {
      name: "Remove unavailable favorite scene:missing-1",
      exact: true,
    })
    .click();
  await expect(page.locator("[data-home-favorite]")).toHaveCount(7);
  const saved = await page.evaluate(() =>
    JSON.parse(localStorage.getItem("core-pilot-home-favorites")!),
  );
  expect(saved).toHaveLength(7);
  expect(saved).toContain("scene:evening");
  expect(saved).not.toContain("scene:missing-1");
  expect(state.posts).toEqual([]);
});

for (const [label, stored, count, feedback] of [
  ["malformed JSON", "{", 0, "Saved favorites could not be read"],
  [
    "non-array",
    '{"device":"matter-light-1"}',
    0,
    "Saved favorites could not be read",
  ],
  [
    "invalid and duplicate entries",
    JSON.stringify([
      null,
      1,
      {},
      "device:",
      "group:no",
      "scene:bad id",
      "device:matter-light-1",
      "device:matter-light-1",
      "scene:evening",
    ]),
    2,
    "Invalid or duplicate favorites were ignored",
  ],
] as const) {
  test(`Home favorites safely ignore ${label}`, async ({ page }) => {
    await mockFirmwareApi(page);
    await page.addInitScript(
      (value) => localStorage.setItem("core-pilot-home-favorites", value),
      stored,
    );
    await page.goto("/");
    await expect(
      page.getByRole("region", { name: "Saved favorites" }),
    ).toBeVisible();
    await expect(page.locator("[data-home-favorite]")).toHaveCount(count);
    await expect(
      page.getByRole("status").filter({ hasText: feedback }),
    ).toBeVisible();
    expect(
      await page.evaluate(() =>
        localStorage.getItem("core-pilot-home-favorites"),
      ),
    ).toBe(stored);
    await page
      .getByRole("button", { name: "Edit favorites", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Bench Lamp · device", exact: true })
      .click();
    expect(
      await page.evaluate(() =>
        JSON.parse(localStorage.getItem("core-pilot-home-favorites")!),
      ),
    ).toEqual(count ? ["scene:evening"] : ["device:matter-light-1"]);
  });
}

test("Home favorites bound oversized saved lists to eight unique items", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.addInitScript(() =>
    localStorage.setItem(
      "core-pilot-home-favorites",
      JSON.stringify(
        Array.from({ length: 12 }, (_, i) => `scene:missing-${i}`),
      ),
    ),
  );
  await page.goto("/");
  await expect(page.locator("[data-home-favorite]")).toHaveCount(8);
  await expect(
    page.locator('[data-home-favorite="scene:missing-8"]'),
  ).toHaveCount(0);
  await expect(
    page
      .getByRole("status")
      .filter({ hasText: "Invalid or duplicate favorites were ignored" }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Edit favorites", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Bench Lamp · device", exact: true }),
  ).toBeDisabled();
  await page
    .getByRole("button", {
      name: "Remove unavailable favorite: scene:missing-0",
      exact: true,
    })
    .click();
  expect(
    await page.evaluate(() =>
      JSON.parse(localStorage.getItem("core-pilot-home-favorites")!),
    ),
  ).toEqual(Array.from({ length: 7 }, (_, i) => `scene:missing-${i + 1}`));
});

for (const failRead of [false, true]) {
  test(`Home favorites storage failure is truthful with read failure ${failRead}`, async ({
    page,
  }) => {
    await mockFirmwareApi(page);
    await page.addInitScript((read) => {
      if (read)
        Storage.prototype.getItem = () => {
          throw new Error("Unavailable");
        };
      Storage.prototype.setItem = () => {
        throw new Error("Unavailable");
      };
    }, failRead);
    await page.goto("/");
    if (failRead)
      await expect(
        page
          .getByRole("status")
          .filter({ hasText: "Saved favorites could not be read" }),
      ).toBeVisible();
    await page.getByRole("button", { name: "Pin favorites" }).click();
    await page
      .getByRole("button", { name: "Bench Lamp · device", exact: true })
      .click();
    await expect(
      page
        .getByRole("status")
        .filter({ hasText: "Favorites changed for this visit only" }),
    ).toBeVisible();
    await expect(
      page.getByText("Favorites saved in this browser.", { exact: true }),
    ).toHaveCount(0);
    await page.getByRole("button", { name: "Done", exact: true }).click();
    await expect(page.locator("[data-home-favorite]")).toHaveCount(1);
    await page.reload();
    await expect(
      page.getByRole("button", { name: "Pin favorites" }),
    ).toBeVisible();
  });
}

for (const endpoint of ["devices", "scenes"]) {
  test(`Home favorites disable editing after ${endpoint} error`, async ({
    page,
  }) => {
    await mockFirmwareApi(page);
    await page.route(`**/api/v1/${endpoint}`, (route) =>
      route.fulfill({
        status: 400,
        contentType: "application/json",
        body: JSON.stringify({ detail: "Inventory unavailable" }),
      }),
    );
    await page.goto("/");
    await expect(
      page.getByRole("alert").filter({ hasText: "Could not load favorites" }),
    ).toBeVisible();
    await expect(
      page.getByRole("button", { name: "Edit favorites" }),
    ).toBeDisabled();
    await expect(
      page.getByRole("button", { name: "Pin favorites" }),
    ).toHaveCount(0);
    await expect(page.locator("button[aria-pressed]")).toHaveCount(0);
  });
}

for (const mode of ["saved", "editor"] as const) {
  for (const last of [false, true]) {
    test(`Home unavailable favorite keyboard removal keeps focus stable in ${mode}, last ${last}`, async ({
      page,
    }) => {
      const state = await mockFirmwareApi(page);
      await page.addInitScript(
        (onlyMissing) =>
          localStorage.setItem(
            "core-pilot-home-favorites",
            JSON.stringify(
              onlyMissing
                ? ["scene:missing"]
                : ["scene:missing", "device:matter-light-1"],
            ),
          ),
        last,
      );
      await page.goto("/");
      const panel = page.locator("#home-favorites-panel");
      await expect(panel).toBeVisible();
      if (mode === "editor") {
        const toggle = page.getByRole("button", {
          name: "Edit favorites",
          exact: true,
        });
        await toggle.focus();
        await page.keyboard.press("Enter");
        await expect(panel).toBeFocused();
      }
      const remove = panel.getByRole("button", {
        name:
          mode === "editor"
            ? "Remove unavailable favorite: scene:missing"
            : "Remove unavailable favorite scene:missing",
        exact: true,
      });
      await remove.focus();
      await expect(remove).toBeFocused();
      await page.keyboard.press("Enter");
      await expect(remove).toHaveCount(0);
      if (mode === "saved" && last) {
        await expect(
          panel.getByRole("button", { name: "Pin favorites", exact: true }),
        ).toBeFocused();
      } else {
        await expect(panel).toBeFocused();
      }
      await expect(
        page.getByRole("button", {
          name: mode === "editor" ? "Done" : "Edit favorites",
          exact: true,
        }),
      ).toHaveAttribute("aria-expanded", String(mode === "editor"));
      expect(
        await page.evaluate(() =>
          JSON.parse(localStorage.getItem("core-pilot-home-favorites")!),
        ),
      ).toEqual(last ? [] : ["device:matter-light-1"]);
      expect(state.posts).toEqual([]);
    });
  }
}

for (const endpoint of ["devices", "scenes"]) {
  for (const focusInside of [true, false]) {
    test(`Home ${endpoint} refetch error exits editor and preserves ${focusInside ? "panel" : "outside"} focus`, async ({
      page,
      context,
    }) => {
      const state = await mockFirmwareApi(page);
      await page.goto("/");
      await page
        .getByRole("button", { name: "Pin favorites", exact: true })
        .click();
      const panel = page.locator("#home-favorites-panel");
      const selection = panel.getByRole("button", {
        name: "Bench Lamp · device",
        exact: true,
      });
      await selection.click();
      let release!: () => void;
      const held = new Promise<void>((resolve) => {
        release = resolve;
      });
      const failInventory = async (route: Route) => {
        await held;
        await route.fulfill({
          status: 400,
          contentType: "application/json",
          body: JSON.stringify({ detail: "Inventory refresh unavailable" }),
        });
      };
      await page.route(`**/api/v1/${endpoint}`, failInventory);
      await context.setOffline(true);
      await expect(
        page.getByRole("status", { name: "Offline", exact: true }),
      ).toBeVisible();
      const requested = page.waitForRequest(
        (request) => new URL(request.url()).pathname === `/api/v1/${endpoint}`,
      );
      await context.setOffline(false);
      await requested;
      const outside = page
        .locator("#main-content footer")
        .getByRole("link", { name: "View activity history", exact: true });
      const focused = focusInside ? selection : outside;
      try {
        await focused.focus();
        await expect(focused).toBeFocused();
        await expect(
          page.getByRole("button", { name: "Done", exact: true }),
        ).toHaveAttribute("aria-expanded", "true");
      } finally {
        release();
      }
      await expect(panel.getByRole("alert")).toContainText(
        "Could not refresh favorites",
      );
      await expect(
        panel.getByRole("link", { name: /Bench Lamp/ }),
      ).toBeVisible();
      await expect(panel.locator("[data-home-card-skeleton]")).toHaveCount(0);
      await expect(
        panel.getByRole("button", { name: "Retry favorites" }),
      ).toBeEnabled();
      await expect(panel).toHaveAccessibleName("Saved favorites");
      const toggle = page.getByRole("button", {
        name: "Edit favorites",
        exact: true,
      });
      await expect(toggle).toHaveAttribute("aria-expanded", "false");
      await expect(toggle).toHaveAttribute(
        "aria-controls",
        "home-favorites-panel",
      );
      await expect(toggle).toBeDisabled();
      await expect(panel.locator("button[aria-pressed]")).toHaveCount(0);
      await expect(focusInside ? panel : outside).toBeFocused();
      await expect(
        page.getByRole("status", { name: "Online", exact: true }),
      ).toBeVisible();

      // Retry recovers inventory without remounting Home or stealing focus.
      await page.unroute(`**/api/v1/${endpoint}`, failInventory);
      await panel
        .getByRole("button", { name: "Retry favorites" })
        .evaluate((button: HTMLButtonElement) => button.click());
      await expect(toggle).toBeEnabled();
      await expect(panel.getByRole("alert")).toHaveCount(0);
      await expect(
        panel.getByRole("link", { name: /Bench Lamp/ }),
      ).toBeVisible();
      await expect(toggle).toHaveAttribute("aria-expanded", "false");
      await expect(focusInside ? panel : outside).toBeFocused();
      expect(state.posts).toEqual([]);
    });
  }
}

for (const endpoint of ["health", "devices"] as const) {
  test(`Home cached ${endpoint} refresh retains favorite identity and Retry recovers`, async ({
    page,
    context,
  }) => {
    const state = await mockFirmwareApi(page);
    await page.addInitScript(() =>
      localStorage.setItem(
        "core-pilot-home-favorites",
        JSON.stringify(["device:matter-light-1"]),
      ),
    );
    await page.goto("/");
    const panel = page.locator("#home-favorites-panel");
    const link = panel.getByRole("link", { name: /Bench Lamp/ });
    await expect(link).toBeVisible();
    await expect(page.getByText("Lab_WiFi", { exact: false })).toBeVisible();
    const original = await link.elementHandle();
    let release!: () => void;
    const held = new Promise<void>((resolve) => {
      release = resolve;
    });
    const fail = async (route: Route) => {
      await held;
      await json(route, { detail: "Refresh unavailable" }, 400);
    };
    await page.route(`**/api/v1/${endpoint}`, fail);
    await context.setOffline(true);
    await expect(
      page.getByRole("status", { name: "Offline", exact: true }),
    ).toBeVisible();
    const requested = page.waitForRequest(`**/api/v1/${endpoint}`);
    await context.setOffline(false);
    await requested;
    try {
      await link.focus();
      await expect(
        page.getByRole("status").filter({ hasText: "Refreshing home status" }),
      ).toBeVisible();
      await expect(page.locator("[data-home-card-skeleton]")).toHaveCount(0);
      await expect(link).toBeFocused();
      expect(
        await link.evaluate(
          (element, previous) => element === previous,
          original,
        ),
      ).toBe(true);
    } finally {
      release();
    }
    const warning = page
      .getByRole("alert")
      .filter({ hasText: "Could not refresh home status" });
    await expect(warning).toContainText("Showing previously loaded data");
    await expect(page.getByText("Lab_WiFi", { exact: false })).toBeVisible();
    await expect(link).toBeFocused();
    expect(
      await link.evaluate(
        (element, previous) => element === previous,
        original,
      ),
    ).toBe(true);
    if (endpoint === "devices")
      await expect(
        page.getByRole("button", { name: "Edit favorites" }),
      ).toBeDisabled();
    await page.unroute(`**/api/v1/${endpoint}`, fail);
    await warning
      .getByRole("button", { name: "Retry home status" })
      .evaluate((button: HTMLButtonElement) => button.click());
    await expect(
      page.getByRole("alert").filter({ hasText: "Could not refresh" }),
    ).toHaveCount(0);
    await expect(
      page.getByRole("button", { name: "Edit favorites" }),
    ).toBeEnabled();
    await expect(link).toBeFocused();
    expect(
      await link.evaluate(
        (element, previous) => element === previous,
        original,
      ),
    ).toBe(true);
    expect(state.posts).toEqual([]);
  });
}

for (const endpoint of ["devices", "scenes"]) {
  test(`Home empty favorites block both editor entry points after cached ${endpoint} failure`, async ({
    page,
    context,
  }) => {
    await mockFirmwareApi(page);
    await page.goto("/");
    const panel = page.locator("#home-favorites-panel");
    const pin = panel.getByRole("button", {
      name: "Pin favorites",
      exact: true,
    });
    const edit = page.getByRole("button", {
      name: "Edit favorites",
      exact: true,
    });
    await expect(pin).toBeEnabled();
    await page.route(`**/api/v1/${endpoint}`, (route) =>
      json(route, { detail: "Inventory refresh unavailable" }, 400),
    );
    await context.setOffline(true);
    await expect(
      page.getByRole("status", { name: "Offline", exact: true }),
    ).toBeVisible();
    await context.setOffline(false);
    await expect(panel.getByRole("alert")).toContainText(
      "Could not refresh favorites",
    );
    await expect(edit).toBeDisabled();
    await expect(pin).toBeDisabled();
    // Synthetic dispatch also exercises the mode guard, independently of native disabled clicks.
    await pin.dispatchEvent("click");
    await edit.dispatchEvent("click");
    await expect(panel).toHaveAccessibleName("Saved favorites");
    await expect(edit).toHaveAttribute("aria-expanded", "false");
    await expect(panel.locator("button[aria-pressed]")).toHaveCount(0);
    await expect(
      page.getByRole("button", { name: "Done", exact: true }),
    ).toHaveCount(0);
  });
}

for (const cached of [false, true]) {
  for (const result of ["success", "failure", "outside"] as const) {
    test(`Home keyboard Retry from ${cached ? "cached failure" : "initial error"} preserves ${result} focus`, async ({
      page,
      context,
    }) => {
      await mockFirmwareApi(page);
      const fail = (route: Route) =>
        json(route, { detail: "Health unavailable" }, 400);
      if (!cached) await page.route("**/api/v1/health", fail);
      await page.goto("/");
      if (cached) {
        await expect(
          page
            .getByRole("group", { name: "home status content", exact: true })
            .getByText("Lab_WiFi", { exact: false }),
        ).toBeVisible();
        await page.route("**/api/v1/health", fail);
        await context.setOffline(true);
        await expect(
          page.getByRole("status", { name: "Offline", exact: true }),
        ).toBeVisible();
        await context.setOffline(false);
      }
      const content = page.getByRole("group", {
        name: "home status content",
        exact: true,
      });
      await expect(content.getByRole("alert")).toContainText(
        cached ? "Could not refresh home status" : "Could not load home status",
      );
      await page.unroute("**/api/v1/health", fail);
      let release!: () => void;
      const held = new Promise<void>((resolve) => {
        release = resolve;
      });
      await page.route("**/api/v1/health", async (route) => {
        await held;
        if (result === "failure") return fail(route);
        return route.fallback();
      });
      const retry = content.getByRole("button", {
        name: "Retry home status",
        exact: true,
      });
      await retry.focus();
      await expect(retry).toBeFocused();
      const requested = page.waitForRequest("**/api/v1/health");
      await page.keyboard.press("Enter");
      await requested;
      const outside = page
        .locator("#main-content footer")
        .getByRole("link", { name: "View activity history", exact: true });
      try {
        if (cached) await expect(content.getByRole("button")).toBeDisabled();
        else
          await expect(
            content.locator('[data-home-card-skeleton="home status"]'),
          ).toBeVisible();
        if (result === "outside") {
          await outside.focus();
          await expect(outside).toBeFocused();
        }
      } finally {
        release();
      }
      if (result === "failure") {
        await expect(retry).toBeEnabled();
        await expect(cached ? retry : content).toBeFocused();
        await expect(content.getByRole("alert")).toBeVisible();
      } else {
        await expect(content.getByRole("alert")).toHaveCount(0);
        await expect(
          content.getByRole("button", { name: /Retry/ }),
        ).toHaveCount(0);
        await expect(result === "outside" ? outside : content).toBeFocused();
      }
    });
  }
}

for (const terminal of ["complete", "failed"] as const) {
  test(`Home local automation waits for terminal ${terminal}`, async ({
    page,
  }) => {
    const state = await mockFirmwareApi(page);
    let release!: () => void;
    const held = new Promise<void>((resolve) => {
      release = resolve;
    });
    let checks = 0;
    await page.route(
      "**/api/v1/automations/operations/status",
      async (route) => {
        expect(route.request().postDataJSON()).toEqual({ operation_id: 101 });
        checks += 1;
        if (checks === 1)
          return json(route, {
            operation_id: 101,
            state: "pending",
            action_index: 0,
            action_count: 1,
            failure: null,
            error: null,
          });
        await held;
        return json(route, {
          operation_id: 101,
          state: terminal,
          action_index: 1,
          action_count: 1,
          failure: terminal === "failed" ? "Local action failed." : null,
          error: null,
        });
      },
    );
    await page.goto("/");
    const run = page.getByRole("button", { name: "Welcome home", exact: true });
    await run.click();
    try {
      await expect(
        page.getByRole("region", { name: "Waiting for automation completion" }),
      ).toContainText("completion has not been confirmed");
      await expect(run).toBeDisabled();
      await expect(
        page.getByRole("region", { name: "Automation run complete" }),
      ).toHaveCount(0);
      await expect.poll(() => checks).toBeGreaterThanOrEqual(2);
    } finally {
      release();
    }
    await expect(
      page.getByRole("region", {
        name:
          terminal === "complete"
            ? "Automation run complete"
            : "Automation run failed",
      }),
    ).toContainText(
      terminal === "complete"
        ? "The automation completed successfully."
        : "Local action failed.",
    );
    await expect(run).toBeEnabled();
    expect(
      state.posts.filter((post) => post.path === "/api/v1/automations/run"),
    ).toHaveLength(1);
  });
}

test("Home alerts offer per-condition review links without misleading healthy states", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  state.network.station_ipv4 = null;
  state.storage.encrypted = false;
  state.storage.mutations_allowed = false;
  state.devices[0].availability = "stale";
  state.system.ota_phase = "failed";
  await page.goto("/");
  const alerts = page.getByRole("list", { name: "Active alerts" });
  await expect(alerts.getByRole("listitem")).toHaveCount(4);
  for (const [title, action, href] of [
    ["Home Wi-Fi is not connected", "Review network setup", "#/system"],
    ["Encrypted storage needs attention", "Review storage", "#/storage"],
    ["1 device states are stale or offline", "Review devices", "#/devices"],
    ["Firmware update: failed", "Review firmware update", "#/system"],
  ]) {
    const alert = alerts.getByRole("listitem").filter({ hasText: title });
    await expect(alert).toBeVisible();
    await expect(
      alert.getByRole("link", { name: action, exact: true }),
    ).toHaveAttribute("href", href);
  }
  for (const text of [
    /No active network, storage/,
    "Encrypted · writable",
    "No update in progress",
    "No offline or stale devices are currently reported.",
    "Clear",
  ])
    await expect(
      page.getByText(text, { exact: typeof text === "string" }),
    ).toHaveCount(0);
  await expect(
    page
      .locator("#main-content footer")
      .getByRole("link", { name: "View activity history" }),
  ).toHaveAttribute("href", "#/activity");
  expect(state.posts).toEqual([]);
});

// Navigation contract: sidebar clicks stay inside the SPA, update history/title,
// and move focus to routed content without recreating the document.
test("loads, filters, and infinitely paginates activity history", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  state.activity.push({
    sequence: 9,
    unix_seconds: null,
    uptime_seconds: 300,
    kind: "ota_prepared",
    detail: "OTA image prepared.",
  });
  await page.goto("/#/activity");

  await expectHeading(page, "Activity");
  await expect(
    page.getByText("Manual automation was queued for local evaluation."),
  ).toBeVisible();
  await page.getByRole("button", { name: "Severity" }).click();
  await page.getByRole("option", { name: "Error" }).click();
  await expect(
    page.getByRole("button", { name: /Severity Error/ }),
  ).toBeVisible();
  await expect(page.getByText("Wi-Fi join failed.")).toBeVisible();
  await expect(
    page.getByText("Manual automation was queued for local evaluation."),
  ).toHaveCount(0);
  await page.getByRole("button", { name: "Severity" }).click();
  await page.getByRole("option", { name: "All severities" }).click();
  await expect(page.getByText("OTA image prepared.")).toBeVisible();
  await expect(
    page.getByText(
      "Room and device filters are unavailable until activity events carry room or device IDs.",
    ),
  ).toBeVisible();
});

for (const trigger of ["automatic", "manual"] as const) {
  test(`activity pagination preserves scroll and viewport on ${trigger} append`, async ({
    page,
  }) => {
    const state = await mockFirmwareApi(page, { activityPageSize: 20 });
    state.activity = Array.from({ length: 65 }, (_, index) => ({
      sequence: 100 - index,
      unix_seconds: 1_700_000_000 - index,
      uptime_seconds: 500 - index,
      kind: "configuration_saved",
      detail: `History event ${index + 1}: ${"Room lighting configuration saved locally. ".repeat(1 + (index % 4))}`,
    }));
    const pending: Route[] = [];
    await page.route("**/api/v1/activity/page", async (route) => {
      if (route.request().postDataJSON().before_sequence == null) {
        await route.fallback();
        return;
      }
      pending.push(route);
    });
    await page.goto("/#/activity");
    const viewport = page.getByRole("region", {
      name: "Scrollable activity history",
    });
    await expect(page.locator("#activity-list-summary")).toContainText(
      "20 loaded",
    );
    const original = await viewport.elementHandle();
    expect(original).not.toBeNull();
    await expect
      .poll(() =>
        viewport
          .locator("li")
          .evaluateAll(
            (rows) =>
              new Set(rows.map((row) => row.getBoundingClientRect().height))
                .size,
          ),
      )
      .toBeGreaterThan(1);

    for (const loaded of [40, 60, 65]) {
      await viewport.evaluate((element, automatic) => {
        element.scrollTop = automatic ? element.scrollHeight : 200;
        element.dispatchEvent(new Event("scroll"));
      }, trigger === "automatic");
      await expect
        .poll(() => viewport.evaluate((element) => element.scrollTop))
        .toBeGreaterThan(0);
      if (trigger === "manual") {
        expect(pending).toHaveLength(0);
        await page
          .getByRole("button", { name: "Load older", exact: true })
          .click();
      }
      await expect.poll(() => pending.length).toBe(1);
      const before = await viewport.evaluate((element) => element.scrollTop);
      await expect(
        page.getByRole("button", { name: "Loading older…" }),
      ).toBeDisabled();
      expect(
        await viewport.evaluate(
          (element, saved) => element === saved,
          original,
        ),
      ).toBe(true);
      await pending.shift()!.fallback();
      await expect(page.locator("#activity-list-summary")).toContainText(
        `${loaded} loaded`,
      );
      expect(
        await viewport.evaluate(
          (element, saved) => element === saved,
          original,
        ),
      ).toBe(true);
      expect
        .soft(
          await viewport.evaluate((element) => element.scrollTop),
          `scroll after ${loaded} loaded events (was ${before})`,
        )
        .toBeGreaterThan(0);
    }
    await expect(
      page.getByRole("button", { name: "Load older", exact: true }),
    ).toHaveCount(0);
    await viewport.evaluate((element) => {
      element.scrollTop = element.scrollHeight;
      element.dispatchEvent(new Event("scroll"));
    });
    await expect(viewport.getByText(/History event 65:/)).toBeVisible();
    await expect
      .poll(() => viewport.evaluate((element) => element.scrollTop))
      .toBeGreaterThan(0);
    expect(pending).toHaveLength(0);
    expect(
      state.posts.filter((post) => post.path === "/api/v1/activity/page"),
    ).toHaveLength(4);
  });
}

for (const [filter, option] of [
  ["Severity", "Info"],
  ["Event type", "configuration saved"],
  ["Related category", "Configuration"],
]) {
  test(`activity explicit ${filter} change resets same-count results to top`, async ({
    page,
  }) => {
    const state = await mockFirmwareApi(page, { activityPageSize: 20 });
    state.activity = Array.from({ length: 20 }, (_, index) => ({
      sequence: 100 - index,
      unix_seconds: 1_700_000_000 - index,
      uptime_seconds: 500 - index,
      kind: "configuration_saved",
      detail: `Filter event ${index + 1}: ${"Room configuration saved. ".repeat(1 + (index % 4))}`,
    }));
    await page.goto("/#/activity");
    const viewport = page.getByRole("region", {
      name: "Scrollable activity history",
    });
    await expect(page.locator("#activity-list-summary")).toContainText(
      "20 loaded",
    );
    await viewport.evaluate((element) => {
      element.scrollTop = 400;
      element.dispatchEvent(new Event("scroll"));
    });
    await expect
      .poll(() => viewport.evaluate((element) => element.scrollTop))
      .toBeGreaterThan(0);
    await page.getByRole("button", { name: filter, exact: false }).click();
    await page.getByRole("option", { name: option, exact: true }).click();
    await expect(page.locator("#activity-list-summary")).toContainText(
      "20 loaded",
    );
    await expect
      .poll(() => viewport.evaluate((element) => element.scrollTop))
      .toBe(0);
  });
}

test("renders and scrolls a full virtualized activity page", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page, { activityPageSize: 20 });
  state.activity = Array.from({ length: 20 }, (_, index) => ({
    sequence: 100 - index,
    unix_seconds: 1_700_000_000 + index,
    uptime_seconds: 500 + index,
    kind: "configuration_saved",
    detail: `Virtualized event ${index + 1}.`,
  }));

  await page.goto("/#/activity");

  const viewport = page.getByRole("region", {
    name: "Scrollable activity history",
  });
  await expect(page.getByText("Virtualized event 1.")).toBeVisible();
  await expect(page.getByText("Virtualized event 2.")).toBeVisible();
  await viewport.evaluate((element) => {
    element.scrollTop = element.scrollHeight;
    element.dispatchEvent(new Event("scroll"));
  });
  await expect(page.getByText("Virtualized event 20.")).toBeVisible();
});

test("uses the keyboard command palette for bounded available actions", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/");

  // Wait for Solid to hydrate and attach the global keyboard listener before
  // sending the shortcut; page load alone does not guarantee hydration.
  await expect(
    page.getByRole("button", { name: /open command palette/i }),
  ).toBeVisible();
  await page.keyboard.press("Control+K");
  const palette = page.getByRole("dialog", { name: "Command palette" });
  await expect(palette).toBeVisible();
  const search = palette.getByPlaceholder(/Search pages/);
  await expect(search).toBeFocused();
  await search.fill("Welcome home");
  const automationOption = palette.getByRole("option", {
    name: /Welcome home/,
  });
  await expect(automationOption).toBeVisible();
  await automationOption.click();
  await expect
    .poll(() => state.posts.at(-1)?.path)
    .toBe("/api/v1/automations/operations/status");

  await page.getByRole("button", { name: /open command palette/i }).click();
  const shortcutSearch = page
    .getByRole("dialog", { name: "Command palette" })
    .getByPlaceholder(/Search pages/);
  await shortcutSearch.fill("Start Wi-Fi scan");
  await page.getByRole("option", { name: /Start Wi-Fi scan/ }).click();
  await expect(page).toHaveURL(/#\/system$/);
  await expect(
    page.getByRole("dialog", { name: "Wi‑Fi network" }),
  ).toBeVisible();
});

test("navigates with the Solid router without reloading the page", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/");
  await page.evaluate(() => {
    (
      window as typeof window & { corePilotDocumentMarker?: string }
    ).corePilotDocumentMarker = "same-document";
  });

  await page
    .locator("nav:visible")
    .getByRole("link", { name: "Devices" })
    .click();
  await expect(page).toHaveURL(/#\/devices$/);
  await expectHeading(page, "Devices");
  await expect(page).toHaveTitle("Devices · Core Pilot");
  expect(
    await page.evaluate(
      () =>
        (window as typeof window & { corePilotDocumentMarker?: string })
          .corePilotDocumentMarker,
    ),
  ).toBe("same-document");
  await expect(page.locator("#main-content")).toBeFocused();

  await page.goBack();
  await expectHeading(page, "Home");
  await page.goForward();
  await expectHeading(page, "Devices");
});

// Mobile contract: the drawer remains scrollable on short screens, closes after
// routing, and the document itself never develops horizontal overflow.
test("keeps navigation usable on a short mobile viewport", async ({ page }) => {
  await page.setViewportSize({ width: 375, height: 500 });
  await mockFirmwareApi(page);
  await page.goto("/");

  await page.getByRole("button", { name: "Toggle navigation" }).click();
  await expect(
    page.getByRole("heading", { name: "Core Pilot" }),
  ).toBeAttached();
  const sheet = page.locator("[data-slot='sheet-content']");
  const closeRow = sheet.locator("[data-mobile-drawer-header]");
  const closeButton = sheet.getByRole("button", { name: "Close navigation" });
  const mobileNav = sheet.locator("nav");
  const firstRoute = mobileNav.getByRole("link", {
    name: "Home",
    exact: true,
  });
  await expect(closeButton).toBeVisible();
  await expect(mobileNav).toBeVisible();
  await expect(firstRoute).toBeVisible();
  const closeRowBox = await closeRow.boundingBox();
  const firstRouteBox = await firstRoute.boundingBox();
  expect(closeRowBox).not.toBeNull();
  expect(firstRouteBox).not.toBeNull();
  expect(closeRowBox!.y + closeRowBox!.height).toBeLessThanOrEqual(
    firstRouteBox!.y,
  );
  await mobileNav
    .getByRole("link", { name: "System", exact: true })
    .scrollIntoViewIfNeeded();
  await mobileNav.getByRole("link", { name: "System", exact: true }).click();

  await expect(page).toHaveURL(/#\/system$/);
  await expectHeading(page, "System");
  await expect(page.locator("[data-slot='sheet-content']")).toBeHidden();
  expect(
    await page.evaluate(
      () =>
        document.documentElement.scrollWidth <=
        document.documentElement.clientWidth,
    ),
  ).toBe(true);
});

test("honors reduced motion for sheets and transitions", async ({ page }) => {
  await page.emulateMedia({ reducedMotion: "reduce" });
  await mockFirmwareApi(page);
  await page.goto("/#/rooms");
  await page.getByRole("button", { name: "Create room" }).first().click();
  const sheet = page.getByRole("dialog", { name: "Create room" });
  await expect(sheet).toBeVisible();

  const motion = await sheet.evaluate((element) => {
    const style = getComputedStyle(element);
    return {
      animationDuration: style.animationDuration,
      transitionDuration: style.transitionDuration,
    };
  });
  expect(["0s", "0.01ms", "1e-05s"]).toContain(motion.animationDuration);
  expect(["0s", "0.01ms", "1e-05s"]).toContain(motion.transitionDuration);
});

test("keeps right sheets usable at the minimum mobile width", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 568 });
  await mockFirmwareApi(page);
  await page.goto("/#/rooms");
  await page.getByRole("button", { name: "Create room" }).first().click();

  const sheet = page.getByRole("dialog", { name: "Create room" });
  await expect(sheet).toBeVisible();
  await expect(sheet.getByRole("button", { name: "Cancel" })).toBeVisible();
  await expect(
    sheet.getByRole("button", { name: "Create room" }),
  ).toBeVisible();

  await expect
    .poll(async () => {
      const box = await sheet.boundingBox();
      return box ? Math.round(box.x + box.width) : Number.POSITIVE_INFINITY;
    })
    .toBeLessThanOrEqual(320);
});

test("keeps every page within mobile and tablet viewports", async ({
  page,
}) => {
  const routes = [
    "/",
    "/#/activity",
    "/#/devices",
    "/#/devices/add",
    "/#/devices/configure",
    "/#/rooms",
    "/#/groups",
    "/#/scenes",
    "/#/automations",
    "/#/ai",
    "/#/ai/tools",
    "/#/ai/prompts",
    "/#/system",
  ];
  await mockFirmwareApi(page);

  for (const viewport of [
    { width: 320, height: 568 },
    { width: 768, height: 1024 },
  ]) {
    await page.setViewportSize(viewport);
    for (const route of routes) {
      await page.goto(route);
      await expect(page.locator("#main-content")).toBeVisible();
      expect(
        await page.evaluate(
          () =>
            document.documentElement.scrollWidth <=
            document.documentElement.clientWidth,
        ),
        `${route} overflowed at ${viewport.width}px`,
      ).toBe(true);
    }
  }
});

for (const width of [320, 1024]) {
  for (const navigation of ["expanded", "collapsed"] as const) {
    test(`long model cards wrap locally at ${width} with ${navigation} navigation`, async ({
      page,
    }) => {
      await page.setViewportSize({ width, height: 900 });
      const state = await mockFirmwareApi(page);
      const name = "N".repeat(64);
      const id = "i".repeat(48);
      state.groups = [{ id, name, device_ids: [id, "d".repeat(48)] }];
      state.scenes = [{ id, name, actions: [`scene|${id}`] }];
      state.automations = [
        {
          id,
          name,
          enabled: true,
          trigger: "manual",
          actions: [`scene|${id}`],
        },
      ];
      await page.goto("/#/groups");
      if (width >= 768) {
        const sidebar = page.locator("[data-state][data-collapsible]");
        if ((await sidebar.getAttribute("data-state")) !== navigation)
          await page.locator('[data-sidebar="trigger"]').click();
        await expect(sidebar).toHaveAttribute("data-state", navigation);
      } else if (navigation === "expanded") {
        await page.getByRole("button", { name: "Toggle navigation" }).click();
        await expect(page.locator('[data-slot="sheet-content"]')).toBeVisible();
        await page.getByRole("button", { name: "Close navigation" }).click();
        await expect(page.locator('[data-slot="sheet-content"]')).toBeHidden();
      }
      for (const route of ["groups", "scenes", "automations"]) {
        await page.evaluate((path) => {
          location.hash = `#/${path}`;
        }, route);
        await expectHeading(page, route[0].toUpperCase() + route.slice(1));
        const title = page.getByText(name, { exact: true });
        await expect(title).toBeVisible();
        const card = title.locator(
          "xpath=ancestor::div[contains(@class, 'rounded-lg')][1]",
        );
        await expect(card).toBeVisible();
        expect(
          await card.evaluate((element) => {
            const bounds = element.getBoundingClientRect();
            return Array.from(element.querySelectorAll("*"))
              .filter(
                (child) =>
                  child.textContent?.trim() && child.getClientRects().length,
              )
              .every((child) => {
                const box = child.getBoundingClientRect();
                return (
                  box.left >= bounds.left - 1 &&
                  box.right <= bounds.right + 1 &&
                  child.scrollWidth <= child.clientWidth + 1
                );
              });
          }),
        ).toBe(true);
        expect(
          await page.evaluate(
            () => document.documentElement.scrollWidth <= innerWidth,
          ),
        ).toBe(true);
        const header = page.locator("#main-content header").first();
        expect(
          await header.evaluate(
            (element) => element.scrollWidth <= element.clientWidth + 1,
          ),
        ).toBe(true);
      }
    });
  }
}

test("contains maximum-length room names within room cards", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  state.rooms = [{ id: `room-${"0".repeat(43)}`, name: "B".repeat(64) }];

  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto("/#/rooms");

  const title = page.getByRole("heading", { name: "B".repeat(64) });
  const card = title.locator(
    "xpath=ancestor::div[contains(@class, 'rounded-lg')][1]",
  );
  await expect(title).toBeVisible();
  await expect(card.getByText("0 devices")).toBeVisible();
  expect(
    await card.evaluate(
      (element) => element.scrollWidth <= element.clientWidth,
    ),
  ).toBe(true);
});

test("keeps confirmation actions visible on a short viewport", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 500 });
  await mockFirmwareApi(page);
  await page.goto("/");
  await page.locator("#app").getByRole("button", { name: "Reboot" }).click();

  const dialog = page.getByRole("dialog", { name: "Reboot Core Pilot?" });
  await expect(dialog.getByRole("button", { name: "Cancel" })).toBeVisible();
  await expect(dialog.getByRole("button", { name: "Reboot" })).toBeVisible();
  const box = await dialog.boundingBox();
  expect(box).not.toBeNull();
  expect(box!.x).toBeGreaterThanOrEqual(8);
  expect(box!.x + box!.width).toBeLessThanOrEqual(312);
  expect(box!.y).toBeGreaterThanOrEqual(8);
  expect(box!.y + box!.height).toBeLessThanOrEqual(492);
});

// Integration positive: drives local model CRUD forms and verifies each page posts
// bounded JSON mutations to the expected firmware model endpoint.
test("exercises local model CRUD pages for rooms, groups, scenes, and automations", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);

  await page.goto("/#/rooms");
  await page.getByRole("button", { name: "Create room" }).first().click();
  await page.getByPlaceholder("Kitchen").fill("Kitchen");
  await page.getByRole("button", { name: "Create room" }).last().click();
  await expect(page.getByText("Room saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/rooms/upsert",
    body: { name: "Kitchen" },
  });
  await page.getByRole("button", { name: "Edit Lab" }).click();
  await expect(page.getByRole("button", { name: "Update room" })).toBeVisible();
  await page.getByRole("button", { name: "Cancel" }).click();
  const postsBeforeDelete = state.posts.length;
  await page.getByRole("button", { name: "Delete Kitchen" }).click();
  await expect(page.getByRole("alertdialog")).toContainText(
    "Core Pilot will not silently change devices",
  );
  expect(state.posts).toHaveLength(postsBeforeDelete);
  await page.getByRole("button", { name: "Delete room" }).click();
  expect(state.posts.at(-1)).toMatchObject({ path: "/api/v1/rooms/delete" });

  await page.goto("/#/groups");
  await page.getByRole("button", { name: "Create group" }).first().click();
  const groupSheet = page.getByRole("dialog", { name: "Create group" });
  await groupSheet.getByPlaceholder("Downstairs lights").fill("Accent lights");
  await groupSheet.getByRole("button", { name: /^Devices/ }).click();
  await page.getByRole("option", { name: "Bench Lamp" }).click();
  await page.keyboard.press("Escape");
  await groupSheet.getByRole("button", { name: "Create group" }).click();
  await expect(page.getByText("Group saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/groups/upsert",
    body: {
      name: "Accent lights",
      device_ids: ["matter-light-1"],
    },
  });

  await page.goto("/#/scenes");
  await page.getByRole("button", { name: "Create scene" }).first().click();
  await page.getByPlaceholder("Movie night").fill("Movie night");
  const sceneSheet = page.getByRole("dialog", { name: "Create scene" });
  await sceneSheet
    .getByRole("button", { name: "Add Local notification" })
    .click();
  await sceneSheet.getByLabel("Message").fill("Movie mode ready");
  await sceneSheet.getByRole("button", { name: "Add Wait" }).click();
  await sceneSheet.getByLabel("Milliseconds").fill("1500");
  await sceneSheet.getByRole("button", { name: "Create scene" }).click();
  await expect(page.getByText("Scene saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/scenes/upsert",
    body: {
      name: "Movie night",
      actions: ["notify|Movie mode ready", "delay|1500"],
    },
  });

  await page.goto("/#/automations");
  await page.getByRole("button", { name: "Create automation" }).first().click();
  await page.getByPlaceholder("Wind-down routine").fill("Sunset routine");
  await page.getByRole("button", { name: "Continue" }).click();
  await expect(page.getByText("Choose a trigger")).toBeVisible();
  await page.getByRole("button", { name: "Continue" }).click();
  await page.getByRole("button", { name: "Add Wait" }).click();
  await page.getByLabel("Milliseconds").fill("1000");
  await page.getByRole("button", { name: "Continue" }).click();
  await page.getByRole("button", { name: "Create automation" }).last().click();
  await expect(page.getByText("Automation saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/automations/upsert",
    body: {
      name: "Sunset routine",
      enabled: true,
      trigger: "manual",
      actions: ["delay|1000"],
    },
  });
  await page.getByRole("button", { name: "Run now" }).last().click();
  await expect(
    page
      .getByRole("region", { name: "Automation run complete" })
      .getByText("The automation completed successfully."),
  ).toBeVisible();
  expect(
    state.posts.some(
      (post) =>
        post.path === "/api/v1/automations/run" &&
        post.body.id === "sunset-routine",
    ),
  ).toBe(true);
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/automations/operations/status",
    body: { operation_id: 101 },
  });
});

test("prefills automation templates backed by device-side execution", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/automations");
  await page.getByRole("button", { name: "Create automation" }).click();
  const sheet = page.getByRole("dialog", { name: "Create automation" });

  await expect(
    sheet.getByRole("button", { name: /Motion lighting/ }),
  ).toBeDisabled();
  await sheet
    .getByRole("button", { name: /Turn everything off at night/ })
    .click();
  await expect(sheet.getByPlaceholder("Wind-down routine")).toHaveValue(
    "Turn everything off at night",
  );
  await sheet.getByRole("button", { name: "Continue" }).click();
  await expect(sheet.getByLabel("Time")).toHaveValue("23:00");
  await sheet.getByRole("button", { name: "Continue" }).click();
  await expect(sheet.getByText("Turn off a device")).toBeVisible();
  await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
  await expect(
    page.getByRole("alertdialog", { name: "Discard unsaved changes?" }),
  ).toBeVisible();
});

test("shows reported room assignments and blocks deletion of referenced scenes", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/rooms");
  await expect(page.getByText("Bench Lamp")).toBeVisible();
  await expect(page.getByText("1 devices")).toBeVisible();

  await page.goto("/#/scenes");
  await page.getByRole("button", { name: "Delete Evening" }).click();
  await expect(page.getByText(/Used by: Morning/)).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Resolve dependencies first" }),
  ).toBeDisabled();
  await expect(page.getByRole("alertdialog")).toBeVisible();
});

// The touch-friendly controls are the accessible fallback for drag-and-drop and must
// preserve the exact ordered action contract submitted to firmware.
test("keeps the visual automation builder responsive and preserves action order", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 568 });
  const state = await mockFirmwareApi(page);

  await page.goto("/#/automations");
  await page.getByRole("button", { name: "Create automation" }).click();
  const sheet = page.getByRole("dialog", { name: "Create automation" });
  await sheet.getByPlaceholder("Wind-down routine").fill("Ordered flow");
  await sheet.getByRole("button", { name: "Continue" }).click();
  await sheet.getByRole("button", { name: "Continue" }).click();

  await expect(sheet.getByRole("tab")).toHaveCount(0);

  await sheet.getByRole("button", { name: "Add Wait" }).click();
  await sheet.getByLabel("Milliseconds").fill("2500");
  await sheet.getByRole("button", { name: "Add Device power" }).click();
  await sheet
    .getByRole("list", { name: "Ordered actions" })
    .getByRole("listitem")
    .nth(1)
    .getByRole("button")
    .nth(2)
    .click();
  await page.getByRole("option", { name: "Bench Lamp" }).click();
  await sheet.getByRole("button", { name: "Move action 2 up" }).click();

  await expect(sheet.getByText("2 / 8 actions")).toBeVisible();
  expect(
    await sheet.evaluate(
      (element) => element.scrollWidth <= element.clientWidth,
    ),
  ).toBe(true);

  await sheet.getByRole("button", { name: "Continue" }).click();
  await sheet.getByRole("button", { name: "Create automation" }).click();
  await expect(page.getByText("Automation saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/automations/upsert",
    body: {
      name: "Ordered flow",
      trigger: "manual",
      actions: ["on|matter-light-1|1", "delay|2500"],
    },
  });
});

test("navigates automation steps through icon controls", async ({ page }) => {
  await mockFirmwareApi(page);
  await page.goto("/#/automations");
  await page.getByRole("button", { name: "Create automation" }).click();
  const sheet = page.getByRole("dialog", { name: "Create automation" });
  const basicsStep = sheet.getByRole("button", { name: "Basics" });
  const triggerStep = sheet.getByRole("button", {
    name: "Trigger",
    exact: true,
  });

  await expect(basicsStep).toHaveAttribute("aria-current", "step");
  await triggerStep.click();
  await expect(sheet.getByText("Give this automation a name.")).toBeVisible();
  await sheet.getByPlaceholder("Wind-down routine").fill("Icon navigation");
  await triggerStep.click();
  await expect(triggerStep).toHaveAttribute("aria-current", "step");
  await expect(sheet.getByText("Choose a trigger")).toBeVisible();

  await basicsStep.click();
  await expect(basicsStep).toHaveAttribute("aria-current", "step");
  await expect(sheet.getByPlaceholder("Wind-down routine")).toHaveValue(
    "Icon navigation",
  );
});

test("adds automation blocks with pointer drag and drop", async ({ page }) => {
  await mockFirmwareApi(page);
  await page.goto("/#/automations");
  await page.getByRole("button", { name: "Create automation" }).click();
  const sheet = page.getByRole("dialog", { name: "Create automation" });
  await sheet.getByPlaceholder("Wind-down routine").fill("Dragged flow");
  await sheet.getByRole("button", { name: "Continue" }).click();
  await sheet.getByRole("button", { name: "Continue" }).click();

  const limitsButton = sheet.getByRole("button", {
    name: "Automation limits",
  });
  await limitsButton.scrollIntoViewIfNeeded();
  await limitsButton.hover();
  await expect(page.getByRole("tooltip")).toContainText(
    "One trigger and up to eight ordered actions",
  );

  const dataTransfer = await page.evaluateHandle(() => new DataTransfer());
  await sheet
    .getByRole("button", { name: "Add Wait" })
    .dispatchEvent("dragstart", { dataTransfer });
  await sheet.getByText("Drop the first action here").dispatchEvent("drop", {
    dataTransfer,
  });

  await expect(sheet.getByLabel("Milliseconds")).toHaveValue("1000");
  await expect(sheet.getByText("1 / 8 actions")).toBeVisible();
});

test("expands commissioning diagnostics with the Accordion primitive", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/devices/add");

  const commissioningDiagnostics = page.getByRole("button", {
    name: /Commissioning diagnostics/,
  });
  await expect(commissioningDiagnostics).toHaveAttribute(
    "aria-expanded",
    "false",
  );
  await commissioningDiagnostics.click();
  await expect(commissioningDiagnostics).toHaveAttribute(
    "aria-expanded",
    "true",
  );
  await expect(page.getByText("PASE / CASE", { exact: true })).toBeVisible();
});

// Integration positive/negative: verifies device inventory rendering, Matter setup
// diagnostics, and validation-only commissioning requests without claiming hardware success.
test("covers device inventory, Matter diagnostics, and Matter commissioning requests", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  state.matter.fabric_configured = true;
  state.matter.fabric_credentials_ready = true;
  state.matter.case_ready = true;
  state.matter.wifi_commissioning_ready = true;
  state.matter.thread_dataset_configured = true;
  state.matter.thread_ready = true;
  state.matter.thread_commissioning_ready = true;

  await page.goto("/#/devices");
  await expect(page.getByText("Bench Lamp", { exact: true })).toBeVisible();
  await expect(page.getByText("On/Off", { exact: true })).toBeVisible();
  await expect(page.getByText("Level", { exact: true })).toBeVisible();
  await expect(
    page
      .locator('[data-device-id="matter-light-1"]')
      .getByText("Active", { exact: true }),
  ).toBeVisible();

  await expect(page.getByRole("columnheader", { name: "Actions" })).toHaveCount(
    1,
  );
  await page
    .locator('[data-device-id="matter-light-1"]')
    .dispatchEvent("contextmenu", { clientX: 100, clientY: 100 });
  await page
    .getByRole("menuitem", { name: "Remove device… Bench Lamp" })
    .click();
  await page.getByRole("button", { name: "Remove device" }).click();
  await expect(
    page.getByRole("region", { name: "Matter removal request accepted" }),
  ).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/matter/devices/remove",
    body: { device_id: "0000000000001234" },
  });

  await page.goto("/#/devices/configure");
  await expectHeading(page, "Device Setup");
  await page.getByText("Matter implementation diagnostics").click();
  await expect(page.getByText("rs-matter parser")).toBeVisible();
  await expect(page.getByText("Matter setup is modeled")).toBeVisible();

  await page.goto("/#/devices/add");
  await expect(
    page.getByRole("heading", { name: "Recent Matter operations" }),
  ).toBeVisible();
  await expect(page.getByText("Set On Off", { exact: true })).toBeVisible();
  await expect(page.getByText("Complete", { exact: true })).toBeVisible();
  await expect(
    page.getByText("Device 0000000000001234 · Endpoint 1", { exact: false }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Start Wi‑Fi journey" }).click();
  const wifiSheet = page.getByRole("dialog", { name: "Matter over Wi‑Fi" });
  await wifiSheet
    .getByPlaceholder("Enter manual setup code")
    .fill("34970112332");
  await page.getByRole("button", { name: "Add with Wi‑Fi" }).click();
  await expect(
    page
      .getByRole("region", { name: "Pairing request accepted" })
      .getByText("wifi pairing request received."),
  ).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/matter/commission",
    body: { transport: "wifi", setup_payload: "34970112332" },
  });
  await expect(wifiSheet).toBeHidden();

  await page.getByRole("button", { name: "Start Thread journey" }).click();
  await page.getByPlaceholder("Enter manual setup code").fill("34970112332");
  await page.getByRole("button", { name: "Add with Thread" }).click();
  await expect.poll(() => state.posts.at(-1)?.body.transport).toBe("thread");
  expect(state.posts.at(-1)).toEqual({
    path: "/api/v1/matter/commission",
    body: {
      transport: "thread",
      setup_payload: "34970112332",
    },
  });
});

// Integration positive/negative: exercises AI chat/provider forms and confirms modeled
// destructive tools remain disabled while available tools can be toggled.
test("keeps the assistant page usable while firmware APIs are unavailable", async ({
  page,
}) => {
  await page.route("**/api/v1/auth", (route) =>
    json(route, { authentication_required: false }),
  );
  await page.goto("/#/ai");
  await expectHeading(page, "Optional Assistant");
  await expect(
    page.getByPlaceholder(
      "Ask Core Pilot to check status, list devices, scenes, or automations…",
    ),
  ).toBeVisible();
});

test("covers AI chat, provider setup, encrypted memory, and tool permissions", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);

  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto("/#/ai");
  const providerCard = page.locator("[data-assistant-provider-card]");
  await expect(providerCard).toBeVisible();
  const providerBox = await providerCard.boundingBox();
  expect(providerBox?.height).toBeLessThan(360);
  await page
    .getByPlaceholder(
      "Ask Core Pilot to check status, list devices, scenes, or automations…",
    )
    .fill("Check status");
  await page.getByRole("button", { name: "Send" }).click();
  await expect(
    page.getByText("Core Pilot status looks healthy."),
  ).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/ai/chat",
    body: { message: "Check status" },
  });

  await page.getByRole("button", { name: "Replace provider" }).click();
  await page.getByRole("button", { name: "OpenAI", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "OpenAI compatible" }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Authorization: Bearer" }),
  ).toBeVisible();
  await page.getByLabel("Model").fill("gpt-test");
  await page.getByLabel("API key").fill("secret-key");
  await page.getByRole("button", { name: "Save provider" }).click();
  await expect(
    page
      .getByRole("region", { name: "Provider settings saved" })
      .getByText("AI provider encrypted."),
  ).toBeVisible();
  expect(state.posts.at(-1)?.path).toBe("/api/v1/ai/provider");

  await page.getByRole("link", { name: "Saved context", exact: true }).click();
  await expect(
    page.getByRole("tab", { name: "Saved context" }),
  ).toHaveAttribute("aria-selected", "true");
  await page.getByRole("button", { name: /Home facts/ }).click();
  await page
    .getByRole("textbox", { name: "Saved context Markdown" })
    .fill("Prefer verified local device state.");
  await page.getByRole("button", { name: "Save encrypted" }).click();
  await expect(page.getByText("Memory saved.", { exact: true })).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/ai/memory/section",
    body: { id: "home", markdown: "Prefer verified local device state." },
  });

  await page.goto("/#/ai/tools");
  const deviceListSwitch = page.getByRole("switch", {
    name: "Toggle devices.list",
  });
  await expect(deviceListSwitch).toBeChecked();
  await page.locator('[data-tool-switch-control="devices.list"]').click();
  await expect(deviceListSwitch).not.toBeChecked();
  await expect(page.getByText("Tool permission saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/ai/mcp/tool",
    body: { tool: "devices.list", enabled: false },
  });
  await expect(page.getByLabel("Toggle ota.apply")).toBeVisible();
  await expect(page.getByLabel("Toggle ota.apply")).toBeDisabled();
});

test("confirms destructive MCP enables while keeping disable immediate", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  Object.assign(state.mcpTools.tools[1], {
    id: "devices.set_on_off",
    name: "SetOnOff",
    title: "Set device power",
    available: true,
  });
  await page.goto("/#/ai/tools");

  const destructiveSwitch = page.getByRole("switch", {
    name: "Toggle devices.set_on_off",
  });
  await expect(destructiveSwitch).not.toBeChecked();
  await page.locator('[data-tool-switch-control="devices.set_on_off"]').click();
  await expect(
    page.getByRole("alertdialog", { name: "Enable destructive tool?" }),
  ).toBeVisible();
  expect(
    state.posts.some(
      (post) =>
        post.path === "/api/v1/ai/mcp/tool" && post.body.enabled === true,
    ),
  ).toBe(false);

  await page.getByRole("button", { name: "Enable tool" }).click();
  await expect(destructiveSwitch).toBeChecked();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/ai/mcp/tool",
    body: { tool: "SetOnOff", enabled: true },
  });

  await page.locator('[data-tool-switch-control="devices.set_on_off"]').click();
  await expect(destructiveSwitch).not.toBeChecked();
  await expect(page.getByRole("alertdialog")).toHaveCount(0);
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/ai/mcp/tool",
    body: { tool: "SetOnOff", enabled: false },
  });
});

// Integration positive: edits an encrypted prompt file through the Assistant UI and
// checks both Markdown preview and the encrypted-save mutation path.
test("covers encrypted assistant prompt editing and preview", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);

  await page.goto("/#/ai/prompts");
  await expectHeading(page, "Assistant Prompts");
  await page.getByRole("button", { name: /System prompt/ }).click();
  await expect(
    page.getByRole("dialog", { name: "System prompt" }),
  ).toBeVisible();
  await page
    .getByRole("textbox", { name: "Prompt Markdown" })
    .fill("# Updated\n- **Bounded** action\n`safe`");
  await page.getByRole("tab", { name: "Preview" }).click();
  await expect(page.getByRole("heading", { name: "Updated" })).toBeVisible();
  await expect(
    page
      .getByRole("listitem")
      .filter({ hasText: "Bounded action" })
      .locator("strong"),
  ).toBeVisible();
  await page.getByRole("button", { name: "Save encrypted" }).click();
  await expect(page.getByText("Prompt file saved.")).toBeVisible();
  expect(state.posts.at(-1)?.path).toBe("/api/v1/ai/prompts/file");
});

test("unverified baseline preview cannot overwrite an unseen encrypted prompt", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  const custom =
    "# Private override\n\nKeep this existing encrypted content. é";
  state.aiPrompts.files[0].markdown = custom;
  let failed = true;
  await page.route("**/api/v1/ai/prompts", async (route) => {
    if (failed)
      await json(route, { detail: "Prompt storage unavailable" }, 503);
    else await route.fallback();
  });
  await page.goto("/#/ai/prompts");
  await expect(
    page.getByText(
      "Encrypted overrides could not be loaded. Only built-in prompts are shown.",
    ),
  ).toBeVisible();
  await page.getByRole("button", { name: /Assistant system prompt/ }).click();
  const dialog = page.getByRole("dialog", {
    name: "Assistant system prompt",
    exact: true,
  });
  const field = dialog.getByRole("textbox", { name: "Prompt Markdown" });
  await expect(field).toHaveJSProperty("readOnly", true);
  await expect(field).toHaveValue(/Core Pilot assistant/);
  await expect(
    dialog.getByRole("status").filter({ hasText: "read-only preview" }),
  ).toContainText("Close it, retry loading, and reopen");
  await expect(
    dialog.getByRole("button", { name: "Save encrypted" }),
  ).toBeDisabled();
  await dialog.getByRole("tab", { name: "Preview" }).click();
  await expect(
    dialog.getByRole("heading", { name: "Core Pilot assistant", exact: true }),
  ).toBeVisible();
  await dialog
    .locator("form")
    .evaluate((form) =>
      form.dispatchEvent(
        new Event("submit", { bubbles: true, cancelable: true }),
      ),
    );
  expect(state.posts).toEqual([]);
  expect(state.aiPrompts.files[0].markdown).toBe(custom);
  await dialog.getByRole("button", { name: "Cancel", exact: true }).click();
  failed = false;
  await page.getByRole("button", { name: "Retry", exact: true }).click();
  await expect(
    page.getByRole("button", { name: /Assistant system prompt/ }),
  ).toHaveCount(0);
  await page.getByRole("button", { name: /System prompt/ }).click();
  const verified = page.getByRole("dialog", {
    name: "System prompt",
    exact: true,
  });
  const verifiedField = verified.getByRole("textbox", {
    name: "Prompt Markdown",
  });
  await expect(verifiedField).toHaveValue(custom);
  await expect(verifiedField).toBeEditable();
  const updated = custom + "\n\nUser-approved addition.";
  await verifiedField.fill(updated);
  await verified.getByRole("button", { name: "Save encrypted" }).click();
  await expect(verified).toHaveCount(0);
  expect(state.posts).toHaveLength(1);
  expect(state.posts[0]).toMatchObject({
    path: "/api/v1/ai/prompts/file",
    body: { path: "assistant/system.md", markdown: "" },
  });
  expect(
    Buffer.from(String(state.posts[0].body.markdown_base64), "base64").toString(
      "utf8",
    ),
  ).toBe(updated);
});

test("prompt refresh failure makes cached entries read-only and preserves an existing draft", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/#/ai/prompts");
  const open = page.getByRole("button", { name: /System prompt/ });
  await expect(open).toBeVisible();
  let failed = true;
  await page.route("**/api/v1/ai/prompts", async (route) => {
    if (failed)
      await json(route, { detail: "Prompt refresh unavailable" }, 503);
    else await route.fallback();
  });
  const refresh = async () => {
    await page.context().setOffline(true);
    await expect.poll(() => page.evaluate(() => navigator.onLine)).toBe(false);
    await page.context().setOffline(false);
  };
  const stale = page.getByText(
    "Prompt files could not be refreshed. Showing cached files and built-in prompts; data may be stale.",
  );
  await refresh();
  await expect(stale).toBeVisible();
  await open.click();
  const dialog = page.getByRole("dialog", {
    name: "System prompt",
    exact: true,
  });
  const field = dialog.getByRole("textbox", { name: "Prompt Markdown" });
  const save = dialog.getByRole("button", { name: "Save encrypted" });
  await expect(field).toHaveJSProperty("readOnly", true);
  await expect(save).toBeDisabled();
  await dialog.getByRole("button", { name: "Cancel", exact: true }).click();
  failed = false;
  await page.getByRole("button", { name: "Retry", exact: true }).click();
  await expect(stale).toHaveCount(0);
  await open.click();
  const draft = "# Unsaved draft\n\nDo not erase this user edit.";
  await field.fill(draft);
  await expectDraftDirty(page, true);
  failed = true;
  await refresh();
  await expect(save).toBeDisabled();
  await expect(field).toHaveValue(draft);
  await expect(field).toHaveJSProperty("readOnly", true);
  await expect(
    dialog.getByRole("status").filter({ hasText: "read-only preview" }),
  ).toContainText("current content could not be verified");
  await dialog
    .locator("form")
    .evaluate((form) =>
      form.dispatchEvent(
        new Event("submit", { bubbles: true, cancelable: true }),
      ),
    );
  expect(state.posts).toEqual([]);
  failed = false;
  // Exercise the page's query retry without dismissing the modal or its dirty draft.
  await page
    .getByRole("button", { name: "Retry", exact: true, includeHidden: true })
    .evaluate((button: HTMLButtonElement) => button.click());
  await expect(save).toBeEnabled();
  await expect(field).toHaveValue(draft);
  await expect(field).toBeEditable();
  await expectDraftDirty(page, true);
  expect(state.posts).toEqual([]);
});

const previewMarkdown = [
  "# Local guide",
  "",
  "## Nested guidance",
  "",
  "### Details",
  "",
  "- First item",
  "  - Nested item",
  "",
  "1. Ordered item",
  "2. Next item",
  "",
  "```js",
  "const bounded = true;",
  "```",
  "",
  "[Documentation](https://example.com/guide)",
  "",
  "| Setting | Value |",
  "| --- | --- |",
  "| Mode | Local |",
].join("\n");

test("assistant preview renders semantic styled Markdown and passes axe", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/ai/prompts");
  await page.getByRole("button", { name: /System prompt/ }).click();
  const dialog = page.getByRole("dialog", { name: "System prompt" });
  await dialog
    .getByRole("textbox", { name: "Prompt Markdown" })
    .fill(previewMarkdown);
  await dialog.getByRole("tab", { name: "Preview" }).click();
  const preview = dialog.getByRole("region", { name: "Markdown preview" });
  for (const [level, name] of [
    [1, "Local guide"],
    [2, "Nested guidance"],
    [3, "Details"],
  ] as const) {
    await expect(
      preview.getByRole("heading", { level, name, exact: true }),
    ).toBeVisible();
  }
  await expect(preview.locator("ul ul li")).toHaveText("Nested item");
  await expect(preview.locator("ol > li")).toHaveCount(2);
  expect(
    await preview
      .locator("ul")
      .first()
      .evaluate((element) => getComputedStyle(element).listStyleType),
  ).not.toBe("none");
  expect(
    await preview
      .locator("ol")
      .evaluate((element) => getComputedStyle(element).listStyleType),
  ).toBe("decimal");
  await expect(preview.locator("pre > code")).toContainText(
    "const bounded = true;",
  );
  await expect(
    preview.getByRole("link", { name: "Documentation" }),
  ).toHaveAttribute("href", "https://example.com/guide");
  await expect(
    preview.getByRole("columnheader", { name: "Setting" }),
  ).toBeVisible();
  await expect(
    preview.getByRole("cell", { name: "Local", exact: true }),
  ).toBeVisible();
  await expectAccessible(page);
});

test("assistant preview rejects executable HTML and unsafe URLs without image requests", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  const resources: string[] = [];
  await page.route("https://markdown-resource.invalid/**", async (route) => {
    resources.push(route.request().url());
    await route.abort();
  });
  const dialogs: string[] = [];
  page.on("dialog", async (dialog) => {
    dialogs.push(dialog.message());
    await dialog.dismiss();
  });
  await page.goto("/#/ai/prompts");
  await page.getByRole("button", { name: /System prompt/ }).click();
  await page
    .getByRole("textbox", { name: "Prompt Markdown" })
    .fill(
      [
        "# Safe preview",
        "",
        "[unsafe](javascript:alert%281%29)",
        "[data](data:text/html,attack)",
        "![image disabled](https://markdown-resource.invalid/image.png)",
        "",
        '<script>alert("executed")</script>',
        '<img src="https://markdown-resource.invalid/raw.png" onerror="alert(1)">',
        '<iframe src="https://markdown-resource.invalid/frame"></iframe>',
        '<svg onload="alert(1)"></svg>',
      ].join("\n"),
    );
  await page.getByRole("tab", { name: "Preview" }).click();
  const preview = page.getByRole("region", { name: "Markdown preview" });
  await expect(
    preview.getByRole("heading", { name: "Safe preview" }),
  ).toBeVisible();
  await expect(
    preview.locator("script,img,iframe,svg,[onerror],[onload]"),
  ).toHaveCount(0);
  await expect(
    preview.locator('a[href^="javascript:"],a[href^="data:"]'),
  ).toHaveCount(0);
  await expect(preview).toContainText("image disabled");
  await page.waitForTimeout(100);
  expect(resources).toEqual([]);
  expect(dialogs).toEqual([]);
});

for (const kind of ["prompt", "memory"] as const) {
  test(`assistant ${kind} enforces UTF-8 bounds from Editor and Preview`, async ({
    page,
  }) => {
    const state = await mockFirmwareApi(page);
    const limit = kind === "prompt" ? 4096 : 12288;
    await page.goto(
      `/#/ai/prompts${kind === "memory" ? "?section=memory" : ""}`,
    );
    await page
      .getByRole("button", {
        name: kind === "prompt" ? /System prompt/ : /Home facts/,
      })
      .click();
    const field = page.getByRole("textbox", {
      name: kind === "prompt" ? "Prompt Markdown" : "Saved context Markdown",
    });
    const oversized = "é".repeat(limit / 2) + "a";
    expect(new TextEncoder().encode(oversized).length).toBe(limit + 1);
    await field.fill(oversized);
    await page.getByRole("button", { name: "Save encrypted" }).click();
    await expect(
      page.getByText(
        kind === "prompt"
          ? "Prompt must be 4 KiB or less."
          : "Saved context must be 12 KiB or less.",
        { exact: true },
      ),
    ).toBeVisible();
    await expectDraftDirty(page, true);
    await page.getByRole("tab", { name: "Preview" }).click();
    const preview = page.getByRole("region", { name: "Markdown preview" });
    await expect(preview.getByRole("status")).toHaveText(
      `${kind === "prompt" ? "Prompt must be 4 KiB or less." : "Saved context must be 12 KiB or less."} Reduce the draft in Editor to preview it.`,
    );
    await expect(preview.locator(".markdown-viewer")).toHaveCount(0);
    await page.getByRole("button", { name: "Save encrypted" }).click();
    expect(state.posts).toEqual([]);
    await page.getByRole("tab", { name: "Editor", exact: true }).click();
    await expect(field).toHaveValue(oversized);
    const valid = "é".repeat(limit / 2);
    await field.fill(valid);
    const editorNode = await field.elementHandle();
    await field.focus();
    await field.evaluate((element: HTMLTextAreaElement) =>
      element.setSelectionRange(7, 7),
    );
    await page.keyboard.insertText("Z");
    await expect(field).toHaveValue(valid.slice(0, 7) + "Z" + valid.slice(7));
    await page.getByRole("tab", { name: "Preview" }).click();
    await expect(preview.getByRole("status")).toBeVisible();
    await expect(preview.locator(".markdown-viewer")).toHaveCount(0);
    await page.getByRole("tab", { name: "Editor", exact: true }).click();
    expect(
      await field.evaluate(
        (element, original) => element === original,
        editorNode,
      ),
    ).toBe(true);
    expect(
      await field.evaluate((element: HTMLTextAreaElement) => [
        element.selectionStart,
        element.selectionEnd,
      ]),
    ).toEqual([8, 8]);
    await field.focus();
    await page.keyboard.press("ControlOrMeta+z");
    await expect(field).toHaveValue(valid);
    await page.getByRole("tab", { name: "Preview" }).click();
    await expect(preview.locator(".markdown-viewer")).toHaveText(valid);
    await page.getByRole("button", { name: "Save encrypted" }).click();
    await expect(page.getByRole("dialog")).toHaveCount(0);
    expect(state.posts.at(-1)).toMatchObject({
      path:
        kind === "prompt"
          ? "/api/v1/ai/prompts/file"
          : "/api/v1/ai/memory/section",
      body: {
        ...(kind === "prompt"
          ? { path: "assistant/system.md" }
          : { id: "home" }),
        ...(kind === "prompt" ? { markdown: "" } : { markdown: valid }),
      },
    });
    const savedMarkdown =
      kind === "prompt"
        ? Buffer.from(
            String(state.posts.at(-1)?.body.markdown_base64),
            "base64",
          ).toString("utf8")
        : state.posts.at(-1)?.body.markdown;
    expect(savedMarkdown).toBe(valid);
    expect(Buffer.byteLength(String(savedMarkdown), "utf8")).toBe(limit);
  });
}

test("assistant context tabs preserve URL history and existing memory records", async ({
  page,
}, testInfo) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/#/ai/prompts?section=invalid&keep=yes");
  const prompts = page.getByRole("tab", { name: "Prompt files", exact: true });
  const memory = page.getByRole("tab", { name: "Saved context", exact: true });
  await expect(prompts).toHaveAttribute("aria-selected", "true");
  await expect(page.locator('[role="tabpanel"] section')).toHaveCount(1);
  await expect(page.locator('[role="tabpanel"] section section')).toHaveCount(
    0,
  );
  await prompts.focus();
  await page.keyboard.press("ArrowRight");
  await expect(memory).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(memory).toHaveAttribute("aria-selected", "true");
  await expect(page).toHaveURL(/section=memory/);
  await expect(page).toHaveURL(/keep=yes/);
  for (const section of state.aiMemory.sections) {
    await expect(
      page.getByRole("button", { name: new RegExp(section.title) }),
    ).toBeVisible();
  }
  await page.goBack();
  await expect(prompts).toHaveAttribute("aria-selected", "true");
  await page.goForward();
  await expect(memory).toHaveAttribute("aria-selected", "true");
  await expect(page).toHaveURL(/keep=yes/);
  await page.screenshot({
    path: testInfo.outputPath("saved-context.png"),
    animations: "disabled",
  });
  expect(state.posts).toEqual([]);
});

test("assistant draft guards close, retains rejected save and closes accepted retry once", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/#/ai/prompts?section=memory");
  const open = page.getByRole("button", { name: /Preferences/ });
  await open.click();
  const field = page.getByRole("textbox", { name: "Saved context Markdown" });
  await expectDraftDirty(page, false);
  await field.fill("Changed preference");
  await expectDraftDirty(page, true);
  await field.fill("Prefer local actions.");
  await expectDraftDirty(page, false);
  await field.fill("Changed preference");
  await page.getByRole("button", { name: "Cancel", exact: true }).click();
  await expect(page.getByRole("alertdialog")).toBeVisible();
  await page.getByRole("button", { name: "Keep editing" }).click();
  const reject = async (route: Route) =>
    json(route, { accepted: false, detail: "Context save rejected." });
  await page.route("**/api/v1/ai/memory/section", reject);
  await page.getByRole("button", { name: "Save encrypted" }).click();
  await expect(
    page.getByText("Context save rejected.", { exact: true }),
  ).toBeVisible();
  await expect(field).toHaveValue("Changed preference");
  await expectDraftDirty(page, true);
  await page.unroute("**/api/v1/ai/memory/section", reject);
  await page.getByRole("button", { name: "Save encrypted" }).click();
  await expect(page.getByRole("dialog")).toHaveCount(0);
  await expect(page.getByText("Memory saved.", { exact: true })).toHaveCount(1);
  expect(state.posts.at(-1)).toMatchObject({
    body: { id: "prefs", markdown: "Changed preference" },
  });
  await open.click();
  await expectDraftDirty(page, false);
  await expect(field).toHaveValue("Changed preference");
  const editor = page.getByRole("tab", { name: "Editor", exact: true });
  await editor.focus();
  await page.keyboard.press("ArrowRight");
  const preview = page.getByRole("tab", { name: "Preview", exact: true });
  await expect(preview).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(preview).toHaveAttribute("aria-selected", "true");
  await page.keyboard.press("ArrowLeft");
  await page.keyboard.press("Enter");
  await expect(editor).toHaveAttribute("aria-selected", "true");
});

for (const width of [320, 768]) {
  test(`assistant Markdown table and code stay contained at ${width}px`, async ({
    page,
  }) => {
    await mockFirmwareApi(page);
    await page.setViewportSize({ width, height: 900 });
    await page.goto("/#/ai/prompts");
    await page.getByRole("button", { name: /System prompt/ }).click();
    await page
      .getByRole("textbox", { name: "Prompt Markdown" })
      .fill(
        previewMarkdown +
          "\n\n```\n" +
          "long_code_".repeat(100) +
          "\n```\n\n| Long | Value |\n| --- | --- |\n| " +
          "wide".repeat(100) +
          " | local |",
      );
    await page.getByRole("tab", { name: "Preview" }).click();
    await expect(page.getByRole("table")).toHaveCount(2);
    const bounds = await page
      .getByRole("region", { name: "Markdown preview" })
      .evaluate((element) => {
        const rect = element.getBoundingClientRect();
        return {
          left: rect.left,
          right: rect.right,
          overflow: document.documentElement.scrollWidth > innerWidth,
        };
      });
    expect(bounds.left).toBeGreaterThanOrEqual(0);
    expect(bounds.right).toBeLessThanOrEqual(width);
    expect(bounds.overflow).toBe(false);
    await expectAccessible(page);
  });
}

test("shows saved Wi‑Fi state and confirms forgetting without exposing passwords", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/#/system");

  await page.getByRole("button", { name: "Edit Wi‑Fi" }).click();
  await expect(page.getByText("Old_Network", { exact: true })).toBeVisible();
  await expect(page.getByText("Saved", { exact: true }).first()).toBeVisible();
  await expect(
    page.getByText("Connected", { exact: true }).first(),
  ).toBeVisible();
  await page.getByRole("button", { name: "Continue" }).click();
  await expect(page.locator("input[type='password']")).toHaveValue("");
  await page.getByRole("button", { name: "Network" }).click();

  await page.getByRole("button", { name: "Forget Old_Network" }).click();
  await expect(
    page.getByRole("alertdialog", { name: "Forget Old_Network?" }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Forget network" }).click();

  await expect(page.getByText("Saved network forgotten.")).toBeVisible();
  await expect(page.getByText("Old_Network", { exact: true })).toHaveCount(0);
  expect(state.posts.at(-1)).toEqual({
    path: "/api/v1/network/wifi/forget",
    body: { ssid: "Old_Network" },
  });
});

test("keeps a full saved Wi‑Fi list usable at minimum mobile width", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 568 });
  const state = await mockFirmwareApi(page);
  state.savedWifiNetworks = Array.from({ length: 8 }, (_, index) => ({
    ssid:
      index === 0
        ? "Connected_Network_With_A_Long_Name"
        : `Saved_Network_${index + 1}_With_Long_Name`,
    connected: index === 0,
  }));
  await page.goto("/#/system");

  await page.getByRole("button", { name: "Edit Wi‑Fi" }).click();
  const sheet = page.getByRole("dialog", { name: "Wi‑Fi network" });
  await expect(sheet).toBeVisible();
  await expect(
    sheet.getByText("Up to 8 encrypted networks are kept."),
  ).toBeVisible();
  await expect(
    sheet.getByRole("button", {
      name: "Forget Connected_Network_With_A_Long_Name",
    }),
  ).toBeVisible();
  expect(
    await sheet.evaluate(
      (element) => element.scrollWidth <= element.clientWidth,
    ),
  ).toBe(true);

  await sheet
    .getByRole("button", {
      name: "Forget Connected_Network_With_A_Long_Name",
    })
    .click();
  const confirmation = page.getByRole("alertdialog", {
    name: "Forget Connected_Network_With_A_Long_Name?",
  });
  await expect(
    confirmation.getByText(/try another saved network/i),
  ).toBeVisible();
  await expect(
    confirmation.getByText(/setup access point will start/i),
  ).toBeVisible();
  await expect(
    confirmation.getByRole("button", { name: "Cancel" }),
  ).toBeVisible();
  await expect(
    confirmation.getByRole("button", { name: "Forget network" }),
  ).toBeVisible();
});

test("changes Wi‑Fi using the canonical DHCP hostname", async ({ page }) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/#/system");

  await page.getByRole("button", { name: "Edit Wi‑Fi" }).click();
  await page.getByRole("button", { name: "Continue" }).click();
  await page
    .getByPlaceholder("Select a network or enter SSID")
    .fill("Mesh_IoT");
  await page.locator("input[type='password']").first().fill("network-secret");
  await page.getByRole("button", { name: "Continue" }).click();
  await page.getByRole("button", { name: "Save Wi‑Fi" }).click();

  await expect(page.getByText("Wi‑Fi settings saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/network/wifi",
    body: {
      credentials: { ssid: "Mesh_IoT", password: "network-secret" },
      hostname: "corepilot",
    },
  });
});

test("covers system Wi‑Fi, time settings, and firmware upload validation", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);

  await page.goto("/#/system");
  await expect(
    page.getByText("Lab_WiFi", { exact: true }).first(),
  ).toBeVisible();
  await expect(
    page.getByText("192.168.29.2", { exact: true }).first(),
  ).toBeVisible();
  await expect(
    page.getByText("Cloudflare time.cloudflare.com", { exact: true }).first(),
  ).toBeVisible();
  await expect(
    page.getByText("UTC · UTC+00:00", { exact: true }).first(),
  ).toBeVisible();
  await expect(
    page.getByText("UTC · UTC+00:00", { exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Edit Wi‑Fi" }).click();
  await page.getByRole("button", { name: "Continue" }).click();
  await page
    .getByPlaceholder("Select a network or enter SSID")
    .fill("Mesh_IoT");
  await page.locator("input[type='password']").first().fill("network-secret");
  await page.getByRole("button", { name: "Continue" }).click();
  await page.getByRole("button", { name: "Save Wi‑Fi" }).click();
  await expect(page.getByText("Wi‑Fi settings saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/network/wifi",
    body: {
      credentials: { ssid: "Mesh_IoT", password: "network-secret" },
      hostname: "corepilot",
    },
  });
  await expect(
    page.getByRole("dialog", { name: "Wi‑Fi network" }),
  ).toBeHidden();

  await page.getByRole("button", { name: "Edit time settings" }).click();
  await expect(page.getByRole("combobox", { name: "NTP server" })).toHaveValue(
    "Cloudflare time.cloudflare.com",
  );
  await expect(page.getByRole("combobox", { name: "Timezone" })).toHaveValue(
    "UTC · UTC+00:00",
  );
  const timeDialog = page.getByRole("dialog", { name: "Time source" });
  await timeDialog
    .getByRole("button", { name: "Open options" })
    .first()
    .click();
  await page.getByRole("option", { name: "Google time.google.com" }).click();
  await page.getByRole("button", { name: "Save time settings" }).click();
  await expect(page.getByText("Time settings saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/time",
    body: {
      ntp_server_id: "google",
      timezone_id: "UTC",
      timezone_offset_minutes: 0,
    },
  });

  await page.locator("#firmware-file").setInputFiles({
    name: "core-pilot-0.2.0-ota.bin",
    mimeType: "application/octet-stream",
    buffer: Buffer.from("firmware image"),
  });
  await expect(
    page.getByRole("button", { name: "Validate & upload" }),
  ).toBeEnabled();
  await page.getByRole("button", { name: "Validate & upload" }).click();
  await expect(page.getByText("Firmware update applied.")).toBeVisible();
  expect(state.posts.some((post) => post.path === "/api/v1/ota/prepare")).toBe(
    true,
  );
  expect(state.posts.some((post) => post.path === "/api/v1/ota/chunk")).toBe(
    true,
  );

  await page.locator("#firmware-file").setInputFiles({
    name: "merged-binary.bin",
    mimeType: "application/octet-stream",
    buffer: Buffer.alloc(5 * 1024 * 1024, 0xaa),
  });
  await expect(
    page
      .getByRole("region", { name: "Firmware update needs attention" })
      .getByText(
        "Select the app firmware .bin for OTA, not merged-binary.bin, bootloader.bin, partition-table.bin, or a full flash image.",
      ),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Validate & upload" }),
  ).toBeDisabled();
});

// Integration positive: verifies Wi‑Fi scan pagination is merged before the
// selected network is saved through the bounded firmware API.
test("covers paged Wi‑Fi scan results before saving network credentials", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page, { pagedWifiScan: true });

  await page.goto("/#/system");
  await page.getByRole("button", { name: "Edit Wi‑Fi" }).click();
  await page.getByRole("button", { name: "Refresh" }).click();
  await expect(page.getByRole("radio", { name: /Lab_WiFi/ })).toBeVisible();
  await expect(page.getByRole("radio", { name: /Mesh_IoT/ })).toBeVisible();

  await page.getByRole("radio", { name: /Mesh_IoT/ }).click();
  await page.getByRole("button", { name: "Continue" }).click();
  await expect(
    page.getByPlaceholder("Select a network or enter SSID"),
  ).toHaveValue("Mesh_IoT");
  await page.locator("input[type='password']").first().fill("network-secret");
  await page.getByRole("button", { name: "Continue" }).click();
  await page.getByRole("button", { name: "Save Wi‑Fi" }).click();
  await expect(page.getByText("Wi‑Fi settings saved.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/network/wifi",
    body: {
      credentials: { ssid: "Mesh_IoT", password: "network-secret" },
    },
  });
});

// Integration positive/negative: confirms the reboot dialog gates the mutation
// until the user explicitly confirms the local system action.
test("gates system reboot behind an explicit confirmation dialog", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);

  const pageRebootButton = page
    .locator("#app")
    .getByRole("button", { name: "Reboot" });

  await page.goto("/");
  await pageRebootButton.click();
  await expect(
    page.getByRole("dialog", { name: "Reboot Core Pilot?" }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Cancel" }).click();
  await expect(
    page.getByRole("dialog", { name: "Reboot Core Pilot?" }),
  ).toBeHidden();
  expect(state.posts).toEqual([]);

  await pageRebootButton.click();
  await page
    .getByRole("dialog", { name: "Reboot Core Pilot?" })
    .getByRole("button", { name: "Reboot" })
    .click();
  await expect(page.getByText("Mutation accepted.")).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({ path: "/api/v1/system/reboot" });
});

// Integration negative: verifies empty or incomplete forms cannot issue a
// firmware API request, whether the action is disabled or rejected on submit.
test("blocks incomplete dashboard forms", async ({ page }) => {
  const state = await mockFirmwareApi(page);

  await page.goto("/#/rooms");
  await page.getByRole("button", { name: "Create room" }).first().click();
  await page.getByRole("button", { name: "Create room" }).last().click();
  await expect(page.getByText("Enter a room name.")).toBeVisible();
  await page.getByPlaceholder("Kitchen").fill(`${"😀".repeat(16)}a`);
  await page.getByRole("button", { name: "Create room" }).last().click();
  await expect(
    page.getByText("Room names must be 64 bytes or fewer."),
  ).toBeVisible();
  expect(
    state.posts.filter((request) => request.path === "/api/v1/rooms/upsert"),
  ).toHaveLength(0);

  await page.getByRole("button", { name: "Cancel", exact: true }).click();
  await page
    .getByRole("alertdialog", { name: "Discard unsaved changes?" })
    .getByRole("button", { name: "Discard changes" })
    .click();
  await page.goto("/#/devices/add");
  await page.getByRole("button", { name: "Validate Wi‑Fi setup code" }).click();
  await page.getByRole("button", { name: "Validate Wi‑Fi code" }).click();
  await expect(page.getByText("Enter a setup code.")).toBeVisible();
  expect(
    state.posts.filter(
      (request) => request.path === "/api/v1/matter/commission",
    ),
  ).toHaveLength(0);
  await page.getByRole("button", { name: "Cancel" }).click();
  await expect(
    page.getByRole("button", { name: "Validate Thread setup code" }),
  ).toBeEnabled();

  await page.goto("/#/ai");
  await expect(page.getByRole("button", { name: "Send" })).toBeDisabled();
  await page.getByRole("button", { name: "Replace provider" }).click();
  await page.getByLabel("Model").fill("");
  await expect(
    page.getByRole("button", { name: "Save provider" }),
  ).toBeEnabled();
  await page.getByRole("button", { name: "Save provider" }).click();
  expect(
    state.posts.filter((request) => request.path === "/api/v1/ai/provider"),
  ).toHaveLength(0);
  await page.getByRole("button", { name: "Cancel", exact: true }).click();
  await page.getByRole("button", { name: "Discard changes" }).click();

  await page.goto("/#/system");
  await expect(
    page.getByRole("button", { name: "Validate & upload" }),
  ).toBeDisabled();
  expect(state.posts).toEqual([]);
});

test("requires a physical tap and explicit status recheck after reboot", async ({
  page,
}) => {
  let state = "idle";
  let checks = 0;
  let mutations = 0;
  await page.route("**/api/v1/**", async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (route.request().method() !== "GET") mutations++;
    if (path === "/api/v1/auth") {
      checks++;
      return json(route, {
        authentication_required: true,
        bootstrap_state: state,
        session_authenticated: false,
      });
    }
    return route.fulfill({ status: 401, body: "{}" });
  });
  await page.context().addCookies([
    {
      name: "core_pilot_session",
      value: "old-session",
      url: "http://127.0.0.1:4173",
    },
  ]);
  await page.goto("/");
  await expect(
    page.getByRole("button", { name: "I have a code" }),
  ).toBeVisible();
  await expect(page.getByLabel("Bootstrap code")).toHaveCount(0);
  expect(checks).toBe(1);
  state = "open";
  await page.getByRole("button", { name: "I have a code" }).click();
  await expect(page.getByLabel("Bootstrap code")).toBeVisible();
  expect(checks).toBe(2);
  expect(mutations).toBe(0);
});

// Cookie issuance is simulated by the server, never by application storage.
async function mockCookieAccess(page: Page) {
  await mockFirmwareApi(page);
  const server = {
    open: false,
    valid: false,
    checks: 0,
    posts: [] as string[],
  };
  await page.route("**/api/v1/auth**", async (route) => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    if (request.method() === "GET") {
      server.checks++;
      return json(route, {
        authentication_required: true,
        bootstrap_state: server.open ? "open" : "idle",
        session_authenticated:
          server.valid &&
          request.headers().cookie?.includes("core_pilot_session=issued") ===
            true,
      });
    }
    server.posts.push(path);
    if (path === "/api/v1/auth/session" && server.open) {
      expect(request.postDataJSON()).toEqual({
        code: "0123456789abcdef",
        mode: "cookie",
      });
      server.open = false;
      server.valid = true;
      return route.fulfill({
        contentType: "application/json",
        headers: {
          "Set-Cookie":
            "core_pilot_session=issued; Path=/; HttpOnly; SameSite=Strict",
        },
        body: JSON.stringify({ authenticated: true, expires_in: 1800 }),
      });
    }
    return json(route, {}, 401);
  });
  return server;
}

async function enterAccessCode(page: Page) {
  await page.getByLabel("Bootstrap code").fill("0123456789abcdef");
  await page
    .getByRole("button", { name: "Unlock dashboard", exact: true })
    .click();
}

test("exchanges a physically issued code by keyboard on a narrow access gate", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 568 });
  const server = await mockCookieAccess(page);
  await page.goto("/");
  const check = page.getByRole("button", { name: "I have a code" });
  await expect(check).toBeVisible();
  await expect(page.getByLabel("Bootstrap code")).toHaveCount(0);
  server.open = true;
  await check.focus();
  await page.keyboard.press("Enter");
  const input = page.getByLabel("Bootstrap code");
  await expect(input).toBeVisible();
  await input.fill("0123456789abcdef");
  await input.press("Tab");
  await expect(
    page.getByRole("button", { name: "Unlock dashboard", exact: true }),
  ).toBeFocused();
  await page.keyboard.press("Enter");
  await expectHeading(page, "Home");
  expect(server.checks).toBe(2);
  expect(server.posts).toEqual(["/api/v1/auth/session"]);
  const cookies = await page.context().cookies();
  expect(
    cookies.find((cookie) => cookie.name === "core_pilot_session"),
  ).toMatchObject({ httpOnly: true, sameSite: "Strict" });
});

for (const failure of [
  "network",
  "malformed status",
  "malformed exchange",
] as const) {
  test(`fails closed without automatic auth issuance on ${failure}`, async ({
    page,
  }) => {
    const server = await mockCookieAccess(page);
    server.open = true;
    if (failure !== "malformed exchange") {
      await page.route("**/api/v1/auth", (route) =>
        failure === "network"
          ? route.abort("failed")
          : json(route, { session_authenticated: true }),
      );
    } else {
      await page.route("**/api/v1/auth/session", (route) =>
        json(route, { authenticated: true, expires_in: 1 }),
      );
    }
    await page.goto("/");
    if (failure === "malformed exchange") await enterAccessCode(page);
    await expect(
      page.getByText("Access temporarily unavailable", { exact: true }),
    ).toBeVisible();
    await expect(
      page.getByRole("heading", { name: "Home", exact: true }),
    ).toHaveCount(0);
    await expect(
      page.getByRole("button", { name: "Check access" }),
    ).toBeEnabled();
    expect(server.posts).toEqual([]);
  });
}

test("keeps independent browser cookies isolated and rejects them after server reboot", async ({
  page,
  browser,
}) => {
  const server = await mockCookieAccess(page);
  server.open = true;
  await page.goto("/");
  await enterAccessCode(page);
  await expectHeading(page, "Home");
  const other = await browser.newContext();
  try {
    const second = await other.newPage();
    // Both contexts consult the same simulated server state.
    await mockFirmwareApi(second);
    await second.route("**/api/v1/auth", (route) =>
      json(route, {
        authentication_required: true,
        bootstrap_state: "idle",
        session_authenticated:
          server.valid &&
          route
            .request()
            .headers()
            .cookie?.includes("core_pilot_session=issued") === true,
      }),
    );
    await second.goto("/");
    await expect(
      second.getByRole("button", { name: "I have a code" }),
    ).toBeVisible();
    await page.reload();
    await expectHeading(page, "Home");
    server.valid = false;
    await page.reload();
    await expect(
      page.getByRole("button", { name: "I have a code" }),
    ).toBeVisible();
    await expect(
      page.getByRole("heading", { name: "Home", exact: true }),
    ).toHaveCount(0);
    expect(
      (await page.context().cookies()).some(
        (cookie) => cookie.name === "core_pilot_session",
      ),
    ).toBe(true);
    expect(server.posts).toEqual(["/api/v1/auth/session"]);
  } finally {
    await other.close();
  }
});

test("clears local access even when logout cannot reach firmware", async ({
  page,
}) => {
  await mockCookieAccess(page);
  await page.route("**/api/v1/auth", (route) =>
    json(route, {
      authentication_required: true,
      bootstrap_state: "consumed",
      session_authenticated: true,
    }),
  );
  await page.route("**/api/v1/auth/logout", (route) => route.abort("failed"));
  await page.goto("/");
  await expectHeading(page, "Home");
  await page.getByRole("button", { name: "Log out" }).click();
  await expect(
    page.getByRole("heading", { name: "Unlock your dashboard" }),
  ).toBeVisible();
  await expect(page.getByText("Bench Lamp", { exact: true })).toHaveCount(0);
});

test("server OTA rejection preserves the normal dashboard without staging chunks", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.route("**/api/v1/ota/prepare", (route) =>
    json(route, {
      accepted: false,
      detail: "Signature verification rejected.",
    }),
  );
  await page.goto("/#/system");
  await page.locator("#firmware-file").setInputFiles({
    name: "core-pilot-0.2.0-ota.bin",
    mimeType: "application/octet-stream",
    buffer: Buffer.from("rejected image"),
  });
  await page.getByRole("button", { name: "Validate & upload" }).click();
  await expect(
    page.getByText("Signature verification rejected.").first(),
  ).toBeVisible();
  await expectHeading(page, "System");
  await expect(page.getByRole("button", { name: "Log out" })).toBeVisible();
  expect(
    state.posts.filter((post) => post.path.startsWith("/api/v1/ota/")),
  ).toEqual([]);
});

test("expires an authenticated session without automatically exchanging another code", async ({
  page,
}) => {
  await page.clock.install();
  const server = await mockCookieAccess(page);
  server.open = true;
  await page.goto("/");
  await enterAccessCode(page);
  await expectHeading(page, "Home");
  await page.clock.runFor(1800 * 1000);
  await expect(
    page.getByRole("heading", { name: "Unlock your dashboard" }),
  ).toBeVisible();
  await expect(page.getByText("Bench Lamp", { exact: true })).toHaveCount(0);
  expect(server.posts).toEqual(["/api/v1/auth/session"]);
});

test("a protected 401 discards cached rooms before another session unlocks", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/#/rooms");
  await expect(page.getByText("Lab", { exact: true }).first()).toBeVisible();
  await page.route("**/api/v1/groups", (route) => json(route, {}, 401));
  await page.getByRole("link", { name: "Groups", exact: true }).first().click();
  await expect(
    page.getByRole("heading", { name: "Unlock your dashboard" }),
  ).toBeVisible();
  state.rooms = [{ id: "new-room", name: "Replacement Room" }];
  await page.unroute("**/api/v1/groups");
  await page.route("**/api/v1/auth", (route) =>
    json(route, {
      authentication_required: true,
      bootstrap_state: "open",
      session_authenticated: false,
    }),
  );
  await page.route("**/api/v1/auth/session", (route) =>
    json(route, { authenticated: true, expires_in: 1800 }),
  );
  await page.getByRole("button", { name: "Check access" }).click();
  await enterAccessCode(page);
  await page.getByRole("link", { name: "Rooms", exact: true }).first().click();
  await expect(
    page.getByText("Replacement Room", { exact: true }).first(),
  ).toBeVisible();
  await expect(page.getByText("Lab", { exact: true })).toHaveCount(0);
});

for (const status of ["pending", "failed"] as const) {
  test(`retains a Matter device when asynchronous removal is ${status}`, async ({
    page,
  }) => {
    await mockFirmwareApi(page);
    await page.route("**/api/v1/matter/devices/remove", (route) => {
      expect(route.request().postDataJSON()).toEqual({
        device_id: "0000000000001234",
      });
      return json(route, {
        accepted: status === "pending",
        status,
        detail: `Removal ${status}.`,
        operation_id: 42,
      });
    });
    await page.goto("/#/devices");
    const device = page.locator('[data-device-id="matter-light-1"]');
    await expect(device).toBeVisible();
    await device.dispatchEvent("contextmenu", { clientX: 100, clientY: 100 });
    await page
      .getByRole("menuitem", { name: "Remove device… Bench Lamp" })
      .click();
    await page.getByRole("button", { name: "Remove device" }).click();
    await expect(page.getByText(`Removal ${status}.`).first()).toBeVisible();
    await expect(device).toBeVisible();
    await page.reload();
    await expect(device).toBeVisible();
  });
}

for (const status of ["pending", "failed"] as const) {
  test(`retains device inventory after a Matter command is ${status}`, async ({
    page,
  }) => {
    const state = await mockFirmwareApi(page);
    state.devices[0].capabilities = ["on_off"];
    await page.route("**/api/v1/devices", (route) =>
      route.request().method() === "GET"
        ? route.fallback()
        : json(route, {
            accepted: status === "pending",
            status,
            operation_id: 43,
            detail: `Command ${status}.`,
          }),
    );
    await page.goto("/#/devices");
    const device = page.locator('[data-device-id="matter-light-1"]');
    await device.dispatchEvent("contextmenu", { clientX: 100, clientY: 100 });
    const response = page.waitForResponse("**/api/v1/devices");
    await page.getByRole("menuitem", { name: "Turn on", exact: true }).click();
    await response;
    await expect(
      page
        .getByText(
          status === "pending"
            ? "Matter command queued. Power will update when the device reports state."
            : "Command failed.",
        )
        .first(),
    ).toBeVisible();
    await expect(device).toBeVisible();
  });
}

test("Devices accepted power command stays Unknown until a fresh report", async ({
  page,
  context,
}) => {
  const state = await mockFirmwareApi(page);
  state.devices[0].capabilities = ["on_off"];
  let reads = 0;
  let reported = false;
  let release!: () => void;
  const held = new Promise<void>((resolve) => {
    release = resolve;
  });
  await page.route("**/api/v1/devices", async (route) => {
    if (route.request().method() !== "GET") {
      expect(route.request().postDataJSON()).toEqual({
        device_id: "0000000000001234",
        endpoint_id: 1,
        on: true,
      });
      return json(route, {
        accepted: true,
        status: "pending",
        operation_id: 43,
        detail: "Power command queued.",
      });
    }
    reads += 1;
    if (reported) await held;
    return json(route, {
      devices: state.devices.map((device) => ({
        ...device,
        ...(reported ? { state: [{ OnOff: true }] } : {}),
      })),
      detail: "Reported inventory",
    });
  });
  await page.goto("/#/devices");
  const device = page.locator('[data-device-id="matter-light-1"]');
  await expect(device.getByText("Unknown", { exact: true })).toBeVisible();
  await device.dispatchEvent("contextmenu", { clientX: 100, clientY: 100 });
  await page.getByRole("menuitem", { name: "Turn on", exact: true }).click();
  const accepted = page.getByRole("region", {
    name: "Matter command request accepted",
    exact: true,
  });
  await expect(accepted).toContainText("Power command queued.");
  await expect(accepted).toContainText(
    "Power remains the last reported state, not the requested state.",
  );
  expect(reads).toBeGreaterThanOrEqual(2);
  await expect(device.getByText("Unknown", { exact: true })).toBeVisible();
  await expect(device.getByText("On", { exact: true })).toHaveCount(0);
  await expect(
    page.getByRole("region", { name: /command.*complet/i }),
  ).toHaveCount(0);
  await expect(
    device.locator("#power-matter-light-1-description"),
  ).toContainText("displayed state updates only when Bench Lamp reports it");
  reported = true;
  await context.setOffline(true);
  await expect(
    page.getByRole("status", { name: "Offline", exact: true }),
  ).toBeVisible();
  const requested = page.waitForRequest("**/api/v1/devices");
  await context.setOffline(false);
  await requested;
  try {
    await expect(device.getByText("Unknown", { exact: true })).toBeVisible();
    await expect(device.getByText("On", { exact: true })).toHaveCount(0);
  } finally {
    release();
  }
  await expect(device.getByText("On", { exact: true })).toBeVisible();
  await expect(device.getByText("Unknown", { exact: true })).toHaveCount(0);
  await expect(accepted).toBeVisible();
});

for (const accepted of [true, false]) {
  test(`Devices rename shows local ${accepted ? "accepted" : "error"} feedback`, async ({
    page,
  }) => {
    await mockFirmwareApi(page);
    await page.route("**/api/v1/devices", (route) => {
      if (route.request().method() === "GET") return route.fallback();
      expect(route.request().postDataJSON()).toEqual({
        device_id: "0000000000001234",
        endpoint_id: 1,
        name: "Desk Lamp",
      });
      return json(route, {
        accepted,
        status: accepted ? "pending" : "failed",
        detail: accepted ? "Rename queued." : "Rename unavailable.",
      });
    });
    await page.goto("/#/devices");
    const device = page.locator('[data-device-id="matter-light-1"]');
    await device.dispatchEvent("contextmenu", { clientX: 100, clientY: 100 });
    await page.getByRole("menuitem", { name: "Rename", exact: true }).click();
    await device
      .getByRole("textbox", { name: "Device name" })
      .fill("Desk Lamp");
    await device.getByRole("button", { name: "Save", exact: true }).click();
    await expect(
      page.getByRole("region", {
        name: accepted
          ? "Device rename request accepted"
          : "Device rename failed",
        exact: true,
      }),
    ).toContainText(accepted ? "Rename queued." : "Rename unavailable.");
    await expect(
      page.getByRole("region", { name: /rename.*complet/i }),
    ).toHaveCount(0);
    if (accepted)
      await expect(
        device.getByText("Bench Lamp", { exact: true }),
      ).toBeVisible();
    else
      await expect(
        device.getByRole("textbox", { name: "Device name" }),
      ).toHaveValue("Desk Lamp");
  });
}

test("enforces room names by UTF-8 bytes instead of character count", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.goto("/#/rooms");
  await page.getByRole("button", { name: "Create room" }).click();
  await page.getByPlaceholder("Kitchen").fill("界".repeat(22));
  await page
    .getByRole("button", { name: "Create room", exact: true })
    .last()
    .click();
  expect(
    state.posts.filter((post) => post.path === "/api/v1/rooms/upsert"),
  ).toEqual([]);
  await page.getByPlaceholder("Kitchen").fill("界".repeat(21) + "a");
  await page
    .getByRole("button", { name: "Create room", exact: true })
    .last()
    .click();
  await expect(
    page.getByText("界".repeat(21) + "a", { exact: true }).first(),
  ).toBeVisible();
  expect(state.posts.at(-1)?.body).toMatchObject({
    name: "界".repeat(21) + "a",
  });
});

for (const outcome of [
  "success",
  "rejected",
  "committed-response-lost",
] as const) {
  test(`OTA keeps one busy operation and one final toast: ${outcome}`, async ({
    page,
  }) => {
    const state = await mockFirmwareApi(page);
    const deferred = () => {
      let release!: () => void;
      const ready = new Promise<void>((resolve) => {
        release = resolve;
      });
      return { ready, release };
    };
    const prepare = deferred();
    const chunks = [deferred(), deferred()];
    const offsets: number[] = [];
    await page.route("**/api/v1/ota/prepare", async (route) => {
      state.system.ota_total_bytes = Number(
        route.request().postDataJSON().size_bytes,
      );
      state.system.ota_phase = "prepared";
      await prepare.ready;
      await json(route, {
        accepted: true,
        detail: "Intermediate prepare",
        upload_token: "a".repeat(64),
      });
    });
    await page.route("**/api/v1/ota/chunk", async (route) => {
      const body = route.request().postDataJSON();
      const index = offsets.length;
      offsets.push(body.offset);
      await chunks[index].ready;
      if (outcome === "rejected") {
        await json(route, { accepted: false, detail: "Final rejection" });
        return;
      }
      state.system.ota_progress_bytes =
        body.offset + Buffer.from(body.data_base64, "base64").length;
      state.system.ota_phase = body.final_chunk ? "activated" : "staging";
      if (outcome === "committed-response-lost" && index === 0) {
        await route.abort("failed");
        return;
      }
      await json(route, { accepted: true, detail: "Intermediate chunk" });
    });
    await page.goto("/#/system");
    await page.locator("#firmware-file").setInputFiles({
      name: "core-pilot-0.2.0-ota.bin",
      mimeType: "application/octet-stream",
      buffer: Buffer.alloc(2049, 0xaa),
    });
    await page.getByRole("button", { name: "Validate & upload" }).click();
    const busy = page.getByRole("button", { name: "Uploading…", exact: true });
    const toasts = page.locator("[data-sonner-toast]");
    await expect(busy).toBeDisabled();
    await expect(page.getByRole("progressbar")).toHaveCount(1);
    await expect(
      page
        .getByRole("region", { name: "Validating metadata" })
        .getByRole("progressbar"),
    ).toBeVisible();
    await expect(page.locator("#firmware-file")).toBeDisabled();
    await expect(
      page.getByRole("button", { name: "Remove selected firmware" }),
    ).toHaveCount(0);
    await expect(toasts).toHaveCount(0);
    expect(offsets).toEqual([]);
    prepare.release();
    await expect.poll(() => offsets.length).toBe(1);
    await expect(busy).toBeDisabled();
    await expect(toasts).toHaveCount(0);
    await expect(page.getByRole("progressbar")).toHaveCount(1);
    chunks[0].release();
    if (outcome !== "rejected") {
      await expect.poll(() => offsets.length).toBe(2);
      expect(offsets).toEqual([0, 2048]);
      await expect(busy).toBeDisabled();
      await expect(toasts).toHaveCount(0);
      await expect(page.getByRole("progressbar")).toHaveCount(1);
      chunks[1].release();
    }
    await expect(busy).toHaveCount(0);
    const upload = page.getByRole("button", { name: "Validate & upload" });
    if (outcome === "rejected") await expect(upload).toBeDisabled();
    else await expect(upload).toBeEnabled();
    await expect(page.locator("#firmware-file")).toBeEnabled();
    await expect(toasts).toHaveCount(1);
    await expect(toasts).toContainText(
      outcome === "rejected" ? "Final rejection" : "Firmware update applied.",
    );
    expect(offsets).toEqual(outcome === "rejected" ? [0] : [0, 2048]);
    if (outcome === "success") {
      await page.getByRole("button", { name: "Edit time settings" }).click();
      await page.getByRole("button", { name: "Save time settings" }).click();
      await expect(
        toasts.filter({ hasText: "Time settings saved." }),
      ).toHaveCount(1);
      await expect(
        toasts.filter({ hasText: "Firmware update applied." }),
      ).toHaveCount(1);
    }
  });
}

// Full-page scans intentionally retain contrast rules and portal content.
for (const [path, heading] of [
  ["/", "Home"],
  ["/#/activity", "Activity"],
  ["/#/devices", "Devices"],
  ["/#/devices/add", "Add Matter Device"],
  ["/#/devices/configure", "Device Setup"],
  ["/#/rooms", "Rooms"],
  ["/#/groups", "Groups"],
  ["/#/scenes", "Scenes"],
  ["/#/automations", "Automations"],
  ["/#/ai", "Optional Assistant"],
  ["/#/ai/tools", "Assistant Permissions"],
  ["/#/ai/prompts", "Assistant Prompts"],
  ["/#/system", "System"],
] as const) {
  test(`axe desktop loaded ${heading}`, async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 1000 });
    await mockFirmwareApi(page);
    await page.goto(path);
    await expectHeading(page, heading);

    await expectAccessible(page);
  });
}

for (const state of [
  "Home",
  "Activity",
  "access gate",
  "dialog",
  "sheet",
  "command palette",
  "menu",
] as const) {
  test(`axe narrow ${state}`, async ({ page }) => {
    await page.setViewportSize({ width: 320, height: 568 });
    if (state === "access gate") {
      const server = await mockCookieAccess(page);
      await page.goto("/");
      await expect(
        page.getByRole("button", { name: "I have a code" }),
      ).toBeVisible();
      await expectAccessible(page);
      server.open = true;
      await page.getByRole("button", { name: "I have a code" }).click();
      await expect(page.getByLabel("Bootstrap code")).toBeVisible();
      await expectAccessible(page);
      return;
    }
    await mockFirmwareApi(page);
    await page.goto(
      state === "Activity"
        ? "/#/activity"
        : state === "sheet"
          ? "/#/groups"
          : state === "menu"
            ? "/#/devices"
            : "/",
    );
    if (state === "dialog") {
      await page
        .locator("#app")
        .getByRole("button", { name: "Reboot" })
        .click();
      await expect(
        page.getByRole("dialog", { name: "Reboot Core Pilot?" }),
      ).toBeVisible();
    } else if (state === "sheet") {
      await page.getByRole("button", { name: "Create group" }).first().click();
      await expect(
        page.getByRole("dialog", { name: "Create group" }),
      ).toBeVisible();
    } else if (state === "command palette") {
      await page.getByRole("button", { name: /open command palette/i }).click();
      await expect(
        page.getByRole("dialog", { name: "Command palette" }),
      ).toBeVisible();
    } else if (state === "menu") {
      await page
        .getByText("Bench Lamp", { exact: true })
        .click({ button: "right" });
      await expect(page.getByRole("menu")).toBeVisible();
    } else {
      await expectHeading(page, state);
    }
    await expectAccessible(page);
  });
}

test("computed cursors distinguish actionable, disabled, text, and drag controls", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/groups");
  const sharedButton = page
    .getByRole("button", { name: "Create group" })
    .first();
  await expect(sharedButton).toHaveCSS("cursor", "pointer");
  await page
    .getByRole("link", { name: "Home", exact: true })
    .first()
    .evaluate((element) => element.setAttribute("data-cursor-link", ""));
  await expect(page.locator("[data-cursor-link]")).toHaveCSS(
    "cursor",
    "pointer",
  );
  await sharedButton.click();
  const sheet = page.getByRole("dialog", { name: "Create group" });
  await expect(sheet.getByPlaceholder("Downstairs lights")).toHaveCSS(
    "cursor",
    "text",
  );
  // Exercise the shared component's disabled styling independently of form validation.
  await sheet
    .getByRole("button", { name: "Create group", exact: true })
    .evaluate((element) => {
      (element as HTMLButtonElement).disabled = true;
    });
  await expect(
    sheet.getByRole("button", { name: "Create group", exact: true }),
  ).toBeDisabled();
  await expect(
    sheet.getByRole("button", { name: "Create group", exact: true }),
  ).not.toHaveCSS("cursor", "pointer");
  await expect(sheet.getByRole("button", { name: /^Devices/ })).toHaveCSS(
    "cursor",
    "pointer",
  );

  await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
  await expect(sheet).toHaveCount(0);

  // Isolate base semantic CSS from component utility classes, including native
  // and ARIA controls that may not all occur together on a dashboard route.
  await page.evaluate(() => {
    const fixture = document.createElement("section");
    fixture.id = "cursor-contract";
    fixture.innerHTML = `
      <button>Raw button</button><button disabled>Disabled button</button>
      <a href="#/rooms">Raw link</a>
      <select aria-label="Native select"><option>One</option></select>
      <input type="checkbox" aria-label="Native checkbox">
      <input type="text" aria-label="Text input">
      <div role="combobox" tabindex="0" aria-label="Combobox"></div>
      <div role="menuitem" tabindex="0">Menu item</div>
      <div role="switch" tabindex="0" aria-checked="false">Switch</div>
      <div role="checkbox" tabindex="0" aria-checked="false">Checkbox</div>
      <div role="tab" tabindex="0">Tab</div>
      <div role="button" tabindex="0" aria-disabled="true">Disabled role button</div>
      <button class="cursor-grab" draggable="true">Drag handle</button>`;
    document.body.append(fixture);
  });
  const fixture = page.locator("#cursor-contract");
  for (const selector of [
    "button:not(:disabled):not([draggable])",
    "a[href]",
    "select",
    'input[type="checkbox"]',
    '[role="combobox"]',
    '[role="menuitem"]',
    '[role="switch"]',
    '[role="checkbox"]',
    '[role="tab"]',
  ]) {
    await expect
      .soft(fixture.locator(selector), selector)
      .toHaveCSS("cursor", "pointer");
  }
  await expect
    .soft(fixture.locator("button:disabled"))
    .not.toHaveCSS("cursor", "pointer");
  await expect
    .soft(fixture.locator('[aria-disabled="true"]'))
    .not.toHaveCSS("cursor", "pointer");
  await expect
    .soft(fixture.locator('input[type="text"]'))
    .toHaveCSS("cursor", "text");
  await expect
    .soft(fixture.getByRole("button", { name: "Drag handle" }))
    .toHaveCSS("cursor", "grab");
});

test("nonmodal context menu exits with Escape and Tab while confirmation traps focus", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/devices");
  const device = page.locator('[data-device-id="matter-light-1"]');
  const openMenu = async () => {
    await device.click({ button: "right" });
    await expect(page.getByRole("menu")).toBeVisible();
    await expect(page.locator("#app")).not.toHaveAttribute(
      "aria-hidden",
      "true",
    );
    await expect(
      page.getByRole("menu").locator("[data-focus-trap]"),
    ).toHaveCount(0);
  };
  await openMenu();
  await page.keyboard.press("Escape");
  await expect(page.getByRole("menu")).toHaveCount(0);
  await openMenu();
  await page
    .getByRole("menuitem", { name: "Remove device… Bench Lamp", exact: true })
    .click();
  const dialog = page.getByRole("alertdialog");
  await expect(dialog).toBeVisible();
  await expect(page.locator("#app")).toHaveAttribute("aria-hidden", "true");
  for (const key of ["Tab", "Shift+Tab", "Tab", "Tab"]) {
    await page.keyboard.press(key);
    await expect
      .poll(() =>
        dialog.evaluate((element) => element.contains(document.activeElement)),
      )
      .toBe(true);
  }
  await dialog.getByRole("button", { name: "Cancel" }).click();
  await expect(dialog).toHaveCount(0);
  await openMenu();
  await page.keyboard.press("Tab");
  await expect(page.getByRole("menu")).toHaveCount(0);
  await expect
    .poll(() =>
      page.evaluate(
        () => document.activeElement?.closest('[role="menu"]') === null,
      ),
    )
    .toBe(true);
});

for (const key of ["Enter", "Space"]) {
  test(`device actions dropdown supports ${key} and restores focus`, async ({
    page,
  }) => {
    await mockFirmwareApi(page);
    await page.goto("/#/devices");
    const trigger = page.getByRole("button", {
      name: "Device actions for Bench Lamp",
    });
    await trigger.focus();
    await page.keyboard.press(key);
    const menu = page.getByRole("menu");
    await expect(menu).toBeVisible();
    const items = await menu.getByRole("menuitem").allTextContents();
    await page.keyboard.press("Escape");
    await expect(trigger).toBeFocused();
    await page
      .locator('[data-device-id="matter-light-1"]')
      .click({ button: "right" });
    await expect(menu).toBeVisible();
    expect(await menu.getByRole("menuitem").allTextContents()).toEqual(items);
  });
}

test("narrow touch device actions expose accessible removal confirmation", async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 320, height: 568 },
    hasTouch: true,
  });
  const page = await context.newPage();
  try {
    await mockFirmwareApi(page);
    await page.goto("http://127.0.0.1:4173/#/devices");
    await page
      .getByRole("button", { name: "Device actions for Bench Lamp" })
      .tap();
    await expect(page.getByRole("menu")).toBeVisible();
    await expectAccessible(page);
    await page
      .getByRole("menuitem", { name: "Remove device… Bench Lamp" })
      .tap();
    const confirm = page.getByRole("button", {
      name: "Remove device",
      exact: true,
    });
    await expect(confirm).toBeInViewport();
    await expect(
      page.getByRole("button", { name: "Cancel", exact: true }),
    ).toBeInViewport();
  } finally {
    await context.close();
  }
});

for (const [platform, label, shortcut] of [
  ["MacIntel", "⌘K", "Meta+K"],
  ["Win32", "Ctrl+K", "Control+K"],
]) {
  test(`command shortcut follows ${platform}`, async ({ page }) => {
    await page.addInitScript(
      (platform) =>
        Object.defineProperty(navigator, "platform", { get: () => platform }),
      platform,
    );
    await mockFirmwareApi(page);
    await page.goto("/");
    const trigger = page.getByRole("button", {
      name: `Search ${label} — open command palette`,
    });
    await expect(trigger).toBeVisible();
    await expect(trigger).toHaveAttribute("aria-keyshortcuts", shortcut);
    await expectAccessible(page);
    await page.keyboard.press(shortcut);
    await expect(
      page.getByRole("dialog", { name: "Command palette" }),
    ).toBeVisible();
  });
}

for (const [model, placeholder] of [
  ["room", "Kitchen"],
  ["group", "Downstairs lights"],
  ["scene", "Movie night"],
  ["automation", "Wind-down routine"],
]) {
  test(`${model} sheet guards dirty cancellation and beforeunload`, async ({
    page,
  }) => {
    await mockFirmwareApi(page);
    await page.goto(`/#/${model}s`);
    const open = page
      .getByRole("button", { name: `Create ${model}`, exact: true })
      .first();
    await open.click();
    await page.getByRole("button", { name: "Cancel", exact: true }).click();
    await expect(page.getByRole("alertdialog")).toHaveCount(0);
    await expect(page.getByRole("dialog")).toHaveCount(0);
    await open.click();
    const field = page.getByPlaceholder(placeholder);
    await field.fill("Unsaved draft");
    expect(
      await page.evaluate(() => {
        const event = new Event("beforeunload", { cancelable: true });
        window.dispatchEvent(event);
        return event.defaultPrevented;
      }),
    ).toBe(true);
    await page.getByRole("button", { name: "Cancel", exact: true }).click();
    const warning = page.getByRole("alertdialog", {
      name: "Discard unsaved changes?",
    });
    await expect(warning).toBeVisible();
    await expect(
      warning.getByRole("button", { name: "Keep editing" }),
    ).toBeFocused();
    if (model === "room") await expectAccessible(page);
    await warning.getByRole("button", { name: "Keep editing" }).click();
    await expect(field).toHaveValue("Unsaved draft");
    await page.getByRole("button", { name: "Cancel", exact: true }).click();
    await warning.getByRole("button", { name: "Discard changes" }).click();
    await expect(page.getByRole("dialog")).toHaveCount(0);
    expect(
      await page.evaluate(() => {
        const event = new Event("beforeunload", { cancelable: true });
        window.dispatchEvent(event);
        return event.defaultPrevented;
      }),
    ).toBe(false);
  });
}

test("devices loading boundary hides decorative table until inventory resolves", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  let release!: () => void;
  const held = new Promise<void>((resolve) => {
    release = resolve;
  });
  await page.route("**/api/v1/devices", async (route) => {
    await held;
    await route.fallback();
  });
  await page.goto("/#/devices");
  await expect(
    page.getByRole("status").filter({ hasText: "Loading devices" }),
  ).toBeVisible();
  await expect(page.getByRole("table")).toHaveCount(0);
  release();
  await expect(page.getByRole("table")).toBeVisible();
});

test("device URL filters preserve unknown parameters and browser history", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto(
    "/#/devices?q=Bench&protocol=matter&availability=online&density=compact&future=keep",
  );
  await expect(
    page.getByRole("searchbox", { name: "Search devices" }),
  ).toHaveValue("Bench");
  await expect(
    page.getByRole("button", { name: "Compact", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await expect(page.getByText("Bench Lamp", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Comfortable", exact: true }).click();
  await expect(page).toHaveURL(/future=keep/);
  await expect(page).not.toHaveURL(/density=/);
  await page.goBack();
  await expect(
    page.getByRole("button", { name: "Compact", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await page.goForward();
  await expect(
    page.getByRole("button", { name: "Comfortable", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
});

test("activity URL filters validate values and expose a visible live count", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto(
    "/#/activity?eventType=wifi_join_failed&severity=error&category=network&future=keep",
  );
  const summary = page.locator("#activity-list-summary");
  await expect(summary).toBeVisible();
  await expect(summary).toHaveAttribute("role", "status");
  await expect(
    page.getByText("Wi-Fi join failed.", { exact: true }),
  ).toBeVisible();
  await page.goto(
    `/#/activity?eventType=${"x".repeat(65)}&severity=invalid&category=invalid&future=keep`,
  );
  await expect(page).toHaveURL(/activity\?future=keep$/);
  await expect(summary).toContainText("3 loaded");
});

test("assistant conversation log remains stable when a completed reply is appended", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  let release!: () => void;
  const held = new Promise<void>((resolve) => {
    release = resolve;
  });
  await page.route("**/api/v1/ai/chat", async (route) => {
    await held;
    await route.fallback();
  });
  await page.goto("/#/ai");
  const log = page.getByRole("log", { name: "Assistant conversation" });
  await expect(log).toHaveAttribute("aria-live", "polite");
  await expect(log).toHaveAttribute("aria-relevant", "additions text");
  const original = await log.elementHandle();
  await page
    .getByPlaceholder(
      "Ask Core Pilot to check status, list devices, scenes, or automations…",
    )
    .fill("Check status");
  await page.getByRole("button", { name: "Send", exact: true }).click();
  await expect(log.getByText("Core Pilot status looks healthy.")).toHaveCount(
    0,
  );
  release();
  await expect(log.getByText("Core Pilot status looks healthy.")).toBeVisible();
  expect(
    await log.evaluate((element, saved) => element === saved, original),
  ).toBe(true);
});

for (const model of ["rooms", "groups", "scenes", "automations"]) {
  test(`${model} cached content survives refresh errors and explicit retry`, async ({
    page,
  }) => {
    await mockFirmwareApi(page);
    await page.goto(`/#/${model}`);
    const content = page.getByRole("group", { name: `${model} content` });
    await expect(content.getByRole("heading").first()).toBeVisible();
    const original = await content.elementHandle();
    let failed = true;
    await page.route(`**/api/v1/${model}`, async (route) => {
      if (failed)
        await json(route, { detail: "Storage temporarily unavailable" }, 503);
      else await route.fallback();
    });
    await page.context().setOffline(true);
    await expect(
      page.getByRole("status", { name: "Offline", exact: true }),
    ).toBeVisible();
    await page.context().setOffline(false);
    await expect(content.getByRole("alert")).toContainText(
      `Could not refresh ${model}`,
    );
    expect(
      await content.evaluate((element, saved) => element === saved, original),
    ).toBe(true);
    await expect(content.getByRole("heading").first()).toBeVisible();
    failed = false;
    await content
      .getByRole("button", { name: `Retry ${model}`, exact: true })
      .click();
    await expect(content.getByRole("alert")).toHaveCount(0);
  });
}

test("activity older error retains viewport and requires explicit retry", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page, { activityPageSize: 20 });
  state.activity = Array.from({ length: 40 }, (_, index) => ({
    sequence: 100 - index,
    unix_seconds: 1_700_000_000 - index,
    uptime_seconds: 500 - index,
    kind: "configuration_saved",
    detail: `Retained history ${index + 1}`,
  }));
  let failed = true;
  let olderReads = 0;
  let releaseRetry!: () => void;
  const heldRetry = new Promise<void>((resolve) => {
    releaseRetry = resolve;
  });
  await page.route("**/api/v1/activity/page", async (route) => {
    if (route.request().postDataJSON().before_sequence != null) {
      olderReads++;
      if (failed) {
        await json(route, { detail: "Older history unavailable" }, 503);
        return;
      }
      await heldRetry;
    }
    await route.fallback();
  });
  await page.goto("/#/activity");
  const viewport = page.getByRole("region", {
    name: "Scrollable activity history",
  });
  await expect(page.locator("#activity-list-summary")).toContainText(
    "20 loaded",
  );
  const original = await viewport.elementHandle();
  await viewport.evaluate((element) => {
    element.scrollTop = element.scrollHeight;
    element.dispatchEvent(new Event("scroll"));
  });
  await expect(
    page.getByRole("button", { name: "Retry older", exact: true }),
  ).toBeVisible();
  const readsAfterError = olderReads;
  const scroll = await viewport.evaluate((element) => element.scrollTop);
  for (let index = 0; index < 3; index++) {
    await viewport.evaluate((element) =>
      element.dispatchEvent(new Event("scroll")),
    );
    await page.getByRole("button", { name: "Severity" }).click();
    await page.keyboard.press("Escape");
  }
  expect(olderReads).toBe(readsAfterError);
  expect(
    await viewport.evaluate((element, saved) => element === saved, original),
  ).toBe(true);
  // Virtual row measurements may adjust the offset without resetting history.
  expect(scroll).toBeGreaterThan(0);
  expect(
    await viewport.evaluate((element) => element.scrollTop),
  ).toBeGreaterThan(0);
  failed = false;
  await page.getByRole("button", { name: "Retry older", exact: true }).click();
  await expect.poll(() => olderReads).toBe(readsAfterError + 1);
  // Measure a settled, fully visible row while the successful retry is held.
  await viewport.evaluate(async (element) => {
    element.scrollTop = 200;
    element.dispatchEvent(new Event("scroll"));
    await new Promise<void>((resolve) =>
      requestAnimationFrame(() => requestAnimationFrame(() => resolve())),
    );
  });
  const anchor = await viewport.evaluate((element) => {
    const top = element.getBoundingClientRect().top;
    const row = Array.from(element.querySelectorAll("li")).find(
      (row) => row.getBoundingClientRect().top >= top,
    )!;
    return {
      label: row.textContent!,
      offset: row.getBoundingClientRect().top - top,
      scroll: element.scrollTop,
    };
  });
  releaseRetry();
  await expect(page.locator("#activity-list-summary")).toContainText(
    "40 loaded",
  );
  expect(
    await viewport.evaluate((element, saved) => element === saved, original),
  ).toBe(true);
  await expect
    .poll(() => viewport.evaluate((element) => element.scrollTop))
    .toBe(anchor.scroll);
  await expect
    .poll(() =>
      viewport
        .getByRole("listitem")
        .filter({ hasText: anchor.label })
        .evaluate(
          (row) =>
            row.getBoundingClientRect().top -
            row.closest('[role="region"]')!.getBoundingClientRect().top,
        ),
    )
    .toBeCloseTo(anchor.offset, 0);
});

test("activity background error retains cached history", async ({ page }) => {
  await mockFirmwareApi(page);
  await page.goto("/#/activity");
  const viewport = page.getByRole("region", {
    name: "Scrollable activity history",
  });
  await expect(
    viewport.getByText("Wi-Fi join failed.", { exact: true }),
  ).toBeVisible();
  const original = await viewport.elementHandle();
  await page.route("**/api/v1/activity/page", (route) =>
    json(route, { detail: "History unavailable" }, 503),
  );
  await page.context().setOffline(true);
  await expect(
    page.getByRole("status", { name: "Offline", exact: true }),
  ).toBeVisible();
  await page.context().setOffline(false);
  await expect(page.getByRole("alert")).toContainText(
    "Activity refresh failed. Showing cached activity.",
  );
  await expect(
    viewport.getByText("Wi-Fi join failed.", { exact: true }),
  ).toBeVisible();
  expect(
    await viewport.evaluate((element, saved) => element === saved, original),
  ).toBe(true);
});

async function expectDraftDirty(page: Page, dirty: boolean) {
  await expect
    .poll(() =>
      page.evaluate(() => {
        const event = new Event("beforeunload", { cancelable: true });
        window.dispatchEvent(event);
        return event.defaultPrevented;
      }),
    )
    .toBe(dirty);
}

for (const [model, name, placeholder] of [
  ["room", "Lab", "Kitchen"],
  ["group", "Bench lights", "Downstairs lights"],
  ["scene", "Evening", "Movie night"],
  ["automation", "Morning", "Wind-down routine"],
]) {
  test(`${model} existing edit stays pristine after name revert`, async ({
    page,
  }) => {
    const state = await mockFirmwareApi(page);
    await page.goto(`/#/${model}s`);
    const edit = page.getByRole("button", {
      name: `Edit ${name}`,
      exact: true,
    });
    await edit.click();
    const sheet = page.getByRole("dialog");
    const field = sheet.getByPlaceholder(placeholder);
    await expect(field).toHaveValue(name);
    await expectDraftDirty(page, false);
    await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
    await expect(sheet).toHaveCount(0);
    await expect(page.getByRole("alertdialog")).toHaveCount(0);

    await edit.click();
    await field.fill(`${name} changed`);
    await expectDraftDirty(page, true);
    await field.fill(name);
    await expectDraftDirty(page, false);
    await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
    await expect(sheet).toHaveCount(0);
    await expect(page.getByRole("alertdialog")).toHaveCount(0);
    expect(state.posts).toHaveLength(0);
  });
}

test("group member change and revert restores the pristine edit baseline", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/groups");
  await page
    .getByRole("button", { name: "Edit Bench lights", exact: true })
    .click();
  const sheet = page.getByRole("dialog");
  await expectDraftDirty(page, false);
  await sheet.getByRole("button", { name: /^Devices/ }).click();
  await page.getByRole("option", { name: "Bench Lamp" }).click();
  await page.keyboard.press("Escape");
  await expectDraftDirty(page, true);
  await sheet.getByRole("button", { name: /^Devices/ }).click();
  await page.getByRole("option", { name: "Bench Lamp" }).click();
  await page.keyboard.press("Escape");
  await expectDraftDirty(page, false);
  await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
  await expect(sheet).toHaveCount(0);
  await expect(page.getByRole("alertdialog")).toHaveCount(0);
});

for (const model of ["scene", "automation"]) {
  test(`${model} action revert clears dirty while reordered actions persist and reopen pristine`, async ({
    page,
  }) => {
    const state = await mockFirmwareApi(page);
    const actions = ["notify|Baseline message", "delay|1000"];
    if (model === "scene") state.scenes[0].actions = actions;
    else state.automations[0].actions = actions;
    const name = model === "scene" ? "Evening" : "Morning";
    await page.goto(`/#/${model}s`);
    const edit = page.getByRole("button", {
      name: `Edit ${name}`,
      exact: true,
    });
    const sheet = page.getByRole("dialog");
    await edit.click();
    await expectDraftDirty(page, false);
    if (model === "automation") {
      await sheet.getByRole("button", { name: "Continue" }).click();
      await sheet.getByRole("button", { name: "Continue" }).click();
    }
    const message = sheet.getByLabel("Message");
    await message.fill("Changed message");
    await expectDraftDirty(page, true);
    await message.fill("Baseline message");
    await expectDraftDirty(page, false);
    await sheet.getByRole("button", { name: "Move action 2 up" }).click();
    await expectDraftDirty(page, true);
    await sheet.getByRole("button", { name: "Move action 2 up" }).click();
    await expectDraftDirty(page, false);
    await sheet.getByRole("button", { name: "Move action 2 up" }).click();
    await expectDraftDirty(page, true);
    await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
    const warning = page.getByRole("alertdialog", {
      name: "Discard unsaved changes?",
    });
    await expect(warning).toBeVisible();
    await warning.getByRole("button", { name: "Keep editing" }).click();
    if (model === "automation")
      await sheet.getByRole("button", { name: "Continue" }).click();
    await sheet
      .getByRole("button", { name: `Update ${model}`, exact: true })
      .click();
    await expect(sheet).toHaveCount(0);
    await expectDraftDirty(page, false);
    expect(state.posts.at(-1)).toMatchObject({
      path: `/api/v1/${model}s/upsert`,
      body: { actions: ["delay|1000", "notify|Baseline message"] },
    });
    await edit.click();
    await expectDraftDirty(page, false);
    if (model === "automation") {
      await sheet.getByRole("button", { name: "Continue" }).click();
      await sheet.getByRole("button", { name: "Continue" }).click();
    }
    await expect(
      sheet
        .getByRole("list", { name: "Ordered actions" })
        .getByRole("listitem")
        .first()
        .getByLabel("Milliseconds"),
    ).toHaveValue("1000");
    await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
    await expect(sheet).toHaveCount(0);
    await expect(page.getByRole("alertdialog")).toHaveCount(0);
  });
}

test("room failed save preserves draft and successful retry closes without warning", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/rooms");
  await page
    .getByRole("button", { name: "Create room", exact: true })
    .first()
    .click();
  const field = page.getByPlaceholder("Kitchen");
  await field.fill("Draft kitchen");
  const fail = (route: Route) =>
    json(route, { accepted: false, detail: "Save rejected" });
  await page.route("**/api/v1/rooms/upsert", fail);
  await page
    .getByRole("dialog")
    .getByRole("button", { name: "Create room", exact: true })
    .click();
  await expect(page.getByText("Save rejected", { exact: true })).toBeVisible();
  await expect(field).toHaveValue("Draft kitchen");
  await expectDraftDirty(page, true);
  await page.unroute("**/api/v1/rooms/upsert", fail);
  await page
    .getByRole("dialog")
    .getByRole("button", { name: "Create room", exact: true })
    .click();
  await expect(page.getByRole("dialog")).toHaveCount(0);
  await expect(page.getByRole("alertdialog")).toHaveCount(0);
  await expectDraftDirty(page, false);
  await page
    .getByRole("button", { name: "Edit Draft kitchen", exact: true })
    .click();
  await expect(field).toHaveValue("Draft kitchen");
  await expectDraftDirty(page, false);
  await page.getByRole("button", { name: "Cancel", exact: true }).click();
  await expect(page.getByRole("dialog")).toHaveCount(0);
  await expect(page.getByRole("alertdialog")).toHaveCount(0);
});

test("dirty room internal navigation waits for discard confirmation", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/rooms");
  await page
    .getByRole("button", { name: "Create room", exact: true })
    .first()
    .click();
  await page.getByPlaceholder("Kitchen").fill("Navigation draft");
  // Drive a real router link from inside the modal, without bypassing its focus trap.
  await page.getByRole("dialog").evaluate((dialog) => {
    const link = document.createElement("a");
    link.href = "#/groups";
    link.textContent = "Test internal navigation";
    dialog.append(link);
    link.click();
    link.remove();
  });
  const warning = page.getByRole("alertdialog", {
    name: "Discard unsaved changes?",
  });
  await expect(warning).toBeVisible();
  await expect(page).toHaveURL(/#\/rooms$/);
  await warning.getByRole("button", { name: "Discard changes" }).click();
  await expectHeading(page, "Groups");
});

for (const [model, name, dependency, reason] of [
  [
    "room",
    "Lab",
    "devices",
    "Device assignments could not be verified. Refresh devices before deleting this room.",
  ],
  [
    "scene",
    "Evening",
    "automations",
    "Automation references could not be verified. Refresh automations before deleting this scene.",
  ],
]) {
  test(`${model} open delete confirmation blocks when required inventory refresh fails`, async ({
    page,
    context,
  }) => {
    const state = await mockFirmwareApi(page);
    for (const device of state.devices) device.room = "";
    state.automations = [];
    await page.goto(`/#/${model}s`);
    await page
      .getByRole("button", { name: `Delete ${name}`, exact: true })
      .click();
    const dialog = page.getByRole("alertdialog", { name: `Delete ${name}?` });
    await expect(
      dialog.getByRole("button", { name: `Delete ${model}`, exact: true }),
    ).toBeEnabled();
    let release!: () => void;
    const held = new Promise<void>((resolve) => {
      release = resolve;
    });
    await page.route(`**/api/v1/${dependency}`, async (route) => {
      await held;
      await json(route, { detail: "Required inventory unavailable" }, 400);
    });
    // Isolate the dependency failure before the independent model refresh completes.
    let releaseModel!: () => void;
    const heldModel = new Promise<void>((resolve) => {
      releaseModel = resolve;
    });
    await page.route(`**/api/v1/${model}s`, async (route) => {
      await heldModel;
      await route.fallback();
    });
    await context.setOffline(true);
    const requested = page.waitForRequest(`**/api/v1/${dependency}`);
    await context.setOffline(false);
    await requested;
    await expect(dialog).toBeVisible();
    release();
    await expect(dialog.getByRole("status")).toHaveText(reason);
    const confirm = dialog.getByRole("button", {
      name: "Resolve dependencies first",
      exact: true,
    });
    await expect(confirm).toBeDisabled();
    // Synthetic dispatch also exercises confirm()'s guard, not just native disabled input.
    await confirm.dispatchEvent("click");
    await expect(dialog).toBeVisible();
    expect(
      state.posts.filter((post) => post.path === `/api/v1/${model}s/delete`),
    ).toEqual([]);
    await dialog.getByRole("button", { name: "Cancel", exact: true }).click();
    await expect(dialog).toHaveCount(0);
    expect(
      state.posts.filter((post) => post.path === `/api/v1/${model}s/delete`),
    ).toEqual([]);
    releaseModel();
  });
}

test("activity filter Back and Forward restore selected results and unknown parameters", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/activity?future=keep");
  await page.getByRole("button", { name: "Severity" }).click();
  await page.getByRole("option", { name: "Error", exact: true }).click();
  await expect(page).toHaveURL(/severity=error/);
  await expect(
    page.getByRole("button", { name: /Severity Error/ }),
  ).toBeVisible();
  await expect(page.locator("#activity-list-summary")).toContainText(
    "1 matching",
  );
  await page.goBack();
  await expect(page).not.toHaveURL(/severity=/);
  await expect(page.locator("#activity-list-summary")).toContainText(
    "3 matching",
  );
  await page.goForward();
  await expect(
    page.getByRole("button", { name: /Severity Error/ }),
  ).toBeVisible();
  await expect(page.locator("#activity-list-summary")).toContainText(
    "1 matching",
  );
  await expect(page).toHaveURL(/future=keep/);
});

test("devices search and availability update URL and restore through history", async ({
  page,
}) => {
  await mockFirmwareApi(page);
  await page.goto("/#/devices?future=keep");
  const search = page.getByRole("searchbox", { name: "Search devices" });
  const availability = page.getByRole("combobox", {
    name: "Availability",
    exact: true,
  });
  await search.fill("Bench");
  await expect(page).toHaveURL(/q=Bench/);
  await availability.selectOption("offline");
  await expect(page).toHaveURL(/availability=offline/);
  await expect(
    page.getByText("No devices match these filters.", { exact: true }),
  ).toBeVisible();
  await page.goBack();
  await expect(search).toHaveValue("Bench");
  await expect(availability).toHaveValue("all");
  await expect(page.getByText("Bench Lamp", { exact: true })).toBeVisible();
  await page.goForward();
  await expect(availability).toHaveValue("offline");
  await expect(search).toHaveValue("Bench");
  await expect(
    page.getByText("No devices match these filters.", { exact: true }),
  ).toBeVisible();
  await expect(page).toHaveURL(/future=keep/);
});

test("assistant regression provider metadata, credential scope and dirty discard", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  Object.assign(state.ai, {
    provider: {
      backend: "anthropic_compatible",
      auth_type: "api_key",
      base_url: "https://api.anthropic.com/v1",
      model: "saved-model",
      max_output_tokens: 2048,
      timeout_ms: 45000,
    },
  });
  await page.goto("/#/ai");
  const open = page.getByRole("button", { name: "Update provider" });
  await open.click();
  const sheet = page.getByRole("dialog", { name: "Assistant provider" });
  await expect(sheet.getByLabel("Model", { exact: true })).toHaveValue(
    "saved-model",
  );
  await expect(sheet.getByLabel("Max tokens")).toHaveValue("2048");
  await expect(sheet.getByLabel("Timeout ms")).toHaveValue("45000");
  await expect(sheet.getByLabel("Base URL")).toHaveValue(
    "https://api.anthropic.com/v1",
  );
  await expect(sheet.getByLabel("Base URL")).toHaveAttribute("readonly", "");
  const key = sheet.getByLabel("API key", { exact: true });
  await expect(key).toHaveValue("");
  await expectDraftDirty(page, false);
  await key.fill("never-cross-provider");
  await sheet.getByRole("button", { name: "OpenAI", exact: true }).click();
  await expect(key).toHaveValue("");
  await key.fill("never-cross-back");
  await sheet.getByRole("button", { name: "Anthropic", exact: true }).click();
  await expect(key).toHaveValue("");
  await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
  await page.getByRole("button", { name: "Keep editing" }).click();
  await expect(sheet).toBeVisible();
  await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
  await page.getByRole("button", { name: "Discard changes" }).click();
  await expect(sheet).toHaveCount(0);
  await open.click();
  await expect(key).toHaveValue("");
  await expectDraftDirty(page, false);
  await sheet.getByRole("button", { name: "Cancel", exact: true }).click();
  const chat = page.getByPlaceholder(
    "Ask Core Pilot to check status, list devices, scenes, or automations…",
  );
  await chat.fill("Unsent draft");
  await expectDraftDirty(page, true);
  await page.getByRole("link", { name: "Saved context", exact: true }).click();
  await page.getByRole("button", { name: "Keep editing" }).click();
  await expect(chat).toHaveValue("Unsent draft");
  await page.getByRole("link", { name: "Saved context", exact: true }).click();
  await page.getByRole("button", { name: "Discard changes" }).click();
  await expect(page).toHaveURL(/\/ai\/prompts/);
  expect(state.posts).toEqual([]);
});

test("assistant regression pending provider cannot dismiss or leak into a reopened sheet", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  let release!: () => void;
  const held = new Promise<void>((resolve) => {
    release = resolve;
  });
  await page.route("**/api/v1/ai/provider", async (route) => {
    state.posts.push({
      path: "/api/v1/ai/provider",
      body: route.request().postDataJSON(),
    });
    await held;
    await json(route, {
      accepted: true,
      detail: "Provider saved after delay.",
    });
  });
  await page.goto("/#/ai");
  await page.getByRole("button", { name: "Replace provider" }).click();
  const sheet = page.getByRole("dialog", { name: "Assistant provider" });
  await sheet.getByLabel("API key", { exact: true }).fill("pending-secret");
  const requested = page.waitForRequest("**/api/v1/ai/provider");
  await sheet.getByRole("button", { name: "Save provider" }).click();
  await requested;
  try {
    await expect(
      sheet.getByRole("button", { name: "Cancel", exact: true }),
    ).toBeDisabled();
    await expect(
      sheet.getByRole("button", { name: "Anthropic", exact: true }),
    ).toBeDisabled();
    await page.keyboard.press("Escape");
    await expect(sheet).toBeVisible();
    await expect(page.getByRole("alertdialog")).toHaveCount(0);
  } finally {
    release();
  }
  await expect(sheet).toHaveCount(0);
  expect(state.posts).toHaveLength(1);
  await page.getByRole("button", { name: "Replace provider" }).click();
  await expect(sheet.getByLabel("API key", { exact: true })).toHaveValue("");
  await expectDraftDirty(page, false);
});

for (const kind of ["prompt", "memory"] as const) {
  test(`assistant regression ${kind} save reopens immediately with background refresh deferred`, async ({
    page,
  }) => {
    const state = await mockFirmwareApi(page);
    await page.goto(
      kind === "prompt" ? "/#/ai/prompts" : "/#/ai/prompts?section=memory",
    );
    const open = page.getByRole("button", {
      name: kind === "prompt" ? /System prompt/ : /Preferences/,
    });
    await open.click();
    let release!: () => void;
    const held = new Promise<void>((resolve) => {
      release = resolve;
    });
    const refresh = page.waitForRequest("**/api/v1/storage");
    await page.route("**/api/v1/storage", async (route) => {
      await held;
      await json(route, state.storage);
    });
    const field = page.getByRole("textbox", {
      name: kind === "prompt" ? "Prompt Markdown" : "Saved context Markdown",
    });
    await field.fill("Saved immediately é");
    await page.getByRole("button", { name: "Save encrypted" }).click();
    try {
      await refresh;
      await expect(page.getByRole("dialog")).toHaveCount(0);
      await open.click();
      await expect(field).toHaveValue("Saved immediately é");
      await expectDraftDirty(page, false);
      await expect(
        page.getByRole("button", { name: "Revert changes" }),
      ).toBeDisabled();
      await field.fill("é");
      await expect(page.getByRole("dialog").getByRole("status")).toContainText(
        "2 /",
      );
      await page.getByRole("button", { name: "Revert changes" }).click();
      await expect(field).toHaveValue("Saved immediately é");
      await expectDraftDirty(page, false);
      expect(state.posts.at(-1)).toMatchObject({
        path:
          kind === "prompt"
            ? "/api/v1/ai/prompts/file"
            : "/api/v1/ai/memory/section",
        body:
          kind === "prompt"
            ? {
                markdown: "",
                markdown_base64: Buffer.from("Saved immediately é").toString(
                  "base64",
                ),
              }
            : { markdown: "Saved immediately é" },
      });
    } finally {
      release();
    }
  });
}

test("assistant regression saved context create, search and exact deletion confirmation send real requests", async ({
  page,
}) => {
  const state = await mockFirmwareApi(page);
  await page.route("**/api/v1/ai/memory/section", async (route) => {
    const body = route.request().postDataJSON();
    state.posts.push({ path: "/api/v1/ai/memory/section", body });
    state.aiMemory.sections.push({
      id: body.id,
      title: body.id,
      markdown: body.markdown,
    });
    await json(route, { accepted: true, detail: "Created encrypted context." });
  });
  await page.route("**/api/v1/ai/memory/remove", async (route) => {
    const body = route.request().postDataJSON();
    state.posts.push({ path: "/api/v1/ai/memory/remove", body });
    state.aiMemory.sections = state.aiMemory.sections.filter(
      (section) => section.id !== body.id,
    );
    await json(route, { accepted: true, detail: "Deleted encrypted context." });
  });
  await page.goto("/#/ai/prompts?section=memory&extra=keep");
  await page.getByRole("button", { name: "Create saved context" }).click();
  await page
    .getByLabel("Saved context ID", { exact: true })
    .fill("e2e_context");
  await page
    .getByRole("textbox", { name: "Saved context Markdown" })
    .fill("Remember verified state.");
  await page.getByRole("button", { name: "Save encrypted" }).click();
  await expect(page.getByRole("dialog")).toHaveCount(0);
  expect(state.posts).toEqual([
    {
      path: "/api/v1/ai/memory/section",
      body: { id: "e2e_context", markdown: "Remember verified state." },
    },
  ]);
  await page.getByLabel("Search assistant context").fill("E2E_CONTEXT");
  await expect(page).toHaveURL(/extra=keep/);
  await expect(page.getByRole("button", { name: /Preferences/ })).toHaveCount(
    0,
  );
  await page.getByRole("button", { name: /e2e_context/ }).click();
  const remove = page.getByRole("button", { name: "Delete saved context" });
  const confirmation = page.getByLabel(
    "Type DELETE MEMORY e2e_context to confirm deletion",
  );
  await expect(remove).toBeDisabled();
  await confirmation.fill("DELETE MEMORY e2e_context ");
  await expect(remove).toBeDisabled();
  expect(state.posts).toHaveLength(1);
  await confirmation.fill("DELETE MEMORY e2e_context");
  await remove.click();
  await expect(page.getByRole("dialog")).toHaveCount(0);
  expect(state.posts).toHaveLength(2);
  expect(state.posts[1]).toEqual({
    path: "/api/v1/ai/memory/remove",
    body: { id: "e2e_context", confirmation: "DELETE MEMORY e2e_context" },
  });
  await expect(page.getByRole("button", { name: /e2e_context/ })).toHaveCount(
    0,
  );
  await page.getByLabel("Search assistant context").fill("");
  await expect(page.getByRole("button", { name: /Preferences/ })).toBeVisible();
});
