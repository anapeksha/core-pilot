import { describe, expect, it } from "vitest";

import {
  automationUpsertRequestSchema,
  groupUpsertRequestSchema,
  matterDeviceForgetRequestSchema,
  matterDeviceRemoveRequestSchema,
  mcpInvokeRequestSchema,
  mcpToolIds,
  mcpToolNames,
  mcpToolPermissionRequestSchema,
  otaPrepareRequestSchema,
  roomUpsertRequestSchema,
  sceneUpsertRequestSchema,
  wifiForgetRequestSchema,
  wifiProvisionRequestSchema,
} from "./request-contracts";

const expectedMcpTools = [
  ["ListRooms", "rooms.list"],
  ["CreateRoom", "rooms.create"],
  ["UpdateRoom", "rooms.update"],
  ["DeleteRoom", "rooms.delete"],
  ["ListGroups", "groups.list"],
  ["CreateGroup", "groups.create"],
  ["UpdateGroup", "groups.update"],
  ["DeleteGroup", "groups.delete"],
  ["ListScenes", "scenes.list"],
  ["CreateScene", "scenes.create"],
  ["UpdateScene", "scenes.update"],
  ["DeleteScene", "scenes.delete"],
  ["ListAutomations", "automations.list"],
  ["CreateAutomation", "automations.create"],
  ["UpdateAutomation", "automations.update"],
  ["DeleteAutomation", "automations.delete"],
  ["RunAutomation", "automations.run"],
  ["EnableAutomation", "automations.enable"],
  ["DisableAutomation", "automations.disable"],
  ["GetMatterOperation", "matter.get_operation"],
] as const;

const exactName = "😀".repeat(16);
const oversizedName = `${exactName}a`;

const modelRequests = [
  ["room", roomUpsertRequestSchema, (name: string) => ({ name })],
  [
    "group",
    groupUpsertRequestSchema,
    (name: string) => ({ name, device_ids: [] }),
  ],
  [
    "scene",
    sceneUpsertRequestSchema,
    (name: string) => ({ name, actions: [] }),
  ],
  [
    "automation",
    automationUpsertRequestSchema,
    (name: string) => ({
      name,
      enabled: true,
      trigger: "manual",
      actions: ["delay|1"],
    }),
  ],
] as const;

describe("persisted name limits", () => {
  for (const [kind, schema, request] of modelRequests) {
    it(`enforces the 64-byte ${kind} name limit`, () => {
      expect(schema.safeParse(request(exactName)).success).toBe(true);
      expect(schema.safeParse(request(oversizedName)).success).toBe(false);
      expect(schema.safeParse(request("   ")).success).toBe(false);
    });
  }

  it("enforces Wi-Fi SSID and password limits in UTF-8 bytes", () => {
    expect(
      wifiProvisionRequestSchema.safeParse({
        credentials: { ssid: "😀".repeat(8), password: "pässword" },
      }).success,
    ).toBe(true);
    expect(
      wifiProvisionRequestSchema.safeParse({
        credentials: { ssid: `${"😀".repeat(8)}a`, password: "password" },
      }).success,
    ).toBe(false);
    expect(
      wifiForgetRequestSchema.safeParse({ ssid: `${"😀".repeat(8)}a` }).success,
    ).toBe(false);
  });

  it("enforces esp_app_desc OTA version bounds and printable characters", () => {
    const request = (version: string) => ({
      version,
      size_bytes: 1024,
      sha256_hex: "a".repeat(64),
      restart_after_update: false,
    });
    expect(
      otaPrepareRequestSchema.safeParse(request("v".repeat(31))).success,
    ).toBe(true);
    for (const version of ["", "   ", "v".repeat(32), "release\n1", "v😀"])
      expect(otaPrepareRequestSchema.safeParse(request(version)).success).toBe(
        false,
      );
  });

  it("keeps the MCP name and ID allowlists in parity", () => {
    expect(mcpToolNames).toHaveLength(mcpToolIds.length);
    for (const [name, id] of expectedMcpTools) {
      expect(mcpToolNames).toContain(name);
      expect(mcpToolIds).toContain(id);
      expect(
        mcpToolPermissionRequestSchema.safeParse({ tool: name, enabled: true })
          .success,
      ).toBe(true);
      expect(
        mcpInvokeRequestSchema.safeParse({ tool: id, arguments_json: "{}" })
          .success,
      ).toBe(true);
    }
    for (const obsolete of [
      "CreateSimpleAutomation",
      "ResetMatterFabric",
      "RestartHub",
      "ShutdownHub",
      "FactoryReset",
      "automations.create_simple",
      "matter.reset_fabric",
      "hub.restart",
      "hub.shutdown",
      "hub.factory_reset",
    ])
      expect(
        mcpToolPermissionRequestSchema.safeParse({
          tool: obsolete,
          enabled: true,
        }).success,
      ).toBe(false);
  });

  it("requires an exact node-specific confirmation before forgetting a Matter device", () => {
    const device_id = "00029a12c059c064";
    expect(
      matterDeviceForgetRequestSchema.safeParse({
        device_id,
        confirmation: `FORGET MATTER DEVICE ${device_id}`,
      }).success,
    ).toBe(true);
    expect(
      matterDeviceForgetRequestSchema.safeParse({
        device_id,
        confirmation: "FORGET MATTER DEVICE",
      }).success,
    ).toBe(false);
  });

  it("accepts only nonzero 64-bit hexadecimal Matter node IDs", () => {
    for (const device_id of ["1", "0000000000001234", "abcdefABCDEF1234"])
      expect(
        matterDeviceRemoveRequestSchema.safeParse({ device_id }).success,
      ).toBe(true);

    for (const device_id of ["", "0", "-1", "+1", "0x12", "g1", "1".repeat(17)])
      expect(
        matterDeviceRemoveRequestSchema.safeParse({ device_id }).success,
      ).toBe(false);
  });
});
