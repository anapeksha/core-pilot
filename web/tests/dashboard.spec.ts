import { Buffer } from "node:buffer";

import { expect, test, type Page, type Route } from "@playwright/test";

type Json = Record<string, unknown>;

type FirmwareState = ReturnType<typeof createFirmwareState>;

function createFirmwareState() {
  return {
    health: {
      product: "Core Pilot",
      hostname: "corepilot.local",
      storage_ok: true,
      ai_enabled: true,
      auth: "none-trusted-lan",
    },
    network: {
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
    },
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
      status: "ok",
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
    return json(route, { accepted: true, detail: "OTA metadata accepted." });
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

test("shows Home card skeletons until its APIs resolve", async ({ page }) => {
  await mockFirmwareApi(page);
  let releaseHealth!: () => void;
  const healthReady = new Promise<void>((resolve) => {
    releaseHealth = resolve;
  });
  await page.route("**/api/v1/health", async (route) => {
    await healthReady;
    await json(route, createFirmwareState().health);
  });

  await page.goto("/");
  await expect(page.getByText("Loading home status")).toBeAttached();
  const skeleton = page.locator('[data-home-card-skeleton="home status"]');
  await expect(skeleton).toBeVisible();
  await expect(skeleton.locator("[data-home-status-skeleton]")).toHaveCount(4);
  await expect(page.getByText("Lab_WiFi", { exact: false })).toHaveCount(0);

  releaseHealth();
  await expect(
    page.getByText("Lab_WiFi", { exact: false }).first(),
  ).toBeVisible();
  await expect(page.getByText("Loading home status")).toHaveCount(0);
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
});

test("provides a bounded operational home dashboard", async ({ page }) => {
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
  await expect(page.getByText("Clear", { exact: true })).toBeVisible();
  await expect(page.getByRole("img", { name: "OK" })).toHaveCount(3);
  await expect(page.getByText("Lab", { exact: true })).toBeVisible();
  await expect(page.getByText("1 reported devices")).toBeVisible();
  await expect(
    page.getByRole("button", { name: "All off · unavailable" }),
  ).toHaveCount(0);
  await expect(
    page.getByRole("button", { name: "Activate scene · unavailable" }),
  ).toHaveCount(0);

  const benchLampFavorite = page.getByRole("button", { name: "Bench Lamp" });
  await benchLampFavorite.click();
  await expect(benchLampFavorite).toHaveAttribute("aria-pressed", "true");
  await expect(page.getByText("Bench Lamp", { exact: true })).toHaveCount(1);
  await page.reload();
  await expect(
    page.getByRole("button", { name: "Bench Lamp" }),
  ).toHaveAttribute("aria-pressed", "true");

  await page.getByRole("button", { name: "Welcome home" }).click();
  await expect
    .poll(() => state.posts.at(-1)?.path)
    .toBe("/api/v1/automations/run");
  await expect(page.getByRole("link", { name: "View activity" })).toBeVisible();
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
    page.getByRole("button", { name: "Open command palette" }),
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
    .toBe("/api/v1/automations/run");

  await page.getByRole("button", { name: "Open command palette" }).click();
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
      .getByRole("region", { name: "Automation run queued" })
      .getByText("Automation queued for bounded local evaluation"),
  ).toBeVisible();
  expect(state.posts.at(-1)).toMatchObject({
    path: "/api/v1/automations/run",
    body: { id: "sunset-routine" },
  });
});

test("prefills editable automation templates without enabling unsupported triggers", async ({
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
  await expect(page.getByText("Bench Lamp")).toBeVisible();
  await expect(page.getByText("On/Off", { exact: true })).toBeVisible();
  await expect(page.getByText("Level", { exact: true })).toBeVisible();

  await expect(page.getByRole("columnheader", { name: "Actions" })).toHaveCount(
    0,
  );
  await page
    .locator('[data-device-id="matter-light-1"]')
    .dispatchEvent("contextmenu", { clientX: 100, clientY: 100 });
  await page.getByRole("menuitem", { name: "Remove Bench Lamp" }).click();
  await page.getByRole("button", { name: "Remove device" }).click();
  await expect(page.getByText("Matter device removal queued.")).toBeVisible();
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

  await page.getByRole("button", { name: "Update provider" }).click();
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

  await expect(
    page.getByText(
      "Markdown sections are stored only as encrypted SD records.",
    ),
  ).toBeVisible();

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
    .locator("textarea")
    .fill("# Updated\n- **Bounded** action\n`safe`");
  await page.getByRole("tab", { name: "Preview" }).click();
  await expect(page.getByRole("heading", { name: "Updated" })).toBeVisible();
  await expect(
    page.locator("strong").filter({ hasText: "Bounded" }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Save encrypted" }).click();
  await expect(page.getByText("Prompt file saved.")).toBeVisible();
  expect(state.posts.at(-1)?.path).toBe("/api/v1/ai/prompts/file");
});

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

  await page.getByRole("button", { name: "Edit time settings" }).click();
  await expect(page.getByRole("combobox", { name: "NTP server" })).toHaveValue(
    "Cloudflare time.cloudflare.com",
  );
  await expect(page.getByRole("combobox", { name: "Timezone" })).toHaveValue(
    "UTC · UTC+00:00",
  );
  await page.getByRole("combobox", { name: "NTP server" }).fill("Google");
  await page.getByRole("option", { name: "Google time.google.com" }).click();
  await page.getByRole("combobox", { name: "Timezone" }).fill("UTC");
  await page.getByRole("option", { name: /^UTC · UTC\+00:00$/ }).click();
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
  await expect(page.getByText("Ready to upload")).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Validate & upload" }),
  ).toBeEnabled();
  await page.getByRole("button", { name: "Validate & upload" }).click();
  await expect(page.getByText("OTA chunk staged.")).toBeVisible();
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
  await page.getByRole("button", { name: "Update provider" }).click();
  await page.getByLabel("Model").fill("");
  await expect(
    page.getByRole("button", { name: "Save provider" }),
  ).toBeEnabled();
  await page.getByRole("button", { name: "Save provider" }).click();
  expect(
    state.posts.filter((request) => request.path === "/api/v1/ai/provider"),
  ).toHaveLength(0);

  await page.goto("/#/system");
  await expect(
    page.getByRole("button", { name: "Validate & upload" }),
  ).toBeDisabled();
  expect(state.posts).toEqual([]);
});
