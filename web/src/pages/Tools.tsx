import {
  For,
  Show,
  createEffect,
  createMemo,
  createSignal,
  type Component,
} from "solid-js";
import { toast } from "solid-sonner";

import { createToastGate } from "~/lib/toast";

import {
  AlertDialog,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogTitle,
} from "~/components/alert-dialog";
import { Badge } from "~/components/badge";
import { Button } from "~/components/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "~/components/card";
import { Flex } from "~/components/flex";
import { Grid } from "~/components/grid";
import { Skeleton } from "~/components/skeleton";
import {
  Switch,
  SwitchControl,
  SwitchLabel,
  SwitchThumb,
} from "~/components/switch";

import { Tooltip, TooltipContent, TooltipTrigger } from "~/components/tooltip";
import type { McpToolDescriptor } from "../lib/api";

import { mcpToolPermissionMutation } from "../mutations/ai";
import { mcpToolsQuery as createMcpToolsQuery } from "../queries/ai";

export function requiresDestructiveToolConfirmation(
  tool: Pick<McpToolDescriptor, "destructive" | "enabled">,
  enabled: boolean,
) {
  return tool.destructive && enabled && !tool.enabled;
}

const Tools: Component = () => {
  const toolsQuery = createMcpToolsQuery();
  const permissionMutation = mcpToolPermissionMutation();
  const [toolAwaitingConfirmation, setToolAwaitingConfirmation] =
    createSignal<McpToolDescriptor | null>(null);
  const tools = createMemo(() => toolsQuery.data?.tools ?? []);

  const isInitialToolsLoad = () =>
    toolsQuery.isFetching && toolsQuery.data === undefined;
  const groupedTools = createMemo(() => {
    const groups: Record<string, McpToolDescriptor[]> = {};
    for (const tool of tools()) {
      const category = tool.category?.trim() || "Capabilities";
      (groups[category] ??= []).push(tool);
    }
    return Object.entries(groups).map(([category, items]) => ({
      category,
      items,
    }));
  });

  const shouldShowToast = createToastGate();
  createEffect(() => {
    const success = permissionMutation.isSuccess;
    const error = permissionMutation.isError;
    const successMessage =
      permissionMutation.data?.detail ?? "Tool permission saved.";
    const errorMessage =
      permissionMutation.error?.message ??
      "Tool permission could not be saved.";
    const key = success
      ? `success:${successMessage}`
      : error
        ? `error:${errorMessage}`
        : "";
    if (!shouldShowToast(key)) return;
    if (success) toast.success(successMessage);
    if (error) toast.error(errorMessage);
  });

  function updateTool(tool: McpToolDescriptor, enabled: boolean) {
    if (!tool.available || permissionMutation.isPending) return;
    if (requiresDestructiveToolConfirmation(tool, enabled)) {
      setToolAwaitingConfirmation(tool);
      return;
    }
    permissionMutation.mutate({ tool: tool.name, enabled });
  }

  function confirmDestructiveTool() {
    const tool = toolAwaitingConfirmation();
    if (!tool || permissionMutation.isPending) return;
    permissionMutation.mutate(
      {
        tool: tool.name,
        enabled: true,
        confirmation: `ENABLE DESTRUCTIVE TOOL ${tool.name}`,
      },
      { onSuccess: () => setToolAwaitingConfirmation(null) },
    );
  }

  return (
    <Card class="w-full overflow-hidden">
      <CardHeader class="gap-4 sm:flex-row sm:items-start sm:justify-between">
        <Flex
          flexDirection="col"
          alignItems="start"
          justifyContent="start"
          class="max-w-3xl gap-3"
        >
          <div class="h-px w-12 bg-foreground" />
          <div>
            <h1 class="text-xl font-semibold tracking-tight">
              Assistant Permissions
            </h1>
            <CardDescription>
              Choose which existing local capabilities the optional assistant
              may request
            </CardDescription>
          </div>
        </Flex>
        <Flex
          justifyContent="start"
          class="w-full flex-wrap gap-2 sm:w-auto sm:shrink-0"
        >
          <Button as="a" href="#/ai" variant="outline" size="sm">
            Assistant setup
          </Button>
          <Button as="a" href="#/ai/prompts" size="sm">
            Prompt files
          </Button>
        </Flex>
      </CardHeader>

      <CardContent>
        <Flex
          flexDirection="col"
          alignItems="stretch"
          justifyContent="start"
          class="gap-5"
        >
          <Card>
            <CardHeader class="gap-3 md:flex-row md:items-start md:justify-between">
              <Flex
                justifyContent="start"
                alignItems="start"
                class="min-w-0 flex-1 gap-3"
              >
                <div
                  class="grid size-10 shrink-0 place-items-center rounded-2xl border text-xs font-semibold"
                  aria-hidden="true"
                >
                  AI
                </div>
                <div class="min-w-0">
                  <CardTitle>What the assistant can access</CardTitle>
                  <CardDescription class="max-w-3xl">
                    These permissions limit optional assistant requests to
                    executable local operations. Disabled permissions are not
                    advertised through the MCP tools list.
                  </CardDescription>
                </div>
              </Flex>
              <Badge
                variant="secondary"
                class="w-fit shrink-0 whitespace-nowrap"
              >
                Local permissions
              </Badge>
            </CardHeader>

            <CardContent>
              <Show
                when={!isInitialToolsLoad()}
                fallback={
                  <Grid
                    colsLg={2}
                    class="gap-3"
                    aria-busy="true"
                    aria-live="polite"
                  >
                    <For each={Array.from({ length: 4 })}>
                      {() => (
                        <Card>
                          <CardHeader class="gap-3">
                            <Flex justifyContent="between" class="gap-3">
                              <Flex
                                flexDirection="col"
                                alignItems="start"
                                justifyContent="start"
                                class="gap-2"
                              >
                                <Skeleton class="h-4 w-32" />
                                <Skeleton class="h-6 w-40 max-w-full" />
                              </Flex>
                              <Skeleton class="size-5 shrink-0 rounded-sm" />
                            </Flex>
                          </CardHeader>
                          <CardContent class="space-y-2">
                            <Skeleton class="h-4 w-full" />
                            <Skeleton class="h-4 w-4/5" />
                          </CardContent>
                        </Card>
                      )}
                    </For>
                  </Grid>
                }
              >
                <Show
                  when={tools().length > 0}
                  fallback={
                    <Flex
                      flexDirection="col"
                      justifyContent="center"
                      class="min-h-40 gap-2 rounded-lg border p-6 text-center sm:p-8"
                    >
                      <h2 class="font-semibold">No capability information</h2>
                      <p class="text-sm text-muted-foreground">
                        Assistant capability information is not available yet.
                      </p>
                    </Flex>
                  }
                >
                  <Flex
                    flexDirection="col"
                    alignItems="stretch"
                    justifyContent="start"
                    class="gap-6"
                  >
                    <For each={groupedTools()}>
                      {(group) => (
                        <fieldset class="flex flex-col gap-3">
                          <legend class="text-sm font-semibold">
                            {group.category}
                          </legend>
                          <Grid colsLg={2} class="gap-3">
                            <For each={group.items}>
                              {(tool) => (
                                <Card>
                                  <CardHeader class="gap-3 sm:flex-row sm:items-start sm:justify-between">
                                    <Flex
                                      flexDirection="col"
                                      alignItems="start"
                                      justifyContent="start"
                                      class="min-w-0 gap-1"
                                    >
                                      <Flex
                                        justifyContent="start"
                                        class="flex-wrap gap-2"
                                      >
                                        <CardTitle class="wrap-break-word text-sm">
                                          {tool.title}
                                        </CardTitle>
                                        <Show when={tool.destructive}>
                                          <Badge variant="error">
                                            destructive
                                          </Badge>
                                        </Show>
                                        <Badge
                                          variant={
                                            tool.available
                                              ? "success"
                                              : "secondary"
                                          }
                                        >
                                          {tool.available
                                            ? "available"
                                            : "not implemented"}
                                        </Badge>
                                        <Show when={tool.enabled}>
                                          <Badge variant="outline">
                                            allowed
                                          </Badge>
                                        </Show>
                                      </Flex>
                                      <code class="inline-flex max-w-full rounded-md border bg-muted px-2 py-1 text-xs font-semibold break-all whitespace-normal text-muted-foreground">
                                        {tool.id}
                                      </code>
                                    </Flex>
                                    <Tooltip>
                                      <TooltipTrigger as="div">
                                        <Switch
                                          checked={tool.enabled}
                                          disabled={
                                            !tool.available ||
                                            permissionMutation.isPending
                                          }
                                          onChange={(checked) =>
                                            updateTool(tool, checked)
                                          }
                                          class="flex items-center gap-2"
                                        >
                                          <span class="pointer-events-none text-sm font-medium">
                                            {tool.enabled ? "Allowed" : "Off"}
                                          </span>
                                          <SwitchControl
                                            data-tool-switch-control={tool.id}
                                          >
                                            <SwitchThumb />
                                          </SwitchControl>
                                          <SwitchLabel class="sr-only">{`Toggle ${tool.id}`}</SwitchLabel>
                                        </Switch>
                                      </TooltipTrigger>
                                      <TooltipContent>
                                        {tool.available
                                          ? `Allow or deny ${tool.title} for the assistant`
                                          : `${tool.title} is registered but not implemented in firmware`}
                                      </TooltipContent>
                                    </Tooltip>
                                  </CardHeader>
                                  <CardContent class="flex flex-col gap-3">
                                    <CardDescription>
                                      {tool.description}
                                    </CardDescription>
                                  </CardContent>
                                </Card>
                              )}
                            </For>
                          </Grid>
                        </fieldset>
                      )}
                    </For>
                  </Flex>
                </Show>
              </Show>
            </CardContent>
          </Card>
        </Flex>
      </CardContent>

      <AlertDialog
        open={Boolean(toolAwaitingConfirmation())}
        onOpenChange={(open) => !open && setToolAwaitingConfirmation(null)}
      >
        <AlertDialogContent>
          <div class="space-y-2 pr-6">
            <AlertDialogTitle>Enable destructive tool?</AlertDialogTitle>
            <AlertDialogDescription>
              {toolAwaitingConfirmation()?.title ?? "This tool"} can make
              destructive changes when the assistant is allowed to request it.
              The firmware will continue to validate and authorize every
              request.
            </AlertDialogDescription>
          </div>
          <Flex
            flexDirection="col-reverse"
            justifyContent="end"
            class="gap-2 sm:flex-row"
          >
            <Button
              variant="outline"
              class="w-full sm:w-auto"
              disabled={permissionMutation.isPending}
              onClick={() => setToolAwaitingConfirmation(null)}
            >
              Cancel
            </Button>
            <Button
              variant="destructive"
              class="w-full sm:w-auto"
              disabled={permissionMutation.isPending}
              aria-busy={permissionMutation.isPending}
              onClick={confirmDestructiveTool}
            >
              {permissionMutation.isPending ? "Enabling…" : "Enable tool"}
            </Button>
          </Flex>
        </AlertDialogContent>
      </AlertDialog>
    </Card>
  );
};

export default Tools;
