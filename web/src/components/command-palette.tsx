import { useNavigate } from "@solidjs/router";
import {
  createEffect,
  createMemo,
  createSignal,
  For,
  onCleanup,
  onMount,
} from "solid-js";
import { HiOutlineCommandLine, HiOutlineMagnifyingGlass } from "solid-icons/hi";
import { toast } from "solid-sonner";
import { Button } from "~/components/button";
import { TooltipIconButton } from "~/components/tooltip";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "~/components/dialog";
import { commandsFor } from "~/lib/commands";
import { automationRunMutation } from "~/mutations/models";
import { fallbackAutomations, automationsQuery } from "~/queries/automations";
import { fallbackDevices, devicesQuery } from "~/queries/devices";
import { fallbackRooms, roomsQuery } from "~/queries/rooms";
import { fallbackScenes, scenesQuery } from "~/queries/scenes";

const isEditable = (target: EventTarget | null) => {
  if (!(target instanceof HTMLElement)) return false;
  return (
    target.isContentEditable ||
    ["INPUT", "TEXTAREA", "SELECT"].includes(target.tagName)
  );
};

export function CommandPalette() {
  const navigate = useNavigate();
  const devices = devicesQuery();
  const rooms = roomsQuery();
  const scenes = scenesQuery();
  const automations = automationsQuery();
  const runMutation = automationRunMutation();
  const [open, setOpen] = createSignal(false);
  const [query, setQuery] = createSignal("");
  const [selectedIndex, setSelectedIndex] = createSignal(0);
  let input: HTMLInputElement | undefined;

  const results = createMemo(() =>
    commandsFor(
      {
        devices: devices.data?.devices ?? fallbackDevices.devices,
        rooms: rooms.data?.rooms ?? fallbackRooms.rooms,
        scenes: scenes.data?.scenes ?? fallbackScenes.scenes,
        automations:
          automations.data?.automations ?? fallbackAutomations.automations,
      },
      query(),
    ),
  );

  createEffect(() => {
    query();
    setSelectedIndex(0);
  });

  createEffect(() => {
    if (open()) queueMicrotask(() => input?.focus());
  });

  onMount(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      if (
        event.key.toLowerCase() === "k" &&
        (event.metaKey || event.ctrlKey) &&
        !isEditable(event.target)
      ) {
        event.preventDefault();
        setOpen(true);
      }
    };
    window.addEventListener("keydown", handleKeyDown);
    onCleanup(() => window.removeEventListener("keydown", handleKeyDown));
  });

  const close = () => {
    setOpen(false);
    setQuery("");
  };

  const execute = async (index: number) => {
    const command = results()[index];
    if (!command || command.action === "unavailable") return;
    if (command.action === "navigate" && command.path) {
      close();
      navigate(command.path);
      return;
    }
    if (command.action === "run-automation" && command.automationId) {
      try {
        const result = await runMutation.mutateAsync({
          id: command.automationId,
        });
        toast.success(result.detail ?? "Automation queued.");
        close();
      } catch (error) {
        toast.error(
          error instanceof Error
            ? error.message
            : "Automation could not be queued.",
        );
      }
    }
  };

  const onInputKeyDown = (
    event: KeyboardEvent & { currentTarget: HTMLInputElement },
  ) => {
    const count = results().length;
    if (event.key === "ArrowDown" && count) {
      event.preventDefault();
      setSelectedIndex((index) => (index + 1) % count);
    } else if (event.key === "ArrowUp" && count) {
      event.preventDefault();
      setSelectedIndex((index) => (index - 1 + count) % count);
    } else if (event.key === "Enter") {
      event.preventDefault();
      void execute(selectedIndex());
    } else if (event.key === "Escape") {
      event.preventDefault();
      close();
    }
  };

  return (
    <>
      <Button
        variant="outline"
        size="sm"
        class="ml-auto hidden gap-2 sm:inline-flex"
        aria-label="Open command palette"
        onClick={() => setOpen(true)}
      >
        <HiOutlineMagnifyingGlass />
        Search
        <kbd class="rounded border bg-muted px-1.5 py-0.5 text-[0.65rem] text-muted-foreground">
          ⌘K
        </kbd>
      </Button>
      <TooltipIconButton
        variant="ghost"
        size="icon"
        class="ml-auto sm:hidden"
        aria-label="Open command palette"
        onClick={() => setOpen(true)}
      >
        <HiOutlineCommandLine aria-hidden="true" />
      </TooltipIconButton>
      <Dialog open={open()} onOpenChange={setOpen}>
        <DialogContent class="max-w-xl gap-3 p-4">
          <DialogHeader class="sr-only">
            <DialogTitle>Command palette</DialogTitle>
            <DialogDescription>
              Search dashboard pages and local models.
            </DialogDescription>
          </DialogHeader>
          <label class="flex items-center gap-2 border-b pb-3">
            <HiOutlineMagnifyingGlass class="size-5 text-muted-foreground" />
            <span class="sr-only">Search commands</span>
            <input
              ref={(element) => (input = element)}
              value={query()}
              onInput={(event) => setQuery(event.currentTarget.value)}
              onKeyDown={onInputKeyDown}
              class="h-9 w-full bg-transparent text-sm outline-none placeholder:text-muted-foreground"
              placeholder="Search pages, devices, rooms, scenes, or automations…"
              role="combobox"
              aria-label="Search commands"
              aria-autocomplete="list"
              aria-expanded={open()}
              aria-controls="command-results"
              aria-activedescendant={
                results()[selectedIndex()]?.id
                  ? `command-${results()[selectedIndex()].id}`
                  : undefined
              }
            />
          </label>
          <div
            id="command-results"
            role="listbox"
            class="max-h-80 overflow-y-auto"
          >
            <For
              each={results()}
              fallback={
                <p class="px-3 py-6 text-center text-sm text-muted-foreground">
                  No matching commands.
                </p>
              }
            >
              {(command, index) => {
                const unavailable = command.action === "unavailable";
                return (
                  <button
                    id={`command-${command.id}`}
                    type="button"
                    role="option"
                    aria-selected={selectedIndex() === index()}
                    disabled={unavailable || runMutation.isPending}
                    class="flex w-full flex-col rounded-md px-3 py-2 text-left text-sm outline-none hover:bg-accent focus:bg-accent disabled:cursor-not-allowed disabled:opacity-60"
                    classList={{ "bg-accent": selectedIndex() === index() }}
                    onMouseEnter={() => setSelectedIndex(index())}
                    onClick={() => void execute(index())}
                  >
                    <span class="font-medium">{command.title}</span>
                    <span class="text-xs text-muted-foreground">
                      {command.detail}
                    </span>
                  </button>
                );
              }}
            </For>
          </div>
          <p class="text-xs text-muted-foreground">
            <span class="font-medium text-foreground">↑↓</span> to move ·{" "}
            <span class="font-medium text-foreground">Enter</span> to select ·{" "}
            <span class="font-medium text-foreground">Esc</span> to close
          </p>
        </DialogContent>
      </Dialog>
    </>
  );
}
