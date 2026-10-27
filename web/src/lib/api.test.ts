import { afterEach, describe, expect, it, vi } from "vitest";
import * as api from "./api";
import { accessToken, clearAccess, unlockAccess } from "./auth";
import {
  loadActivityPage,
  loadNetwork,
  MAX_OTA_IMAGE_BYTES,
  prepareOta,
  requireAccepted,
  runWifiScanSession,
  type WifiScanPage,
} from "./api";

function jsonResponse(body: unknown, init: ResponseInit = {}) {
  return new Response(JSON.stringify(body), {
    status: 200,
    headers: { "content-type": "application/json" },
    ...init,
  });
}

describe("api client", () => {
  afterEach(() => {
    clearAccess();
    vi.useRealTimers();
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  it("adds the RAM bearer centrally and clears it on 401 without replay", async () => {
    const token = "b".repeat(64);
    const fetchMock = vi
      .fn()
      .mockResolvedValueOnce(jsonResponse({ token, expires_in: 28800 }))
      .mockResolvedValueOnce(jsonResponse({ mode: "station" }))
      .mockResolvedValueOnce(jsonResponse({}, { status: 401 }));
    vi.stubGlobal("fetch", fetchMock);
    await unlockAccess("0123456789abcdef");
    await loadNetwork();
    expect(fetchMock).toHaveBeenNthCalledWith(
      2,
      "/api/v1/network",
      expect.objectContaining({
        headers: expect.objectContaining({ authorization: `Bearer ${token}` }),
        credentials: "omit",
      }),
    );
    await expect(api.rebootSystem()).rejects.toThrow("401");
    expect(accessToken()).toBeNull();
    expect(fetchMock).toHaveBeenCalledTimes(3);
  });

  it("turns firmware mutation rejections into errors", () => {
    expect(() =>
      requireAccepted({
        accepted: false,
        status: "rejected",
        detail: "Storage unavailable.",
      }),
    ).toThrow("Storage unavailable.");
    expect(
      requireAccepted({ accepted: true, status: "saved", detail: "Saved." }),
    ).toMatchObject({ accepted: true });
  });

  // Positive: loads network state from the bounded firmware endpoint and sends
  // an explicit JSON Accept header for embedded picoserve routing.
  it("loads network status with JSON accept headers", async () => {
    const fetchMock = vi.fn(() =>
      Promise.resolve(
        jsonResponse({
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
        }),
      ),
    );
    vi.stubGlobal("fetch", fetchMock);

    await expect(loadNetwork()).resolves.toMatchObject({
      mode: "station",
      station_ssid: "Lab_WiFi",
      wan_enabled: false,
    });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/v1/network",
      expect.objectContaining({ headers: { accept: "application/json" } }),
    );
  });

  it("does not retry permanent firmware query rejections", async () => {
    const fetchMock = vi.fn(() =>
      Promise.resolve(
        jsonResponse({ detail: "Invalid request." }, { status: 400 }),
      ),
    );
    vi.stubGlobal("fetch", fetchMock);

    await expect(loadNetwork()).rejects.toThrow("/api/v1/network 400");
    expect(fetchMock).toHaveBeenCalledOnce();
  });

  it("passes caller cancellation to firmware queries", async () => {
    const controller = new AbortController();
    const fetchMock = vi.fn(
      (_input: RequestInfo | URL, init?: RequestInit) =>
        new Promise<Response>((_resolve, reject) => {
          init?.signal?.addEventListener(
            "abort",
            () => reject(init.signal?.reason),
            { once: true },
          );
        }),
    );
    vi.stubGlobal("fetch", fetchMock);

    const request = loadNetwork(controller.signal);
    const rejection = expect(request).rejects.toMatchObject({
      name: "AbortError",
    });
    await vi.waitFor(() => expect(fetchMock).toHaveBeenCalledOnce());
    const requestSignal = fetchMock.mock.calls[0]?.[1]?.signal;
    expect(requestSignal).toBeInstanceOf(AbortSignal);
    controller.abort();

    await rejection;
    expect(requestSignal?.aborted).toBe(true);
  });

  it("releases the API queue between bounded retry attempts", async () => {
    vi.useFakeTimers();
    let attempts = 0;
    const fetchMock = vi.fn((_input: RequestInfo | URL, init?: RequestInit) => {
      attempts += 1;
      if (attempts <= 3) {
        return new Promise<Response>((_resolve, reject) => {
          init?.signal?.addEventListener(
            "abort",
            () => reject(init.signal?.reason),
            { once: true },
          );
        });
      }
      return Promise.resolve(jsonResponse({ mode: "station" }));
    });
    vi.stubGlobal("fetch", fetchMock);

    const stalled = loadNetwork();
    const queued = loadNetwork();
    await vi.advanceTimersByTimeAsync(20_000);

    await expect(stalled).resolves.toMatchObject({ mode: "station" });
    await expect(queued).resolves.toMatchObject({ mode: "station" });
    expect(fetchMock).toHaveBeenCalledTimes(5);
  });

  it("releases the API queue while retrying stalled response bodies", async () => {
    vi.useFakeTimers();
    let attempts = 0;
    const fetchMock = vi.fn((_input: RequestInfo | URL, init?: RequestInit) => {
      attempts += 1;
      if (attempts <= 3) {
        return Promise.resolve({
          ok: true,
          status: 200,
          json: () =>
            new Promise((_resolve, reject) => {
              init?.signal?.addEventListener(
                "abort",
                () => reject(init.signal?.reason),
                { once: true },
              );
            }),
        } as Response);
      }
      return Promise.resolve(jsonResponse({ mode: "station" }));
    });
    vi.stubGlobal("fetch", fetchMock);

    const stalled = loadNetwork();
    const queued = loadNetwork();
    await vi.advanceTimersByTimeAsync(20_000);

    await expect(stalled).resolves.toMatchObject({ mode: "station" });
    await expect(queued).resolves.toMatchObject({ mode: "station" });
    expect(fetchMock).toHaveBeenCalledTimes(5);
  });

  it("loads a validated activity cursor page", async () => {
    const fetchMock = vi.fn(() =>
      Promise.resolve(
        jsonResponse({
          events: [
            {
              sequence: 42,
              unix_seconds: 1_700_000_000,
              uptime_seconds: 123,
              kind: "automation_queued",
              detail: "Automation queued.",
            },
          ],
          next_before_sequence: 42,
          retained: 2,
          dropped_events: 0,
          detail: "Newest events first.",
        }),
      ),
    );
    vi.stubGlobal("fetch", fetchMock);

    await expect(
      loadActivityPage({ before_sequence: null, limit: 20 }),
    ).resolves.toMatchObject({
      next_before_sequence: 42,
      events: [{ kind: "automation_queued" }],
    });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/v1/activity/page",
      expect.objectContaining({
        method: "POST",
        body: '{"before_sequence":null,"limit":20}',
      }),
    );
  });

  it("rejects invalid mutation payloads before fetch", async () => {
    const fetchMock = vi.fn();
    vi.stubGlobal("fetch", fetchMock);

    await expect(
      api.provisionWifi({
        credentials: { ssid: "", password: "short" },
        hostname: "-invalid-host",
      }),
    ).rejects.toThrow();
    await expect(api.upsertRoom({ name: "x".repeat(65) })).rejects.toThrow();
    await expect(api.upsertRoom({ name: "😀".repeat(17) })).rejects.toThrow();
    await expect(
      api.saveAiMemorySection({ id: "x".repeat(49), markdown: "bounded" }),
    ).rejects.toThrow();
    await expect(api.loadActivityPage({ limit: 33 })).rejects.toThrow();
    await expect(
      api.updateThreadDataset({ active_dataset_tlvs_hex: "abc" }),
    ).rejects.toThrow("complete hexadecimal bytes");
    await expect(
      api.updateThreadDataset({ active_dataset_tlvs_hex: "not-hex" }),
    ).rejects.toThrow("hexadecimal characters only");

    expect(fetchMock).not.toHaveBeenCalled();
  });

  it("normalizes legacy AI status without provider transport metadata", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(() =>
        Promise.resolve(
          jsonResponse({
            enabled: true,
            provider_configured: false,
            mcp_enabled: true,
          }),
        ),
      ),
    );

    await expect(api.loadAi()).resolves.toMatchObject({
      enabled: true,
      provider_configured: false,
      provider_transport: {
        http_ipv4_literal: true,
        detail: expect.any(String),
      },
      provider_transport_requirements: [],
    });
  });

  it("normalizes partial MCP descriptors for readable permission cards", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(() =>
        Promise.resolve(
          jsonResponse({
            tools: [
              {
                description: "List commissioned devices",
                enabled: true,
                destructive: false,
                available: true,
                inputSchema: {
                  type: "object",
                  properties: {},
                  additionalProperties: false,
                },
              },
            ],
          }),
        ),
      ),
    );

    await expect(api.loadMcpTools()).resolves.toEqual({
      tools: [
        expect.objectContaining({
          id: "devices.list",
          name: "ListDevices",
          title: "Devices list",
          category: "Capabilities",
          enabled: true,
          available: true,
          inputSchema: {
            type: "object",
            properties: {},
            additionalProperties: false,
          },
        }),
      ],
    });
  });

  it("loads and forgets saved Wi-Fi networks without credential fields", async () => {
    const fetchMock = vi
      .fn()
      .mockResolvedValueOnce(
        jsonResponse({
          networks: [{ ssid: "Lab_WiFi", connected: true }],
          detail: "1 saved network.",
        }),
      )
      .mockResolvedValueOnce(
        jsonResponse({
          accepted: true,
          status: "forgotten",
          detail: "Forgotten.",
        }),
      );
    vi.stubGlobal("fetch", fetchMock);

    await expect(api.loadSavedWifiNetworks()).resolves.toEqual({
      networks: [{ ssid: "Lab_WiFi", connected: true }],
      detail: "1 saved network.",
    });
    await api.forgetWifiNetwork({ ssid: "Lab_WiFi" });

    expect(fetchMock).toHaveBeenNthCalledWith(
      2,
      "/api/v1/network/wifi/forget",
      expect.objectContaining({
        method: "POST",
        body: '{"ssid":"Lab_WiFi"}',
      }),
    );
  });

  it("accepts current Wi-Fi and tool permission wire payloads", async () => {
    const fetchMock = vi.fn(() =>
      Promise.resolve(
        jsonResponse({ accepted: true, status: "ok", detail: "saved" }),
      ),
    );
    vi.stubGlobal("fetch", fetchMock);

    await api.provisionWifi({
      credentials: { ssid: "Mesh_IoT", password: "network-secret" },
      hostname: "corepilot.local",
    });
    await api.postJson("/api/v1/ai/mcp/tool", {
      tool: "devices.list",
      enabled: false,
    });

    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  // Positive: posts OTA metadata through the dedicated prepare endpoint and
  // preserves the app-image size plus restart policy in JSON.
  it("posts OTA metadata with bounded size and restart policy", async () => {
    const fetchMock = vi.fn(() =>
      Promise.resolve(
        jsonResponse({
          accepted: true,
          status: "prepared",
          detail: "Firmware metadata validated.",
        }),
      ),
    );
    vi.stubGlobal("fetch", fetchMock);

    await expect(
      prepareOta({
        version: "app",
        size_bytes: MAX_OTA_IMAGE_BYTES,
        sha256_hex:
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        restart_after_update: true,
      }),
    ).resolves.toMatchObject({ accepted: true, status: "prepared" });

    expect(fetchMock).toHaveBeenCalledWith(
      "/api/v1/ota/prepare",
      expect.objectContaining({
        method: "POST",
        headers: {
          accept: "application/json",
          "content-type": "application/json",
        },
        body: expect.stringContaining(`"size_bytes":${MAX_OTA_IMAGE_BYTES}`),
      }),
    );
  });

  // Positive: streams paginated Wi‑Fi scan results through the page callback and
  // returns the accumulated network list when scanning completes.
  it("streams paged Wi-Fi scan results through the callback", async () => {
    const pages: WifiScanPage[] = [
      {
        networks: [
          {
            id: "a",
            ssid: "Lab_WiFi",
            bssid: "aa:bb:cc:dd:ee:ff",
            connected: false,
            signal_dbm: -42,
            channel: 6,
            secured: true,
            auth: "wpa2",
          },
        ],
        scanning: true,
        scan_state: "scanning",
        next_last_seen: "a",
        detail: "Scanning…",
      },
      {
        networks: [
          {
            id: "b",
            ssid: "Guest",
            bssid: "11:22:33:44:55:66",
            connected: false,
            signal_dbm: -70,
            channel: 11,
            secured: false,
            auth: "open",
          },
        ],
        scanning: false,
        scan_state: "complete",
        next_last_seen: null,
        detail: "Complete.",
      },
    ];
    const seenPages: WifiScanPage[] = [];
    const fetchMock = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      const path = String(input);
      if (path === "/api/v1/network/wifi/scan" && init?.method === "POST") {
        return Promise.resolve(
          jsonResponse({
            networks: [],
            scanning: true,
            scan_state: "scanning",
            detail: "Queued.",
          }),
        );
      }
      if (path === "/api/v1/network/wifi/scan/page") {
        return Promise.resolve(jsonResponse(pages.shift()));
      }
      throw new Error(`Unexpected request ${path}`);
    });
    vi.stubGlobal("fetch", fetchMock);

    const result = await runWifiScanSession(
      new AbortController().signal,
      (page) => {
        seenPages.push(page);
      },
    );

    expect(result.networks.map((network) => network.ssid)).toEqual([
      "Lab_WiFi",
      "Guest",
    ]);
    expect(seenPages.map((page) => page.networks.length)).toEqual([1, 2]);
    expect(fetchMock).toHaveBeenCalledTimes(3);
  });

  it("resumes a Wi-Fi scan after the station HTTP connection returns", async () => {
    vi.useFakeTimers();
    let pageAttempts = 0;
    vi.stubGlobal(
      "fetch",
      vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
        const path = String(input);
        if (path === "/api/v1/network/wifi/scan" && init?.method === "POST") {
          return Promise.resolve(
            jsonResponse({
              networks: [],
              scanning: true,
              scan_state: "scanning",
              detail: "Queued.",
            }),
          );
        }
        if (path === "/api/v1/network/wifi/scan/page") {
          pageAttempts += 1;
          if (pageAttempts === 1)
            return Promise.reject(
              new TypeError("station temporarily unavailable"),
            );
          return Promise.resolve(
            jsonResponse({
              networks: [
                {
                  id: "mesh",
                  ssid: "Mesh_IoT",
                  bssid: "22:33:44:55:66:77",
                  connected: false,
                  signal_dbm: -61,
                  channel: 6,
                  secured: true,
                  auth: "wpa2",
                },
              ],
              scanning: false,
              scan_state: "complete",
              next_last_seen: null,
              detail: "Complete.",
            }),
          );
        }
        throw new Error(`Unexpected request ${path}`);
      }),
    );

    const scan = runWifiScanSession(new AbortController().signal);
    await vi.runAllTimersAsync();
    await expect(scan).resolves.toMatchObject({
      scan_state: "complete",
      networks: [{ ssid: "Mesh_IoT" }],
    });
    expect(pageAttempts).toBe(2);
  });

  // Negative: aborting a Wi-Fi scan propagates the AbortSignal reason instead of
  // spinning through bounded scan retries after the user leaves the System page.
  it("stops Wi-Fi scan sessions when the signal is aborted", async () => {
    vi.useFakeTimers();
    const controller = new AbortController();
    vi.stubGlobal(
      "fetch",
      vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
        const path = String(input);
        if (path === "/api/v1/network/wifi/scan" && init?.method === "POST") {
          return Promise.resolve(
            jsonResponse({
              networks: [],
              scanning: true,
              scan_state: "scanning",
              detail: "Queued.",
            }),
          );
        }
        if (path === "/api/v1/network/wifi/scan/page") {
          return Promise.resolve(
            jsonResponse({
              networks: [],
              scanning: true,
              scan_state: "scanning",
              next_last_seen: null,
              detail: "Still scanning.",
            }),
          );
        }
        if (path === "/api/v1/network/wifi/scan") {
          return Promise.resolve(
            jsonResponse({
              networks: [],
              scanning: true,
              scan_state: "scanning",
              detail: "Still scanning.",
            }),
          );
        }
        throw new Error(`Unexpected request ${path}`);
      }),
    );

    const scan = runWifiScanSession(controller.signal);
    await vi.runOnlyPendingTimersAsync();
    controller.abort(new Error("left system page"));
    await expect(scan).rejects.toThrow("left system page");
  });

  // Negative: non-2xx firmware responses surface as endpoint-specific errors so
  // callers can render failure states instead of accepting fallback data silently.
  it("surfaces non-ok firmware responses as errors", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(() => Promise.resolve(jsonResponse({}, { status: 500 }))),
    );
    await expect(loadNetwork()).rejects.toThrow("/api/v1/network 500");
  });

  // Negative: malformed JSON from firmware rejects the load path and does not get
  // coerced into a plausible status object.
  it("rejects malformed firmware JSON responses", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(() =>
        Promise.resolve(
          new Response("not-json", {
            status: 200,
            headers: { "content-type": "application/json" },
          }),
        ),
      ),
    );
    await expect(loadNetwork()).rejects.toThrow();
  });

  it("rejects invalid Matter OnOff requests before fetch", async () => {
    const fetchMock = vi.fn();
    vi.stubGlobal("fetch", fetchMock);

    await expect(
      api.updateMatterDevice({
        device_id: "",
        endpoint_id: 0x1_0000,
        on: true,
      }),
    ).rejects.toThrow();
    expect(fetchMock).not.toHaveBeenCalled();
  });

  // Positive/integration: maps every dashboard API wrapper to its bounded firmware
  // endpoint and verifies mutating calls use POST with serialized JSON bodies.
  it("maps dashboard API wrappers to bounded firmware endpoints", async () => {
    const calls: { path: string; init?: RequestInit }[] = [];
    vi.stubGlobal(
      "fetch",
      vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
        calls.push({ path: String(input), init });
        return Promise.resolve(
          jsonResponse({ accepted: true, status: "ok", detail: "ok" }),
        );
      }),
    );

    const sampleDelete = { id: "id-1" };
    await api.loadHealth();
    await api.loadSystem();
    await api.rebootSystem();
    await api.loadStorage();
    await api.loadTime();
    await api.saveTimeConfig({
      ntp_server_id: "cloudflare",
      timezone_id: "UTC",
      timezone_offset_minutes: 0,
    });
    await api.loadDevices();
    await api.updateMatterDevice({
      device_id: "1234",
      endpoint_id: 1,
      on: true,
    });
    await api.loadRooms();
    await api.upsertRoom({ name: "Kitchen" });
    await api.deleteRoom(sampleDelete);
    await api.loadGroups();
    await api.upsertGroup({ name: "Downstairs", device_ids: [] });
    await api.deleteGroup(sampleDelete);
    await api.loadScenes();
    await api.upsertScene({ name: "Evening", actions: [] });
    await api.deleteScene(sampleDelete);
    await api.loadAutomations();
    await api.upsertAutomation({
      name: "Morning",
      enabled: true,
      trigger: "manual",
      actions: ["notify|Morning automation"],
    });
    await api.runAutomation(sampleDelete);
    await api.deleteAutomation(sampleDelete);
    await api.loadMatter();
    await api.commissionMatter({
      transport: "wifi",
      setup_payload: "12345678901",
    });
    await api.updateThreadDataset({
      active_dataset_tlvs_hex: "0E080000000000010000",
    });
    await api.loadAi();
    await api.loadAiMemory();
    await api.loadAiPrompts();
    await api.sendAiChat({ message: "hi", history: [] });
    await api.saveAiMemorySection({ id: "notes", markdown: "hello" });
    await api.saveAiPromptFile({ path: "system.md", markdown: "hello" });
    await api.provisionWifi({
      credentials: { ssid: "Lab", password: "password" },
    });
    await api.setupAiProvider({
      provider: {
        backend: "open_ai_compatible",
        auth_type: "bearer",
        base_url: "https://api.openai.com/v1",
        model: "gpt-4.1-mini",
        max_output_tokens: 256,
        timeout_ms: 10_000,
        api_key: "test-key",
        api_key_redacted: false,
      },
    });
    await api.loadMcpTools();
    await api.updateMcpToolPermission({ tool: "GetHubStatus", enabled: true });
    await api.loadMcpContext();
    await api.invokeMcpTool({ tool: "GetHubStatus", arguments_json: "{}" });
    await api.uploadOtaChunk({
      offset: 0,
      data_base64: "AA==",
      final_chunk: true,
    });

    expect(calls.map((call) => call.path)).toEqual([
      "/api/v1/health",
      "/api/v1/system",
      "/api/v1/system/reboot",
      "/api/v1/storage",
      "/api/v1/time",
      "/api/v1/time",
      "/api/v1/devices",
      "/api/v1/devices",
      "/api/v1/rooms",
      "/api/v1/rooms/upsert",
      "/api/v1/rooms/delete",
      "/api/v1/groups",
      "/api/v1/groups/upsert",
      "/api/v1/groups/delete",
      "/api/v1/scenes",
      "/api/v1/scenes/upsert",
      "/api/v1/scenes/delete",
      "/api/v1/automations",
      "/api/v1/automations/upsert",
      "/api/v1/automations/run",
      "/api/v1/automations/delete",
      "/api/v1/matter",
      "/api/v1/matter/commission",
      "/api/v1/matter/thread/dataset",
      "/api/v1/ai",
      "/api/v1/ai/memory",
      "/api/v1/ai/prompts",
      "/api/v1/ai/chat",
      "/api/v1/ai/memory/section",
      "/api/v1/ai/prompts/file",
      "/api/v1/network/wifi",
      "/api/v1/ai/provider",
      "/api/v1/ai/mcp/tools",
      "/api/v1/ai/mcp/tool",
      "/api/v1/ai/mcp/context",
      "/api/v1/ai/mcp/invoke",
      "/api/v1/ota/chunk",
    ]);
    expect(
      calls
        .filter((call) => call.init?.method === "POST")
        .map((call) => call.path),
    ).toEqual([
      "/api/v1/system/reboot",
      "/api/v1/time",
      "/api/v1/rooms/upsert",
      "/api/v1/rooms/delete",
      "/api/v1/groups/upsert",
      "/api/v1/groups/delete",
      "/api/v1/scenes/upsert",
      "/api/v1/scenes/delete",
      "/api/v1/automations/upsert",
      "/api/v1/automations/run",
      "/api/v1/automations/delete",
      "/api/v1/matter/commission",
      "/api/v1/matter/thread/dataset",
      "/api/v1/ai/chat",
      "/api/v1/ai/memory/section",
      "/api/v1/ai/prompts/file",
      "/api/v1/network/wifi",
      "/api/v1/ai/provider",
      "/api/v1/ai/mcp/tool",
      "/api/v1/ai/mcp/invoke",
      "/api/v1/ota/chunk",
    ]);
    expect(calls[7].init?.body).toBe(
      '{"device_id":"1234","endpoint_id":1,"on":true}',
    );
    expect(calls[23].init?.body).toBe(
      '{"active_dataset_tlvs_hex":"0e080000000000010000"}',
    );
    expect(calls[29].init?.body).toContain("aGVsbG8=");
  });
});
