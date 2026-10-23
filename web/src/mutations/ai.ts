import { createMutation, useQueryClient } from "@tanstack/solid-query";
import {
  invokeMcpTool,
  requireAccepted,
  updateMcpToolPermission,
  saveAiMemorySection,
  saveAiPromptFile,
  sendAiChat,
  setupAiProvider,
  type AiChatRequest,
  type AiMemoryUpdateRequest,
  type AiPromptUpdateRequest,
  type AiProviderSetupRequest,
  type McpInvokeRequest,
  type McpToolPermissionUpdateRequest,
} from "../lib/api";

export function aiProviderMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["ai", "provider"],
    mutationFn: async (request: AiProviderSetupRequest) =>
      requireAccepted(await setupAiProvider(request)),
    onSuccess: () => {
      void queryClient.invalidateQueries({
        queryKey: ["ai"],
        exact: true,
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["system"],
        refetchType: "all",
      });
    },
  }));
}

export function aiChatMutation() {
  return createMutation(() => ({
    mutationKey: ["ai", "chat"],
    mutationFn: async (request: AiChatRequest) =>
      requireAccepted(await sendAiChat(request)),
  }));
}

export function aiMemoryMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["ai", "memory"],
    mutationFn: async (request: AiMemoryUpdateRequest) =>
      requireAccepted(await saveAiMemorySection(request)),
    onSuccess: () => {
      void queryClient.invalidateQueries({
        queryKey: ["ai", "memory"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["storage"],
        refetchType: "all",
      });
    },
  }));
}

export function aiPromptMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["ai", "prompts"],
    mutationFn: async (request: AiPromptUpdateRequest) =>
      requireAccepted(await saveAiPromptFile(request)),
    onSuccess: () => {
      void queryClient.invalidateQueries({
        queryKey: ["ai", "prompts"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["ai", "mcp", "context"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["storage"],
        refetchType: "all",
      });
    },
  }));
}

export function mcpToolPermissionMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["ai", "mcp", "tool"],
    mutationFn: async (request: McpToolPermissionUpdateRequest) =>
      requireAccepted(await updateMcpToolPermission(request)),
    onSuccess: () => {
      void queryClient.invalidateQueries({
        queryKey: ["ai", "mcp", "tools"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["ai", "mcp", "context"],
        refetchType: "all",
      });
      void queryClient.invalidateQueries({
        queryKey: ["storage"],
        refetchType: "all",
      });
    },
  }));
}

export function mcpInvokeMutation() {
  return createMutation(() => ({
    mutationKey: ["ai", "mcp", "invoke"],
    mutationFn: async (request: McpInvokeRequest) =>
      requireAccepted(await invokeMcpTool(request)),
  }));
}
