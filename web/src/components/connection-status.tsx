import { createSignal, onCleanup, onMount, Show } from "solid-js";
import { queryClient } from "~/lib/queryClient";

type ConnectionState = "online" | "offline" | "reconnecting";

export default function ConnectionStatus() {
  const [state, setState] = createSignal<ConnectionState>(
    navigator.onLine ? "online" : "offline",
  );
  const label = () =>
    state() === "online"
      ? "Online"
      : state() === "offline"
        ? "Offline"
        : "Reconnecting";
  let reconnecting = false;

  const goOffline = () => setState("offline");
  const goOnline = async () => {
    if (reconnecting || state() !== "offline") return;
    reconnecting = true;
    setState("reconnecting");
    try {
      await queryClient.refetchQueries({ type: "active" });
    } finally {
      reconnecting = false;
      setState(navigator.onLine ? "online" : "offline");
    }
  };

  onMount(() => {
    window.addEventListener("offline", goOffline);
    window.addEventListener("online", goOnline);
  });
  onCleanup(() => {
    window.removeEventListener("offline", goOffline);
    window.removeEventListener("online", goOnline);
  });

  return (
    <div
      class="ml-auto flex min-w-0 items-center gap-2 text-xs text-muted-foreground"
      role="status"
      aria-label={label()}
      aria-live="polite"
      aria-atomic="true"
    >
      <span
        class="size-2 shrink-0 rounded-full bg-emerald-500 data-[state=offline]:bg-destructive data-[state=reconnecting]:animate-pulse data-[state=reconnecting]:bg-amber-500"
        data-state={state()}
        aria-hidden="true"
      />
      <span class="hidden sm:inline">
        <Show when={state() === "online"}>Online</Show>
        <Show when={state() === "offline"}>Offline</Show>
        <Show when={state() === "reconnecting"}>Reconnecting…</Show>
      </span>
      <span class="sr-only sm:hidden">
        {state() === "online"
          ? "Online"
          : state() === "offline"
            ? "Offline"
            : "Reconnecting"}
      </span>
    </div>
  );
}
