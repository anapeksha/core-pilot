import { createEffect, createMemo, createSignal, For, Show } from "solid-js";
import { toast } from "solid-sonner";

import { Alert } from "~/components/alert";
import { Badge } from "~/components/badge";
import { Button } from "~/components/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "~/components/card";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "~/components/dialog";
import { Flex } from "~/components/flex";
import { Grid } from "~/components/grid";
import { Skeleton } from "~/components/skeleton";
import { Spinner } from "~/components/spinner";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "~/components/tabs";
import {
  TextField,
  TextFieldLabel,
  TextFieldTextArea,
} from "~/components/text-field";
import type { AiPromptFile } from "~/lib/api";
import { createToastGate } from "~/lib/toast";
import { aiPromptMutation } from "~/mutations/ai";
import { aiPromptsQuery } from "~/queries/ai";

type PromptTab = "editor" | "preview";

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

function escapeHtml(value: string): string {
  return value
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function renderInlineMarkdown(value: string): string {
  return escapeHtml(value)
    .replace(/`([^`]+)`/g, "<code>$1</code>")
    .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
}

function renderPromptPreview(markdown: string): string {
  const blocks: string[] = [];
  const listItems: string[] = [];
  let codeLines: string[] | null = null;
  const flushList = () => {
    if (listItems.length)
      blocks.push(`<ul>${listItems.splice(0).join("")}</ul>`);
  };

  for (const rawLine of markdown.split("\n")) {
    const line = rawLine.trimEnd();
    if (line.startsWith("```")) {
      if (codeLines) {
        blocks.push(
          `<pre><code>${escapeHtml(codeLines.join("\n"))}</code></pre>`,
        );
        codeLines = null;
      } else {
        flushList();
        codeLines = [];
      }
      continue;
    }
    if (codeLines) {
      codeLines.push(line);
      continue;
    }
    const trimmed = line.trim();
    if (!trimmed) {
      flushList();
      continue;
    }
    const listMatch = /^[-*]\s+(.+)$/.exec(trimmed);
    if (listMatch) {
      listItems.push(`<li>${renderInlineMarkdown(listMatch[1])}</li>`);
      continue;
    }
    flushList();
    const headingMatch = /^(#{1,3})\s+(.+)$/.exec(trimmed);
    if (headingMatch) {
      const level = headingMatch[1].length;
      blocks.push(
        `<h${level}>${renderInlineMarkdown(headingMatch[2])}</h${level}>`,
      );
    } else {
      blocks.push(`<p>${renderInlineMarkdown(trimmed)}</p>`);
    }
  }
  flushList();
  if (codeLines)
    blocks.push(`<pre><code>${escapeHtml(codeLines.join("\n"))}</code></pre>`);
  return blocks.join("");
}

export default function AiPrompts() {
  const promptsResult = aiPromptsQuery();
  const promptMutation = aiPromptMutation();
  const [selectedPrompt, setSelectedPrompt] = createSignal<AiPromptFile | null>(
    null,
  );
  const [promptDraft, setPromptDraft] = createSignal("");
  const [promptTab, setPromptTab] = createSignal<PromptTab>("editor");
  const shouldShowToast = createToastGate();

  const promptFolders = createMemo(() => {
    const encryptedByPath = new Map(
      (promptsResult.data?.files ?? []).map((file) => [file.path, file]),
    );
    const merged = baselinePrompts.map(
      (baseline) => encryptedByPath.get(baseline.path) ?? baseline,
    );
    for (const file of encryptedByPath.values()) {
      if (!baselinePrompts.some((baseline) => baseline.path === file.path))
        merged.push(file);
    }

    const folders: Record<string, AiPromptFile[]> = {};
    for (const file of merged) {
      const folder = file.path.includes("/")
        ? file.path.split("/").slice(0, -1).join("/")
        : "root";
      (folders[folder] ??= []).push(file);
    }
    return Object.entries(folders).map(([path, files]) => ({ path, files }));
  });

  const closePromptEditor = () => {
    setSelectedPrompt(null);
    setPromptDraft("");
    setPromptTab("editor");
  };
  const editPrompt = (file: AiPromptFile) => {
    setSelectedPrompt(file);
    setPromptDraft(file.markdown);
    setPromptTab("editor");
  };
  const savePrompt = () => {
    const file = selectedPrompt();
    if (!file) return;
    promptMutation.mutate(
      { path: file.path, markdown: promptDraft() },
      { onSuccess: closePromptEditor },
    );
  };

  createEffect(() => {
    const message = promptMutation.isSuccess
      ? (promptMutation.data?.detail ?? "Prompt file saved.")
      : promptMutation.isError
        ? (promptMutation.error?.message ?? "Prompt file could not be saved.")
        : "";
    const key = `${promptMutation.isSuccess ? "success" : "error"}:${message}`;
    if (!shouldShowToast(message ? key : "")) return;
    if (promptMutation.isSuccess) toast.success(message);
    if (promptMutation.isError) toast.error(message);
  });

  const loading = () => promptsResult.isFetching && !promptsResult.data;

  return (
    <>
      <Card class="w-full overflow-hidden">
        <CardHeader class="gap-4 sm:flex-row sm:items-start sm:justify-between">
          <div class="max-w-3xl">
            <div class="mb-3 h-px w-12 bg-foreground" />
            <h1 class="text-xl font-semibold tracking-tight">
              Assistant Prompts
            </h1>
            <CardDescription>
              Review and edit the encrypted instructions used by the optional
              assistant.
            </CardDescription>
          </div>
          <Flex class="w-full flex-wrap gap-2 sm:w-auto">
            <Button as="a" href="#/ai" variant="outline" size="sm">
              Assistant
            </Button>
            <Button as="a" href="#/ai/tools" variant="outline" size="sm">
              Permissions
            </Button>
          </Flex>
        </CardHeader>
        <CardContent class="space-y-5">
          <Flex
            alignItems="start"
            class="flex-col gap-3 sm:flex-row sm:justify-between"
          >
            <div>
              <h2 class="font-semibold">Prompt files</h2>
              <p class="text-sm text-muted-foreground">
                Baseline prompts become encrypted editable Markdown after they
                are saved.
              </p>
            </div>
            <Show
              when={!loading()}
              fallback={<Skeleton class="h-5 w-20 rounded-full" />}
            >
              <Badge variant="secondary">
                {promptsResult.data?.encrypted && !promptsResult.isError
                  ? "encrypted"
                  : "baseline"}
              </Badge>
            </Show>
          </Flex>

          <Show
            when={!loading()}
            fallback={
              <Grid
                cols={1}
                colsLg={2}
                class="gap-4"
                aria-busy="true"
                aria-live="polite"
              >
                <For each={[1, 2]}>
                  {() => <Skeleton class="h-48 w-full rounded-lg" />}
                </For>
              </Grid>
            }
          >
            <Grid cols={1} colsLg={2} class="gap-4">
              <For each={promptFolders()}>
                {(folder) => (
                  <Card>
                    <CardHeader class="pb-3">
                      <CardTitle class="text-sm">{folder.path}</CardTitle>
                    </CardHeader>
                    <CardContent class="flex flex-col gap-2">
                      <For each={folder.files}>
                        {(file) => (
                          <Button
                            variant="outline"
                            class="h-auto w-full justify-start py-3 text-left whitespace-normal"
                            onClick={() => editPrompt(file)}
                          >
                            <span class="min-w-0 flex-1">
                              <span class="block wrap-break-word font-semibold">
                                {file.title}
                              </span>
                              <span class="block wrap-break-word font-mono text-xs text-muted-foreground">
                                {file.path}
                              </span>
                            </span>
                            <Badge variant="secondary" class="shrink-0">
                              {file.baseline ? "baseline" : "custom"}
                            </Badge>
                          </Button>
                        )}
                      </For>
                    </CardContent>
                  </Card>
                )}
              </For>
            </Grid>
          </Show>
          <Alert>
            <p>
              {promptsResult.isError
                ? "Built-in prompts are available, but encrypted overrides could not be loaded."
                : (promptsResult.data?.detail ??
                  "Built-in prompts are ready. Save one to create an encrypted override.")}
            </p>
          </Alert>
        </CardContent>
      </Card>

      <Dialog
        open={Boolean(selectedPrompt())}
        onOpenChange={(open) => !open && closePromptEditor()}
      >
        <DialogContent class="max-h-[calc(100dvh-1rem)] w-[calc(100vw-1rem)] max-w-3xl overflow-y-auto p-4 sm:max-h-[calc(100dvh-2rem)] sm:w-full sm:p-6">
          <DialogHeader>
            <DialogTitle>
              {selectedPrompt()?.title ?? "Edit prompt"}
            </DialogTitle>
            <DialogDescription>
              {selectedPrompt()?.path ?? "Prompt Markdown"}
            </DialogDescription>
          </DialogHeader>
          <Tabs
            value={promptTab()}
            onChange={(value) => setPromptTab(value as PromptTab)}
          >
            <TabsList aria-label="Prompt editor tabs">
              <TabsTrigger value="editor">Editor</TabsTrigger>
              <TabsTrigger value="preview">Preview</TabsTrigger>
            </TabsList>
            <TabsContent value="editor">
              <TextField>
                <TextFieldLabel class="sr-only">Prompt Markdown</TextFieldLabel>
                <TextFieldTextArea
                  class="min-h-64 font-mono text-xs leading-relaxed sm:min-h-100"
                  value={promptDraft()}
                  onInput={(event) => setPromptDraft(event.currentTarget.value)}
                />
              </TextField>
            </TabsContent>
            <TabsContent value="preview">
              <div
                class="prose prose-sm min-h-64 max-w-none overflow-x-auto rounded-md border p-4 text-foreground prose-headings:text-foreground prose-p:text-foreground prose-strong:text-foreground prose-code:text-foreground sm:min-h-100"
                innerHTML={renderPromptPreview(promptDraft())}
              />
            </TabsContent>
          </Tabs>
          <DialogFooter>
            <Button variant="outline" onClick={closePromptEditor}>
              Cancel
            </Button>
            <Button
              disabled={promptMutation.isPending || !selectedPrompt()}
              onClick={savePrompt}
            >
              <Show when={promptMutation.isPending}>
                <Spinner />
              </Show>
              Save encrypted
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </>
  );
}
