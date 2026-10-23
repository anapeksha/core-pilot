import { createMutation, useQueryClient } from "@tanstack/solid-query";
import {
  cancelMatterCommission,
  commissionMatter,
  requireAccepted,
  setMatterOnOff,
  type MatterCommissionRequest,
  type MatterOnOffRequest,
  type ThreadDatasetUpdateRequest,
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

export function matterOnOffMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["matter", "on-off"],
    mutationFn: async (request: MatterOnOffRequest) =>
      requireAccepted(await setMatterOnOff(request)),
    onSuccess: () =>
      queryClient.invalidateQueries({
        queryKey: ["devices"],
        refetchType: "all",
      }),
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
