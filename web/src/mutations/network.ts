import { createMutation, useQueryClient } from "@tanstack/solid-query";
import { provisionWifi, type WifiProvisionRequest } from "../lib/api";

export function wifiProvisionMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["network", "wifi"],
    mutationFn: async (request: WifiProvisionRequest) => {
      const response = await provisionWifi(request);
      if (!response.accepted) {
        throw new Error(response.detail);
      }
      return response;
    },
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ["network"] });
      void queryClient.invalidateQueries({ queryKey: ["system"] });
      void queryClient.invalidateQueries({ queryKey: ["storage"] });
    },
  }));
}
