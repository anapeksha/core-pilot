import { afterEach, expect, it, vi } from "vitest";
import { loadAi, removeAiMemory } from "./api";
import {
  aiMemoryRemoveRequestSchema,
  aiMemoryRequestSchema,
  aiProviderSummarySchema,
} from "./request-contracts";

afterEach(() => vi.unstubAllGlobals());

it("requires bounded ASCII IDs and exact entry-specific deletion confirmation", () => {
  for (const id of ["", "a".repeat(49), "é", "has space", "../home"]) {
    expect(aiMemoryRequestSchema.safeParse({ id, markdown: "" }).success).toBe(
      false,
    );
    expect(
      aiMemoryRemoveRequestSchema.safeParse({
        id,
        confirmation: `DELETE MEMORY ${id}`,
      }).success,
    ).toBe(false);
  }
  expect(
    aiMemoryRemoveRequestSchema.safeParse({
      id: "Home_1-a",
      confirmation: "DELETE MEMORY Home_1-a",
    }).success,
  ).toBe(true);
  expect(
    aiMemoryRemoveRequestSchema.safeParse({
      id: "home",
      confirmation: "DELETE MEMORY other",
    }).success,
  ).toBe(false);
  expect(
    aiMemoryRequestSchema.safeParse({ id: "home", markdown: "é".repeat(6144) })
      .success,
  ).toBe(true);
  expect(
    aiMemoryRequestSchema.safeParse({ id: "home", markdown: "é".repeat(6145) })
      .success,
  ).toBe(false);
});

it("posts the validated removal contract and rejects invalid input before fetching", async () => {
  const fetch = vi
    .fn()
    .mockResolvedValue(new Response(JSON.stringify({ accepted: true })));
  vi.stubGlobal("fetch", fetch);
  await expect(
    removeAiMemory({ id: "home", confirmation: "DELETE MEMORY other" }),
  ).rejects.toThrow();
  expect(fetch).not.toHaveBeenCalled();
  await removeAiMemory({ id: "home", confirmation: "DELETE MEMORY home" });
  expect(fetch).toHaveBeenCalledWith(
    "/api/v1/ai/memory/remove",
    expect.objectContaining({
      method: "POST",
      body: JSON.stringify({ id: "home", confirmation: "DELETE MEMORY home" }),
    }),
  );
});

it("accepts optional safe provider metadata without retaining credential fields", async () => {
  const provider = {
    backend: "open_ai_compatible",
    auth_type: "bearer",
    base_url: "https://api.openai.com/v1",
    model: "example",
    max_output_tokens: 1024,
    timeout_ms: 30000,
  };
  vi.stubGlobal(
    "fetch",
    vi.fn().mockImplementation(() =>
      Promise.resolve(
        new Response(
          JSON.stringify({
            provider: { ...provider, api_key: "must-not-retain" },
          }),
        ),
      ),
    ),
  );
  expect((await loadAi()).provider).toEqual(provider);
  expect(
    aiProviderSummarySchema.safeParse({ ...provider, timeout_ms: -1 }).success,
  ).toBe(false);
  vi.stubGlobal("fetch", vi.fn().mockResolvedValue(new Response("{}")));
  expect((await loadAi()).provider).toBeUndefined();
});
