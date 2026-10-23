import { createMutation, useQueryClient } from "@tanstack/solid-query";
import {
  rebootSystem,
  requireAccepted,
  saveTimeConfig,
  type TimeConfigRequest,
} from "../lib/api";

export function systemRebootMutation() {
  return createMutation(() => ({
    mutationKey: ["system", "reboot"],
    mutationFn: async () => requireAccepted(await rebootSystem()),
  }));
}

export function timeConfigMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["time", "config"],
    mutationFn: async (request: TimeConfigRequest) =>
      requireAccepted(await saveTimeConfig(request)),
    onSuccess: () => {
      void queryClient.invalidateQueries({
        queryKey: ["time"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["storage"],
        refetchType: "all",
      });
    },
  }));
}
