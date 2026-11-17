import { QueryClient } from "@tanstack/query-core";
import { beforeEach, expect, it, vi } from "vitest";

const state = vi.hoisted(() => ({ client: null as unknown }));
vi.mock("@tanstack/solid-query", () => ({
  useQueryClient: () => state.client,
  createMutation: (options: () => unknown) => options(),
}));
import {
  aiMemoryMutation,
  aiMemoryRemoveMutation,
  aiPromptMutation,
} from "./ai";

type SavedOptions = {
  onSuccess: (response: unknown, request: unknown) => Promise<void>;
};
let client: QueryClient;
beforeEach(() => {
  client = new QueryClient();
  state.client = client;
});

it("cancels an older memory GET before reconciling accepted content and creating entries", async () => {
  const key = ["ai", "memory"];
  client.setQueryData(key, {
    sections: [{ id: "home", title: "Home", markdown: "old" }],
    encrypted: true,
  });
  let finish!: (value: unknown) => void;
  const older = client
    .fetchQuery({
      queryKey: key,
      queryFn: () =>
        new Promise((resolve) => {
          finish = resolve;
        }),
    })
    .catch(() => undefined);
  const options = aiMemoryMutation() as unknown as SavedOptions;
  await options.onSuccess({}, { id: "home", markdown: "accepted" });
  finish({ sections: [] });
  await older;
  expect(client.getQueryData(key)).toMatchObject({
    sections: [{ id: "home", title: "Home", markdown: "accepted" }],
  });
  await options.onSuccess({}, { id: "new", markdown: "created" });
  expect(client.getQueryData(key)).toMatchObject({
    sections: [{ id: "home" }, { id: "new", markdown: "created" }],
  });
  client.clear();
});

it("reconciles accepted baseline overrides and confirmed removal without a stale refetch", async () => {
  client.setQueryData(["ai", "prompts"], { files: [], encrypted: true });
  await (aiPromptMutation() as unknown as SavedOptions).onSuccess(
    {},
    { path: "assistant/system.md", markdown: "accepted" },
  );
  expect(client.getQueryData(["ai", "prompts"])).toMatchObject({
    files: [
      { path: "assistant/system.md", markdown: "accepted", baseline: false },
    ],
  });
  client.setQueryData(["ai", "memory"], {
    sections: [{ id: "home" }, { id: "keep" }],
  });
  await (aiMemoryRemoveMutation() as unknown as SavedOptions).onSuccess(
    {},
    { id: "home" },
  );
  expect(client.getQueryData(["ai", "memory"])).toEqual({
    sections: [{ id: "keep" }],
  });
  client.clear();
});
