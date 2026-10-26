import { createMutation, useQueryClient } from "@tanstack/solid-query";
import {
  forgetWifiNetwork,
  provisionWifi,
  type WifiForgetRequest,
  type WifiProvisionRequest,
} from "../lib/api";

export function wifiForgetMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["network", "wifi", "forget"],
    mutationFn: async (request: WifiForgetRequest) => {
      const response = await forgetWifiNetwork(request);
      if (!response.accepted) throw new Error(response.detail);
      return response;
    },
    onSuccess: () => {
      void queryClient.invalidateQueries({
        queryKey: ["network", "wifi", "saved"],
      });
      void queryClient.invalidateQueries({ queryKey: ["network"] });
    },
  }));
}

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
      void queryClient.invalidateQueries({
        queryKey: ["network", "wifi", "saved"],
      });
      void queryClient.invalidateQueries({ queryKey: ["system"] });
      void queryClient.invalidateQueries({ queryKey: ["storage"] });
    },
  }));
}
