import { useSearchParams } from "@solidjs/router";
import { createMemo, createSignal, For, Show } from "solid-js";
import { createForm } from "solid-hook-form";
import { toast } from "solid-sonner";

import { Alert } from "~/components/alert";
import { Badge } from "~/components/badge";
import { Button } from "~/components/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "~/components/dialog";
import { MarkdownViewer } from "~/components/markdown-viewer";
import { PageHeader } from "~/components/page-header";
import { Spinner } from "~/components/spinner";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "~/components/tabs";
import {
  TextField,
  TextFieldErrorMessage,
  TextFieldLabel,
  TextFieldTextArea,
} from "~/components/text-field";
import type { AiMemorySection, AiPromptFile } from "~/lib/api";
import { bytesAtMost, entityLimits } from "~/lib/request-contracts";
import { createUnsavedChanges } from "~/lib/unsaved-changes";
import { aiMemoryMutation, aiPromptMutation } from "~/mutations/ai";
import { aiMemoryQuery, aiPromptsQuery } from "~/queries/ai";

type EditorTarget =
  | { kind: "prompt"; file: AiPromptFile }
  | { kind: "memory"; section: AiMemorySection };

const baselinePrompts: readonly AiPromptFile[] = [
  {
    path: "assistant/system.md",
    title: "Assistant system prompt",
    markdown:
      "# Core Pilot assistant\n\nHelp the user operate their local smart home clearly and safely. Prefer local state and enabled Core Pilot tools. Never invent device state or claim an action succeeded unless the tool result confirms it.",
    baseline: true,
  },
  {
    path: "assistant/safety.md",
    title: "Assistant safety policy",
    markdown:
      "# Safety policy\n\n- Keep credentials, tokens, setup payloads, and private keys out of responses.\n- Ask for explicit confirmation before destructive changes.\n- Do not weaken Matter, Thread, storage, or network security checks.\n- State when a service or device is unavailable instead of guessing.",
    baseline: true,
  },
];

export default function AiPrompts() {
  const promptsResult = aiPromptsQuery();
  const memoryResult = aiMemoryQuery();
  const promptMutation = aiPromptMutation();
  const memoryMutation = aiMemoryMutation();
  const [searchParams, setSearchParams] = useSearchParams();
  const section = () =>
    searchParams.section === "memory" ? "memory" : "prompts";
  const [target, setTarget] = createSignal<EditorTarget | null>(null);
  const [editorTab, setEditorTab] = createSignal("editor");
  const [previewOnly, setPreviewOnly] = createSignal(false);
  const defaultValues = { markdown: "" };
  const {
    register,
    reset: resetFields,
    values,
    errors,
    handleSubmit,
    formState,
  } = createForm({ defaultValues, mode: "onTouched" });
  const markdownField = register("markdown", {
    validate: (value) => {
      const memory = target()?.kind === "memory";
      const result = bytesAtMost(
        memory ? entityLimits.memoryMarkdown : entityLimits.promptMarkdown,
        memory
          ? "Saved context must be 12 KiB or less."
          : "Prompt must be 4 KiB or less.",
      ).safeParse(value);
      return result.success || result.error.issues[0]?.message;
    },
  });
  const markdownBytes = createMemo(
    () => new TextEncoder().encode(values().markdown).byteLength,
  );
  const markdownLimit = () =>
    target()?.kind === "memory"
      ? entityLimits.memoryMarkdown
      : entityLimits.promptMarkdown;
  const previewAllowed = () => markdownBytes() <= markdownLimit();
  const pending = () => promptMutation.isPending || memoryMutation.isPending;
  const contentVerified = (kind: EditorTarget["kind"]) => {
    const query = kind === "prompt" ? promptsResult : memoryResult;
    return (
      query.data !== undefined && !query.isPlaceholderData && !query.isError
    );
  };
  const saveAllowed = () => {
    const selected = target();
    return (
      selected !== null && !previewOnly() && contentVerified(selected.kind)
    );
  };
  function reset(markdown: string) {
    Object.assign(defaultValues, { markdown });
    resetFields({ markdown });
  }
  const unsaved = createUnsavedChanges({
    open: () => target() !== null,
    dirty: formState.isDirty,
    label: "assistant context",
  });
  function close() {
    setTarget(null);
    reset("");
    setEditorTab("editor");
    setPreviewOnly(false);
  }
  function requestClose() {
    if (!pending()) unsaved.request(close);
  }
  function edit(next: EditorTarget) {
    setPreviewOnly(!contentVerified(next.kind));
    setTarget(next);
    reset(next.kind === "prompt" ? next.file.markdown : next.section.markdown);
    setEditorTab("editor");
  }
  const save = handleSubmit(async ({ markdown }) => {
    const selected = target();
    if (!selected || pending() || !saveAllowed()) return;
    try {
      const response =
        selected.kind === "prompt"
          ? await promptMutation.mutateAsync({
              path: selected.file.path,
              markdown,
            })
          : await memoryMutation.mutateAsync({
              id: selected.section.id,
              markdown,
            });
      close();
      toast.success(response.detail ?? "Assistant context saved.");
    } catch (error) {
      toast.error(
        error instanceof Error
          ? error.message
          : "Assistant context could not be saved.",
      );
    }
  });
  const title = () => {
    const selected = target();
    return selected?.kind === "prompt"
      ? selected.file.title
      : selected?.section.title;
  };
  const identifier = () => {
    const selected = target();
    return selected?.kind === "prompt"
      ? selected.file.path
      : selected?.section.id;
  };
  const promptFolders = createMemo(() => {
    const files = new Map(baselinePrompts.map((file) => [file.path, file]));
    for (const file of promptsResult.data?.files ?? [])
      files.set(file.path, file);
    const folders = new Map<string, AiPromptFile[]>();
    for (const file of files.values()) {
      const folder = file.path.includes("/")
        ? file.path.slice(0, file.path.lastIndexOf("/"))
        : "root";
      const entries = folders.get(folder) ?? [];
      entries.push(file);
      folders.set(folder, entries);
    }
    return Array.from(folders, ([path, files]) => ({ path, files }));
  });
  const promptsLoading = () =>
    promptsResult.isFetching &&
    (promptsResult.isPlaceholderData || !promptsResult.data);
  const memoryLoading = () =>
    memoryResult.isFetching &&
    (memoryResult.isPlaceholderData || !memoryResult.data);

  return (
    <div class="space-y-6">
      <PageHeader
        title="Assistant Prompts"
        description="Prompt files define assistant instructions; saved context records information to remember. Both are included with chat and saved as encrypted SD records."
        actions={
          <>
            <Button as="a" href="#/ai" variant="outline" size="sm">
              Assistant
            </Button>
            <Button as="a" href="#/ai/tools" variant="outline" size="sm">
              Permissions
            </Button>
          </>
        }
      />
      <Tabs
        value={section()}
        onChange={(value) =>
          setSearchParams({
            section: value === "memory" ? "memory" : undefined,
          })
        }
      >
        <TabsList aria-label="Assistant context sections">
          <TabsTrigger value="prompts">Prompt files</TabsTrigger>
          <TabsTrigger value="memory">Saved context</TabsTrigger>
        </TabsList>
        <TabsContent value="prompts" class="space-y-5">
          <div>
            <h2 class="font-semibold">Prompt files</h2>
            <p class="text-sm text-muted-foreground">
              Baseline prompts become encrypted editable Markdown after they are
              saved.
            </p>
          </div>
          <Show when={promptsResult.isError}>
            <Alert>
              <p>
                {promptsResult.data
                  ? "Prompt files could not be refreshed. Showing cached files and built-in prompts; data may be stale."
                  : "Encrypted overrides could not be loaded. Only built-in prompts are shown."}
              </p>
              <Button
                variant="outline"
                size="sm"
                disabled={promptsResult.isFetching}
                onClick={() => void promptsResult.refetch()}
              >
                Retry
              </Button>
            </Alert>
          </Show>
          <Show
            when={!promptsLoading()}
            fallback={
              <p role="status" class="text-sm text-muted-foreground">
                Loading prompt files…
              </p>
            }
          >
            <For each={promptFolders()}>
              {(folder) => (
                <section class="space-y-2">
                  <h3 class="font-mono text-sm font-semibold">{folder.path}</h3>
                  <div class="divide-y border-y">
                    <For each={folder.files}>
                      {(file) => (
                        <Button
                          variant="ghost"
                          class="h-auto w-full justify-start gap-3 rounded-none px-0 py-4 text-left whitespace-normal"
                          onClick={() => edit({ kind: "prompt", file })}
                        >
                          <span class="min-w-0 flex-1">
                            <span class="block wrap-break-word font-semibold">
                              {file.title}
                            </span>
                            <span class="block wrap-break-word font-mono text-xs text-muted-foreground">
                              {file.path}
                            </span>
                          </span>
                          <Badge variant="secondary">
                            {file.baseline ? "baseline" : "custom"}
                          </Badge>
                        </Button>
                      )}
                    </For>
                  </div>
                </section>
              )}
            </For>
            <Show
              when={
                !promptsResult.isError &&
                !promptsResult.isPlaceholderData &&
                promptsResult.data?.detail
              }
            >
              <p class="text-sm text-muted-foreground">
                {promptsResult.data?.detail}
              </p>
            </Show>
          </Show>
        </TabsContent>
        <TabsContent value="memory" class="space-y-5">
          <div>
            <h2 class="font-semibold">Saved context</h2>
            <p class="text-sm text-muted-foreground">
              Information the assistant remembers, stored as encrypted Markdown
              sections.
            </p>
          </div>
          <Show when={memoryResult.isError}>
            <Alert>
              <p>
                {memoryResult.data
                  ? "Saved context could not be refreshed. Showing cached sections; data may be stale."
                  : "Saved context could not be loaded."}
              </p>
              <Button
                variant="outline"
                size="sm"
                disabled={memoryResult.isFetching}
                onClick={() => void memoryResult.refetch()}
              >
                Retry
              </Button>
            </Alert>
          </Show>
          <Show
            when={!memoryLoading()}
            fallback={
              <p role="status" class="text-sm text-muted-foreground">
                Loading saved context…
              </p>
            }
          >
            <Show when={memoryResult.data && !memoryResult.isPlaceholderData}>
              <div class="divide-y border-y">
                <For
                  each={memoryResult.data?.sections}
                  fallback={
                    <p class="py-4 text-sm text-muted-foreground">
                      No saved context sections are available.
                    </p>
                  }
                >
                  {(saved) => (
                    <Button
                      variant="ghost"
                      class="h-auto w-full justify-start rounded-none px-0 py-4 text-left whitespace-normal"
                      onClick={() => edit({ kind: "memory", section: saved })}
                    >
                      <span class="min-w-0">
                        <span class="block wrap-break-word font-semibold">
                          {saved.title}
                        </span>
                        <span class="block wrap-break-word font-mono text-xs text-muted-foreground">
                          {saved.id}
                        </span>
                      </span>
                    </Button>
                  )}
                </For>
              </div>
              <Show when={memoryResult.data?.detail}>
                <p class="text-sm text-muted-foreground">
                  {memoryResult.data?.detail}
                </p>
              </Show>
            </Show>
          </Show>
        </TabsContent>
      </Tabs>
      <Dialog
        open={target() !== null}
        onOpenChange={(open) => !open && requestClose()}
      >
        <DialogContent class="max-h-[calc(100dvh-2rem)] w-[calc(100vw-2rem)] max-w-3xl overflow-y-auto">
          <DialogHeader>
            <DialogTitle>{title()}</DialogTitle>
            <DialogDescription class="wrap-break-word font-mono">
              {identifier()}
            </DialogDescription>
          </DialogHeader>
          <Show when={target() !== null && !saveAllowed()}>
            <p role="status" class="text-sm text-muted-foreground">
              This is a read-only preview because current content could not be
              verified. Close it, retry loading, and reopen the current entry
              before saving.
            </p>
          </Show>
          <form onSubmit={save} class="min-w-0 space-y-4">
            <Tabs
              value={editorTab()}
              onChange={(value) => !pending() && setEditorTab(value)}
            >
              <TabsList aria-label="Assistant context editor tabs">
                <TabsTrigger value="editor" disabled={pending()}>
                  Editor
                </TabsTrigger>
                <TabsTrigger value="preview" disabled={pending()}>
                  Preview
                </TabsTrigger>
              </TabsList>
              <TabsContent
                value="editor"
                forceMount
                hidden={editorTab() !== "editor"}
              >
                <TextField
                  value={values().markdown}
                  validationState={errors.markdown ? "invalid" : "valid"}
                  disabled={pending()}
                >
                  <TextFieldLabel class="sr-only">
                    {target()?.kind === "memory"
                      ? "Saved context Markdown"
                      : "Prompt Markdown"}
                  </TextFieldLabel>
                  <TextFieldTextArea
                    {...markdownField}
                    value={values().markdown}
                    disabled={pending()}
                    readOnly={!saveAllowed()}
                    class="max-h-[50dvh] min-h-64 font-mono text-sm leading-relaxed sm:min-h-80"
                  />
                  <Show when={errors.markdown?.message}>
                    {(message) => (
                      <TextFieldErrorMessage>{message()}</TextFieldErrorMessage>
                    )}
                  </Show>
                </TextField>
              </TabsContent>
              <TabsContent value="preview">
                <div
                  class="max-h-[50dvh] min-h-64 overflow-auto rounded-md border p-4 sm:min-h-80"
                  role="region"
                  aria-label="Markdown preview"
                  tabIndex={0}
                >
                  <Show
                    when={previewAllowed()}
                    fallback={
                      <p role="status" class="text-sm text-muted-foreground">
                        {target()?.kind === "memory"
                          ? "Saved context must be 12 KiB or less."
                          : "Prompt must be 4 KiB or less."}{" "}
                        Reduce the draft in Editor to preview it.
                      </p>
                    }
                  >
                    <MarkdownViewer markdown={values().markdown} />
                  </Show>
                </div>
              </TabsContent>
            </Tabs>
            <DialogFooter>
              <Button
                type="button"
                variant="outline"
                disabled={pending()}
                onClick={requestClose}
              >
                Cancel
              </Button>
              <Button type="submit" disabled={pending() || !saveAllowed()}>
                <Show when={pending()}>
                  <Spinner />
                </Show>
                Save encrypted
              </Button>
            </DialogFooter>
          </form>
        </DialogContent>
      </Dialog>
      <unsaved.Confirmation />
    </div>
  );
}
