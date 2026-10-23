import { createQuery } from "@tanstack/solid-query";
import {
  fallbackAi,
  loadAi,
  loadAiMemory,
  loadAiPrompts,
  loadMcpContext,
  loadMcpTools,
} from "../lib/api";

export function aiQuery() {
  return createQuery(() => ({
    queryKey: ["ai"],
    queryFn: ({ signal }) => loadAi(signal),
    placeholderData: fallbackAi,
  }));
}

export function aiMemoryQuery() {
  return createQuery(() => ({
    queryKey: ["ai", "memory"],
    queryFn: ({ signal }) => loadAiMemory(signal),
    placeholderData: {
      sections: [],
      encrypted: false,
      detail: "Loading memory.",
    },
  }));
}

export function aiPromptsQuery() {
  return createQuery(() => ({
    queryKey: ["ai", "prompts"],
    queryFn: ({ signal }) => loadAiPrompts(signal),
    placeholderData: {
      files: [],
      encrypted: false,
      detail: "Loading prompts.",
    },
  }));
}

export function mcpToolsQuery() {
  return createQuery(() => ({
    queryKey: ["ai", "mcp", "tools"],
    queryFn: ({ signal }) => loadMcpTools(signal),
    placeholderData: { tools: [] },
  }));
}

export function mcpContextQuery() {
  return createQuery(() => ({
    queryKey: ["ai", "mcp", "context"],
    queryFn: ({ signal }) => loadMcpContext(signal),
    placeholderData: { system_prompt: "", context_json: "{}", tools: [] },
  }));
}
