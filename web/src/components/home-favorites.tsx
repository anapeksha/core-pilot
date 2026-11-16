import {
  createEffect,
  createMemo,
  createSignal,
  For,
  type JSX,
  onMount,
  onCleanup,
  Show,
} from "solid-js";
import { HiOutlineStar } from "solid-icons/hi";
import { Button } from "~/components/button";
import { CardQueryContent } from "~/components/card-query-content";
import {
  Card,
  CardDescription,
  CardHeader,
  CardTitle,
} from "~/components/card";
import { Flex } from "~/components/flex";
import { Skeleton } from "~/components/skeleton";
import { cn } from "~/lib/utils";
import {
  devicesQuery as createDevicesQuery,
  fallbackDevices,
} from "~/queries/devices";
import {
  fallbackScenes,
  scenesQuery as createScenesQuery,
} from "~/queries/scenes";

const FAVORITES_KEY = "core-pilot-home-favorites";
const MAX_FAVORITES = 8;

function FavoritesSkeleton() {
  return (
    <div class="space-y-2">
      <For each={[0, 1]}>
        {() => (
          <div class="min-w-0 space-y-2 rounded-md border p-3">
            <Skeleton height={16} class="w-3/5" />
            <Skeleton height={12} class="w-2/5" />
          </div>
        )}
      </For>
    </div>
  );
}

export function HomeFavorites() {
  const devicesQuery = createDevicesQuery();
  const scenesQuery = createScenesQuery();
  const [favoriteKeys, setFavoriteKeys] = createSignal<string[]>([]);
  const [editingFavorites, setEditingFavorites] = createSignal(false);
  const [favoritesFeedback, setFavoritesFeedback] = createSignal("");
  let favoritesToggle: HTMLButtonElement | undefined;
  let favoritesPanel: HTMLDivElement | undefined;
  let pinFavorites: HTMLButtonElement | undefined;
  const favoritesBlocked = () =>
    devicesQuery.isPlaceholderData ||
    scenesQuery.isPlaceholderData ||
    !!devicesQuery.error ||
    !!scenesQuery.error;
  createEffect(() => {
    if (favoritesBlocked()) setEditingFavorites(false);
  });
  const FavoritesEditor = (props: { children: JSX.Element }) => {
    onCleanup(() => {
      if (
        favoritesBlocked() &&
        (favoritesPanel?.contains(document.activeElement) ||
          document.activeElement === favoritesToggle)
      ) {
        favoritesPanel?.focus();
      }
    });
    return props.children;
  };
  const changeFavoritesMode = (editing: boolean) => {
    if (editing && favoritesBlocked()) return;
    setEditingFavorites(editing);
    queueMicrotask(() => {
      if (editing) favoritesPanel?.focus();
      else favoritesToggle?.focus();
    });
  };
  const devices = () => devicesQuery.data?.devices ?? fallbackDevices.devices;
  const scenes = () => scenesQuery.data?.scenes ?? fallbackScenes.scenes;

  const favoriteInventory = createMemo(() => [
    ...devices().map((device) => ({
      key: `device:${typeof device.id === "string" ? device.id : device.id[0]}`,
      name: device.name,
      kind: "device",
      href: "#/devices",
    })),
    ...scenes().map((scene) => ({
      key: `scene:${scene.id}`,
      name: scene.name,
      kind: "scene",
      href: "#/scenes",
    })),
  ]);

  const normalizeFavorites = (values: unknown[]): string[] =>
    [
      ...new Set(
        values.filter(
          (value): value is string =>
            typeof value === "string" && /^(device|scene):\S+$/.test(value),
        ),
      ),
    ].slice(0, MAX_FAVORITES);

  onMount(() => {
    try {
      const stored: unknown = JSON.parse(
        localStorage.getItem(FAVORITES_KEY) ?? "[]",
      );
      if (!Array.isArray(stored)) throw new Error("Invalid favorites");
      const normalized = normalizeFavorites(stored);
      setFavoriteKeys(normalized);
      if (JSON.stringify(normalized) !== JSON.stringify(stored))
        setFavoritesFeedback(
          "Invalid or duplicate favorites were ignored. Change a selection to save the cleaned list.",
        );
    } catch {
      setFavoritesFeedback(
        "Saved favorites could not be read. You can select favorites for this visit; saving may be unavailable.",
      );
    }
  });

  const saveFavorites = (favorites: string[]) => {
    const bounded = normalizeFavorites(favorites);
    setFavoriteKeys(bounded);
    try {
      localStorage.setItem(FAVORITES_KEY, JSON.stringify(bounded));
      setFavoritesFeedback("Favorites saved in this browser.");
    } catch {
      setFavoritesFeedback(
        "Favorites changed for this visit only. Browser storage could not save them.",
      );
    }
  };

  const toggleFavorite = (key: string) => {
    const current = favoriteKeys();
    saveFavorites(
      current.includes(key)
        ? current.filter((favorite) => favorite !== key)
        : [...current, key],
    );
  };

  const removeUnavailableFavorite = (key: string) => {
    toggleFavorite(key);
    if (!editingFavorites() && favoriteKeys().length === 0 && pinFavorites)
      pinFavorites.focus();
    else favoritesPanel?.focus();
  };

  return (
    <Card class="@container/card min-w-0">
      <CardHeader>
        <Flex class="flex-wrap items-start gap-3">
          <div class="min-w-0 flex-1">
            <CardTitle>Favorites</CardTitle>
            <CardDescription>
              Up to eight shortcuts, saved in this browser only
            </CardDescription>
          </div>
          <Button
            ref={(element) => (favoritesToggle = element)}
            type="button"
            size="sm"
            variant="outline"
            disabled={favoritesBlocked()}
            aria-expanded={editingFavorites()}
            aria-controls="home-favorites-panel"
            onClick={() => changeFavoritesMode(!editingFavorites())}
          >
            {editingFavorites() ? "Done" : "Edit favorites"}
          </Button>
        </Flex>
      </CardHeader>
      <div
        id="home-favorites-panel"
        ref={(element) => (favoritesPanel = element)}
        tabindex="-1"
        class="min-w-0 space-y-3"
        role="region"
        aria-label={editingFavorites() ? "Edit favorites" : "Saved favorites"}
      >
        <CardQueryContent
          label="favorites"
          queries={[devicesQuery, scenesQuery]}
          fallback={<FavoritesSkeleton />}
        >
          <Show
            when={editingFavorites()}
            fallback={
              <Show
                when={favoriteKeys().length > 0}
                fallback={
                  <Button
                    ref={(element) => (pinFavorites = element)}
                    type="button"
                    disabled={favoritesBlocked()}
                    onClick={() => changeFavoritesMode(true)}
                  >
                    Pin favorites
                  </Button>
                }
              >
                <p class="text-xs text-muted-foreground">
                  Shortcuts open the device or scene list; they do not run
                  actions.
                </p>
                <ul class="space-y-2">
                  <For each={favoriteKeys()}>
                    {(key) => {
                      const item = () =>
                        favoriteInventory().find((entry) => entry.key === key);
                      return (
                        <li
                          class="min-w-0 rounded-md border p-3"
                          data-home-favorite={key}
                        >
                          <Show
                            when={item()}
                            fallback={
                              <div class="space-y-2">
                                <p class="wrap-break-word text-sm">
                                  Unavailable favorite: {key}
                                </p>
                                <Button
                                  size="sm"
                                  variant="outline"
                                  onClick={() => removeUnavailableFavorite(key)}
                                  aria-label={`Remove unavailable favorite ${key}`}
                                >
                                  Remove favorite
                                </Button>
                              </div>
                            }
                          >
                            {(entry) => (
                              <a
                                href={entry().href}
                                class="block min-w-0 whitespace-normal wrap-break-word text-sm underline-offset-4 hover:underline"
                              >
                                <span class="block font-medium">
                                  {entry().name}
                                </span>
                                <span class="text-xs text-muted-foreground">
                                  Open {entry().kind} · View {entry().kind} list
                                </span>
                              </a>
                            )}
                          </Show>
                        </li>
                      );
                    }}
                  </For>
                </ul>
              </Show>
            }
          >
            <FavoritesEditor>
              <p class="text-sm text-muted-foreground">
                {favoriteKeys().length} of {MAX_FAVORITES} selected. Remove a
                favorite before adding another at the limit.
              </p>
              <div class="space-y-2">
                <For each={favoriteInventory()}>
                  {(item) => (
                    <Button
                      type="button"
                      variant={
                        favoriteKeys().includes(item.key)
                          ? "secondary"
                          : "outline"
                      }
                      class="h-auto w-full min-w-0 justify-start whitespace-normal py-3 text-left"
                      aria-pressed={favoriteKeys().includes(item.key)}
                      disabled={
                        !favoriteKeys().includes(item.key) &&
                        favoriteKeys().length >= MAX_FAVORITES
                      }
                      onClick={() => toggleFavorite(item.key)}
                    >
                      <HiOutlineStar
                        class={cn(
                          "shrink-0",
                          favoriteKeys().includes(item.key) && "fill-current",
                        )}
                        data-icon="inline-start"
                        aria-hidden="true"
                      />
                      <span class="min-w-0 wrap-break-word">
                        {item.name} · {item.kind}
                      </span>
                    </Button>
                  )}
                </For>
                <For
                  each={favoriteKeys().filter(
                    (key) =>
                      !favoriteInventory().some((item) => item.key === key),
                  )}
                >
                  {(key) => (
                    <Button
                      class="h-auto w-full min-w-0 whitespace-normal py-3"
                      variant="outline"
                      onClick={() => removeUnavailableFavorite(key)}
                    >
                      <span class="min-w-0 wrap-break-word">
                        Remove unavailable favorite: {key}
                      </span>
                    </Button>
                  )}
                </For>
              </div>
              <Show when={favoriteInventory().length === 0}>
                <p class="text-sm text-muted-foreground">
                  No devices or scenes to pin yet.
                </p>
                <Flex class="flex-wrap gap-2" justifyContent="start">
                  <Button as="a" href="#/devices/add" size="sm">
                    Add device
                  </Button>
                  <Button as="a" href="#/scenes" size="sm" variant="outline">
                    Create scene
                  </Button>
                </Flex>
              </Show>
            </FavoritesEditor>
          </Show>
        </CardQueryContent>
      </div>
      <Show when={favoritesFeedback()}>
        <p role="status" class="px-6 pb-4 text-xs text-muted-foreground">
          {favoritesFeedback()}
        </p>
      </Show>
    </Card>
  );
}
