import { createMutation, useQueryClient } from "@tanstack/solid-query";
import {
  prepareOta,
  uploadOtaChunk,
  type OtaChunkRequest,
  type OtaPrepareRequest,
} from "../lib/api";

export function otaPrepareMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["ota", "prepare"],
    mutationFn: (request: OtaPrepareRequest) => prepareOta(request),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ["system"] });
    },
  }));
}

export function otaChunkMutation() {
  return createMutation(() => ({
    mutationKey: ["ota", "chunk"],
    mutationFn: (request: OtaChunkRequest) => uploadOtaChunk(request),
  }));
}
