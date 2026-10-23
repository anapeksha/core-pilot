import { createMutation, useQueryClient } from "@tanstack/solid-query";
import {
  deleteAutomation,
  deleteGroup,
  deleteRoom,
  deleteScene,
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

export function automationRunMutation() {
  return createMutation(() => ({
    mutationKey: ["automations", "run"],
    mutationFn: async (request: ModelDeleteRequest) =>
      requireAccepted(await runAutomation(request)),
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
