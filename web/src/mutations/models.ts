import { createMutation, useQueryClient } from "@tanstack/solid-query";
import {
  deleteAutomation,
  deleteGroup,
  deleteRoom,
  deleteScene,
  loadAutomationOperation,
  runAutomation,
  requireAccepted,
  upsertAutomation,
  upsertGroup,
  upsertRoom,
  upsertScene,
  type AutomationUpsertRequest,
  type GroupUpsertRequest,
  type ModelDeleteRequest,
  type RoomUpsertRequest,
  type SceneUpsertRequest,
} from "../lib/api";

function invalidateModelAndStorageQueries(
  queryClient: ReturnType<typeof useQueryClient>,
  key: string,
) {
  void queryClient.invalidateQueries({ queryKey: [key], refetchType: "all" });
  void queryClient.invalidateQueries({
    queryKey: ["storage"],
    refetchType: "all",
  });
}

export function roomUpsertMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["rooms", "upsert"],
    mutationFn: async (request: RoomUpsertRequest) =>
      requireAccepted(await upsertRoom(request)),
    onSuccess: () => invalidateModelAndStorageQueries(queryClient, "rooms"),
  }));
}

export function roomDeleteMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["rooms", "delete"],
    mutationFn: async (request: ModelDeleteRequest) =>
      requireAccepted(await deleteRoom(request)),
    onSuccess: () => invalidateModelAndStorageQueries(queryClient, "rooms"),
  }));
}

export function groupUpsertMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["groups", "upsert"],
    mutationFn: async (request: GroupUpsertRequest) =>
      requireAccepted(await upsertGroup(request)),
    onSuccess: () => invalidateModelAndStorageQueries(queryClient, "groups"),
  }));
}

export function groupDeleteMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["groups", "delete"],
    mutationFn: async (request: ModelDeleteRequest) =>
      requireAccepted(await deleteGroup(request)),
    onSuccess: () => invalidateModelAndStorageQueries(queryClient, "groups"),
  }));
}

export function sceneUpsertMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["scenes", "upsert"],
    mutationFn: async (request: SceneUpsertRequest) =>
      requireAccepted(await upsertScene(request)),
    onSuccess: () => invalidateModelAndStorageQueries(queryClient, "scenes"),
  }));
}

export function sceneDeleteMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["scenes", "delete"],
    mutationFn: async (request: ModelDeleteRequest) =>
      requireAccepted(await deleteScene(request)),
    onSuccess: () => invalidateModelAndStorageQueries(queryClient, "scenes"),
  }));
}

export function automationUpsertMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["automations", "upsert"],
    mutationFn: async (request: AutomationUpsertRequest) =>
      requireAccepted(await upsertAutomation(request)),
    onSuccess: () =>
      invalidateModelAndStorageQueries(queryClient, "automations"),
  }));
}

const automationPollIntervalMs = 500;
const automationPollLimit = 1200;

async function runAutomationToTerminal(request: ModelDeleteRequest) {
  const accepted = requireAccepted(await runAutomation(request));
  if (
    !Number.isInteger(accepted.operation_id) ||
    (accepted.operation_id ?? 0) <= 0
  )
    throw new Error(
      "The device accepted the automation without an operation ID.",
    );

  for (let attempt = 0; attempt < automationPollLimit; attempt += 1) {
    const operation = await loadAutomationOperation(accepted.operation_id!);
    if (operation.state === "complete")
      return {
        ...accepted,
        detail: "The automation completed successfully.",
      };
    if (operation.state === "failed")
      throw new Error(
        operation.failure ??
          operation.error ??
          "The automation operation failed.",
      );
    await new Promise<void>((resolve) =>
      window.setTimeout(resolve, automationPollIntervalMs),
    );
  }
  throw new Error("The automation did not complete within ten minutes.");
}

export function automationRunMutation() {
  return createMutation(() => ({
    mutationKey: ["automations", "run"],
    mutationFn: runAutomationToTerminal,
  }));
}

export function automationDeleteMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["automations", "delete"],
    mutationFn: async (request: ModelDeleteRequest) =>
      requireAccepted(await deleteAutomation(request)),
    onSuccess: () =>
      invalidateModelAndStorageQueries(queryClient, "automations"),
  }));
}
