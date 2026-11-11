import { QueryClient } from "@tanstack/solid-query";
import { onAccessCleared } from "./auth";

export const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: Infinity,
      gcTime: 5 * 60_000,
      // `api.ts` owns bounded, status-aware transport retries. Retrying again here would replay
      // the complete retry sequence and multiply load on the embedded HTTP server.
      retry: false,
      refetchOnMount: false,
      refetchOnReconnect: false,
      refetchOnWindowFocus: false,
    },
    mutations: {
      // Execute once even when the browser reports offline. Pausing would replay a
      // stale command when connectivity returns.
      networkMode: "always",
      retry: false,
    },
  },
});

onAccessCleared(() => queryClient.clear());
