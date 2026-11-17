import { createMutation, useQueryClient } from "@tanstack/solid-query";
import {
  invokeMcpTool,
  removeAiMemory,
  type AiMemoryRemoveRequest,
  type AiMemoryResponse,
  type AiPromptDirectoryResponse,
  requireAccepted,
  updateMcpToolPermission,
  saveAiMemorySection,
  saveAiPromptFile,
  sendAiChat,
  setupAiProvider,
  type AiChatRequest,
  type AiChatResponse,
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

export type AiChatMutationInput = {
  request: AiChatRequest;
  onStatus: (response: AiChatResponse) => void;
};

export function aiChatMutation() {
  return createMutation(() => ({
    mutationKey: ["ai", "chat"],
    mutationFn: async ({ request, onStatus }: AiChatMutationInput) =>
      requireAccepted(await sendAiChat(request, onStatus)),
  }));
}

export function aiMemoryMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["ai", "memory"],
    mutationFn: async (request: AiMemoryUpdateRequest) =>
      requireAccepted(await saveAiMemorySection(request)),
    onSuccess: async (_response, request) => {
      await queryClient.cancelQueries({
        queryKey: ["ai", "memory"],
        exact: true,
      });
      queryClient.setQueryData<AiMemoryResponse>(
        ["ai", "memory"],
        (previous) =>
          previous && {
            ...previous,
            encrypted: true,
            detail: "Encrypted AI memory saved.",
            sections: previous.sections.some(
              (section) => section.id === request.id,
            )
              ? previous.sections.map((section) =>
                  section.id === request.id
                    ? { ...section, markdown: request.markdown }
                    : section,
                )
              : [...previous.sections, { ...request, title: request.id }],
          },
      );
      void queryClient.invalidateQueries({
        queryKey: ["storage"],
        refetchType: "all",
      });
    },
  }));
}

export function aiMemoryRemoveMutation() {
  const queryClient = useQueryClient();
  return createMutation(() => ({
    mutationKey: ["ai", "memory", "remove"],
    mutationFn: async (request: AiMemoryRemoveRequest) =>
      requireAccepted(await removeAiMemory(request)),
    onSuccess: async (_response, request) => {
      await queryClient.cancelQueries({
        queryKey: ["ai", "memory"],
        exact: true,
      });
      queryClient.setQueryData<AiMemoryResponse>(
        ["ai", "memory"],
        (previous) =>
          previous && {
            ...previous,
            sections: previous.sections.filter(
              (section) => section.id !== request.id,
            ),
          },
      );
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
    onSuccess: async (_response, request) => {
      await queryClient.cancelQueries({
        queryKey: ["ai", "prompts"],
        exact: true,
      });
      queryClient.setQueryData<AiPromptDirectoryResponse>(
        ["ai", "prompts"],
        (previous) =>
          previous && {
            ...previous,
            encrypted: true,
            detail: "Encrypted AI prompt saved.",
            files: previous.files.some((file) => file.path === request.path)
              ? previous.files.map((file) =>
                  file.path === request.path
                    ? { ...file, markdown: request.markdown, baseline: false }
                    : file,
                )
              : [
                  ...previous.files,
                  {
                    path: request.path,
                    title: request.path,
                    markdown: request.markdown,
                    baseline: false,
                  },
                ],
          },
      );
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
