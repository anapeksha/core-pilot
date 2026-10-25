import AccessGate from "~/components/access-gate";
import { HashRouter } from "@solidjs/router";
import { QueryClientProvider } from "@tanstack/solid-query";
import {
  ColorModeProvider,
  ColorModeScript,
  createLocalStorageManager,
} from "@kobalte/core";
import { FatalErrorBoundary } from "~/components/fatal-error-boundary";
import { Toaster } from "~/components/sonner";
import { queryClient } from "~/lib/queryClient";
import Shell from "~/Shell";
import AppRoutes from "~/routes";

const colorModeManager = createLocalStorageManager("core-pilot-color-mode");

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <ColorModeScript storageType="localStorage" initialColorMode="dark" />
      <ColorModeProvider
        initialColorMode="dark"
        storageManager={colorModeManager}
      >
        <FatalErrorBoundary>
          <AccessGate>
            <HashRouter root={Shell}>
              <AppRoutes />
            </HashRouter>
          </AccessGate>
        </FatalErrorBoundary>
        <Toaster
          richColors
          closeButton
          position="top-right"
          duration={5_000}
          visibleToasts={4}
        />
      </ColorModeProvider>
    </QueryClientProvider>
  );
}
