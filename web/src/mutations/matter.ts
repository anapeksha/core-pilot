import { createMutation, useQueryClient } from "@tanstack/solid-query";
import {
  cancelMatterCommission,
  commissionMatter,
  removeMatterDevice,
  requireAccepted,
  type MatterCommissionRequest,
  type MatterDeviceMutationRequest,
  type MatterDeviceRemoveRequest,
  type ThreadDatasetUpdateRequest,
  updateMatterDevice,
  updateThreadDataset,
} from "../lib/api";

export function matterCommissionMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["matter", "commission"],
    mutationFn: async (request: MatterCommissionRequest) => {
      const response = await commissionMatter(request);
      if (!response.accepted) {
        throw new Error(response.detail);
      }
      return response;
    },
    onSuccess: () => {
      void queryClient.invalidateQueries({
        queryKey: ["matter"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["devices"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["system"],
        refetchType: "all",
      });
    },
  }));
}

export function matterDeviceMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["matter", "devices", "update"],
    mutationFn: async (request: MatterDeviceMutationRequest) =>
      requireAccepted(await updateMatterDevice(request)),
    onSuccess: () =>
      queryClient.invalidateQueries({
        queryKey: ["devices"],
        refetchType: "all",
      }),
  }));
}

export function matterDeviceRemoveMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["matter", "devices", "remove"],
    mutationFn: async (request: MatterDeviceRemoveRequest) =>
      requireAccepted(await removeMatterDevice(request)),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ["matter"] });
      void queryClient.invalidateQueries({ queryKey: ["devices"] });
    },
  }));
}

export function matterThreadDatasetMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["matter", "thread", "dataset"],
    mutationFn: async (request: ThreadDatasetUpdateRequest) =>
      requireAccepted(await updateThreadDataset(request)),
    onSuccess: () => {
      void queryClient.invalidateQueries({
        queryKey: ["matter"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["system"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["storage"],
        refetchType: "all",
      });
    },
  }));
}

export function matterCommissionCancelMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["matter", "commission", "cancel"],
    mutationFn: async (operationId: number) => {
      const response = await cancelMatterCommission(operationId);
      if (!response.accepted) throw new Error(response.detail);
      return response;
    },
    onSuccess: () =>
      queryClient.invalidateQueries({
        queryKey: ["matter"],
        refetchType: "all",
      }),
  }));
}
