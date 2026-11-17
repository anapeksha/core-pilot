import {
  For,
  Show,
  createEffect,
  createMemo,
  createSignal,
  type Component,
} from "solid-js";
import { createForm } from "solid-hook-form";
import { toast } from "solid-sonner";

import { createUnsavedChanges } from "~/lib/unsaved-changes";
import { createToastGate } from "~/lib/toast";
import { z } from "zod";

import { Alert, AlertDescription, AlertTitle } from "~/components/alert";
import { Badge } from "~/components/badge";
import { Button } from "~/components/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from "~/components/card";

import { Flex } from "~/components/flex";
import { Grid } from "~/components/grid";
import { OperationStatus } from "~/components/operation-status";

import {
  Message,
  MessageAvatar,
  MessageBubble,
  MessageContent,
  MessageFooter,
  MessageHeader,
} from "~/components/message";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "~/components/select";
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetFooter,
  SheetHeader,
  SheetTitle,
} from "~/components/sheet";

import {
  TextField,
  TextFieldDescription,
  TextFieldErrorMessage,
  TextFieldInput,
  TextFieldLabel,
  TextFieldTextArea,
} from "~/components/text-field";
import { bytesAtMost, entityLimits } from "~/lib/request-contracts";
import {
  fallbackAi,
  type AiApprovalProposal,
  type AiChatActionState,
  type AiChatHistoryMessage,
  type AiChatMessage,
  type AiChatResponse,
} from "../lib/api";

import { aiChatMutation, aiProviderMutation } from "../mutations/ai";
import {
  aiQuery as createAiQuery,
  mcpToolsQuery as createMcpToolsQuery,
} from "../queries/ai";

type ProviderMode = "openai-compatible" | "anthropic-compatible";
type ProviderBackend = "open_ai_compatible" | "anthropic_compatible";
type ProviderAuthType = "bearer" | "api_key" | "none";

type SelectOption<T extends string> = { value: T; label: string };

const providerBackendOptions: SelectOption<ProviderBackend>[] = [
  { value: "open_ai_compatible", label: "OpenAI compatible" },
  { value: "anthropic_compatible", label: "Anthropic compatible" },
];
const providerAuthOptions: SelectOption<ProviderAuthType>[] = [
  { value: "bearer", label: "Authorization: Bearer" },
  { value: "api_key", label: "X-API-Key" },
  { value: "none", label: "None" },
];

const chatSchema = z.object({
  message: bytesAtMost(
    entityLimits.chatMessage,
    "Message must be 4 KiB or less.",
  ).refine((value) => value.trim().length > 0, "Enter a message."),
});

export function isOfficialProviderBaseUrl(value: string): boolean {
  const normalized = value.trim().replace(/\/$/, "");
  return (
    normalized === "https://api.openai.com/v1" ||
    normalized === "https://api.anthropic.com/v1"
  );
}

const providerSchema = z.object({
  backend: z.enum(["open_ai_compatible", "anthropic_compatible"]),
  authType: z.enum(["bearer", "api_key", "none"]),
  baseUrl: bytesAtMost(entityLimits.providerUrl, "Base URL is too long.")
    .refine((value) => value.trim().length > 0, "Enter a base URL.")
    .refine(
      isOfficialProviderBaseUrl,
      "Use the official OpenAI or Anthropic HTTPS API base URL.",
    ),
  model: bytesAtMost(
    entityLimits.providerModel,
    "Model identifier is too long.",
  ).refine((value) => value.trim().length > 0, "Enter a model identifier."),
  maxOutputTokens: z.coerce
    .number<string>()
    .int("Enter a whole number of tokens.")
    .min(1, "Use at least 1 token.")
    .max(8192, "Use no more than 8192 tokens."),
  timeoutMs: z.coerce
    .number<string>()
    .int("Enter a whole number of milliseconds.")
    .min(1000, "Timeout must be at least 1000 ms.")
    .max(120000, "Timeout must be no more than 120000 ms."),
  apiKey: bytesAtMost(
    entityLimits.apiKey,
    "API key must be 256 bytes or less.",
  ),
});

type ChatFormValues = z.input<typeof chatSchema>;
type ProviderFormValues = z.input<typeof providerSchema>;

function zodResolver<T>(schema: z.ZodType<T>) {
  return async (values: T) => {
    const result = schema.safeParse(values);
    if (result.success) return { values: result.data, errors: {} };
    return {
      values: {},
      errors: Object.fromEntries(
        result.error.issues.map((issue) => [
          issue.path.join("."),
          { type: issue.code, message: issue.message },
        ]),
      ),
    };
  };
}

function newChatId(): string {
  if (globalThis.crypto?.randomUUID) return globalThis.crypto.randomUUID();
  return `chat-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
}

function messageLabel(role: AiChatMessage["role"]): string {
  if (role === "user") return "You";
  if (role === "tool") return "Tool";
  return "Core Pilot";
}

export type DisplayChatMessage = AiChatMessage & {
  operationId?: string;
  proposal?: AiApprovalProposal;
  actionState?: AiChatActionState;
};

export function userAuthoredHistory(
  messages: readonly AiChatMessage[],
): AiChatHistoryMessage[] {
  return messages
    .filter(
      (message): message is AiChatHistoryMessage => message.role === "user",
    )
    .slice(-entityLimits.chatHistory);
}

export function proposalExpiry(
  proposal: AiApprovalProposal,
): string | undefined {
  if (proposal.expires_at) {
    const expiresAt = new Date(proposal.expires_at);
    if (!Number.isNaN(expiresAt.getTime()))
      return `Expires ${expiresAt.toLocaleString()}`;
  }
  if (proposal.state === "proposed" || proposal.state === "approval_required") {
    if (proposal.expires_in_seconds !== undefined)
      return `Expires in ${proposal.expires_in_seconds} seconds`;
  }
  return undefined;
}

export function reconcileApprovalResponse(
  messages: readonly DisplayChatMessage[],
  response: AiChatResponse,
): DisplayChatMessage[] {
  const existingIndex = messages.findIndex(
    (message) => message.operationId === response.operation_id,
  );
  const existing = existingIndex >= 0 ? messages[existingIndex] : undefined;
  const proposal = response.proposal ?? existing?.proposal;
  if (!proposal) return [...messages];

  const updated: DisplayChatMessage = {
    role: "assistant",
    content:
      response.reply ||
      existing?.content ||
      "Review this proposal on CoreS3 before it expires.",
    operationId: response.operation_id,
    proposal: { ...proposal, state: response.status },
  };
  if (existingIndex < 0)
    return [...messages, updated].slice(-MAX_RENDERED_CHAT_MESSAGES);
  return messages.map((message, index) =>
    index === existingIndex ? updated : message,
  );
}

function proposalStateLabel(state: AiApprovalProposal["state"]): string {
  return state.replaceAll("_", " ");
}

export function actionStateLabel(state?: AiChatActionState): string {
  if (state === "proposed") return "Proposed locally; not executed";
  if (state === "executing") return "Executing locally";
  if (state === "queued") return "Queued locally";
  if (state === "complete") return "Completed locally";
  if (state === "rejected") return "Rejected locally; not executed";
  if (state === "expired") return "Approval expired; not executed";
  if (state === "failed") return "Local execution failed";
  return "Local action state unavailable";
}

function proposalGuidance(state: AiApprovalProposal["state"]): string {
  if (state === "proposed" || state === "approval_required")
    return "Approve on CoreS3";
  if (state === "executing") return "Executing on CoreS3";
  if (state === "complete") return "Completed on CoreS3";
  if (state === "rejected") return "Rejected on CoreS3";
  if (state === "expired") return "Approval expired";
  return "Operation failed";
}

const ApprovalProposal: Component<{ proposal: AiApprovalProposal }> = (
  props,
) => (
  <div class="mt-3 space-y-3 rounded-lg border border-warning-foreground/40 bg-warning/20 p-3">
    <Flex class="flex-wrap gap-2" justifyContent="start">
      <Badge
        variant={props.proposal.state === "complete" ? "success" : "warning"}
      >
        {proposalGuidance(props.proposal.state)}
      </Badge>
      <Badge variant="outline">Effect: {props.proposal.effect}</Badge>
      <Badge variant="outline">
        State: {proposalStateLabel(props.proposal.state)}
      </Badge>
    </Flex>
    <div>
      <p class="text-xs font-semibold uppercase tracking-wide text-foreground/60">
        Local summary
      </p>
      <p class="mt-1 text-sm whitespace-pre-wrap">
        {props.proposal.local_summary}
      </p>
    </div>
    <Show when={proposalExpiry(props.proposal)}>
      {(expiry) => <p class="text-xs text-foreground/70">{expiry()}</p>}
    </Show>
    <Show when={props.proposal.actions.length > 0}>
      <ul class="space-y-1 text-sm">
        <For each={props.proposal.actions}>
          {(action) => (
            <li>
              <span class="font-medium">{action.tool}</span>: {action.detail}
            </li>
          )}
        </For>
      </ul>
    </Show>
    <div class="rounded-md bg-background/70 px-3 py-2 text-sm font-semibold">
      {proposalGuidance(props.proposal.state)}
    </div>
    <Show
      when={
        props.proposal.state === "proposed" ||
        props.proposal.state === "approval_required"
      }
    >
      <p class="text-xs text-foreground/60">
        Review and approve this proposal on the physical CoreS3. Browser
        approval is not available.
      </p>
    </Show>
  </div>
);

const ChatMessage: Component<{
  message: DisplayChatMessage;
  pending?: boolean;
}> = (props) => {
  const isUserMessage = () => props.message.role === "user";
  return (
    <Message align={isUserMessage() ? "end" : "start"}>
      <MessageAvatar aria-hidden="true">
        {isUserMessage()
          ? "You"
          : props.message.role === "tool"
            ? "Tool"
            : "CP"}
      </MessageAvatar>
      <MessageContent>
        <MessageHeader>{messageLabel(props.message.role)}</MessageHeader>
        <MessageBubble tone={isUserMessage() ? "default" : "muted"}>
          <p class={props.pending ? "shimmer" : "whitespace-pre-wrap"}>
            {props.message.content}
          </p>
        </MessageBubble>
        <Show when={props.message.proposal}>
          {(proposal) => <ApprovalProposal proposal={proposal()} />}
        </Show>
        <Show when={props.message.role === "tool"}>
          <MessageFooter>
            {actionStateLabel(props.message.actionState)}
          </MessageFooter>
        </Show>
      </MessageContent>
    </Message>
  );
};

const MAX_RENDERED_CHAT_MESSAGES = 50;

function appendChatMessage(
  current: DisplayChatMessage[],
  message: DisplayChatMessage,
): DisplayChatMessage[] {
  return [...current, message].slice(-MAX_RENDERED_CHAT_MESSAGES);
}

const AiAssistant: Component = () => {
  const aiQuery = createAiQuery();
  const toolsQuery = createMcpToolsQuery();

  const providerMutation = aiProviderMutation();
  const chatMutation = aiChatMutation();

  const providerDefaults: ProviderFormValues = {
    backend: "open_ai_compatible",
    authType: "bearer",
    baseUrl: "https://api.openai.com/v1",
    model: "gpt-4o-mini",
    maxOutputTokens: "1024",
    timeoutMs: "30000",
    apiKey: "",
  };
  const [providerSheetOpen, setProviderSheetOpen] = createSignal(false);
  const {
    errors: providerErrors,
    handleSubmit: handleProviderSubmit,
    register: registerProvider,
    reset: resetProvider,
    values: providerValues,
    setValue: setProviderValue,
    formState: providerFormState,
  } = createForm<ProviderFormValues>({
    defaultValues: providerDefaults,
    mode: "onSubmit",
    resolver: zodResolver(providerSchema),
  });
  const {
    errors: chatErrors,
    handleSubmit: handleChatSubmit,
    register: registerChat,
    reset: resetChat,
    values: chatValues,
    formState: chatFormState,
  } = createForm<ChatFormValues>({
    defaultValues: { message: "" },
    mode: "onSubmit",
    resolver: zodResolver(chatSchema),
  });
  const [messages, setMessages] = createSignal<DisplayChatMessage[]>([
    {
      role: "assistant",
      content:
        "Hi — configure an encrypted HTTPS provider, then I can answer questions and use the local tools you have explicitly enabled.",
    },
  ]);
  const chatId = newChatId();
  const ai = createMemo(() => aiQuery.data ?? fallbackAi);

  const shouldShowToast = createToastGate();
  createEffect(() => {
    const success = providerMutation.isSuccess;
    const error = providerMutation.isError;
    const successMessage =
      providerMutation.data?.detail ?? "AI settings updated.";
    const errorMessage =
      providerMutation.error?.message ?? "AI settings could not be updated.";
    const key = success
      ? `success:${successMessage}`
      : error
        ? `error:${errorMessage}`
        : "";
    if (!shouldShowToast(key)) return;
    if (success) toast.success(successMessage);
    if (error) toast.error(errorMessage);
  });

  // Status metadata is optional on older firmware; credentials are never read.
  const savedProvider = () =>
    (
      ai() as typeof fallbackAi & {
        provider?: {
          backend: ProviderBackend;
          auth_type: ProviderAuthType;
          base_url: string;
          model: string;
          max_output_tokens: number;
          timeout_ms: number;
        };
      }
    ).provider;
  const providerMode = () =>
    providerValues().backend === "open_ai_compatible"
      ? "openai-compatible"
      : "anthropic-compatible";
  const providerBackend = () =>
    providerBackendOptions.find(
      (option) => option.value === providerValues().backend,
    );
  const providerAuthType = () =>
    providerAuthOptions.find(
      (option) => option.value === providerValues().authType,
    );
  const unsaved = createUnsavedChanges({
    open: () => true,
    dirty: () =>
      (providerSheetOpen() && providerFormState.isDirty()) ||
      chatFormState.isDirty(),
    label: "provider settings or unsent chat draft",
  });

  function openProviderSheet() {
    if (providerMutation.isPending) return;
    const saved = savedProvider();
    Object.assign(providerDefaults, {
      backend: saved?.backend ?? "open_ai_compatible",
      authType: saved?.auth_type ?? "bearer",
      baseUrl: saved?.base_url ?? "https://api.openai.com/v1",
      model: saved?.model ?? "gpt-4o-mini",
      maxOutputTokens: String(saved?.max_output_tokens ?? 1024),
      timeoutMs: String(saved?.timeout_ms ?? 30000),
      apiKey: "",
    });
    resetProvider({ ...providerDefaults });
    providerMutation.reset();
    setProviderSheetOpen(true);
  }

  function dismissProviderSheet() {
    setProviderSheetOpen(false);
    resetProvider({ ...providerDefaults, apiKey: "" });
  }

  function closeProviderSheet() {
    if (providerMutation.isPending) return;
    if (providerFormState.isDirty()) unsaved.request(dismissProviderSheet);
    else dismissProviderSheet();
  }

  function chooseProvider(mode: ProviderMode) {
    if (providerMutation.isPending || mode === providerMode()) return;
    const openAi = mode === "openai-compatible";
    setProviderValue("apiKey", "", { shouldDirty: true });
    setProviderValue(
      "backend",
      openAi ? "open_ai_compatible" : "anthropic_compatible",
      { shouldDirty: true },
    );
    setProviderValue("authType", openAi ? "bearer" : "api_key", {
      shouldDirty: true,
    });
    setProviderValue(
      "baseUrl",
      openAi ? "https://api.openai.com/v1" : "https://api.anthropic.com/v1",
      { shouldDirty: true },
    );
    setProviderValue("model", openAi ? "gpt-4o-mini" : "claude-sonnet-4-5", {
      shouldDirty: true,
    });
  }

  const credentialScope = (values: ProviderFormValues) => {
    try {
      const url = new URL(values.baseUrl);
      return JSON.stringify([
        url.protocol,
        url.hostname,
        url.port || (url.protocol === "https:" ? "443" : "80"),
        values.backend,
        values.authType,
      ]);
    } catch {
      return JSON.stringify([values.baseUrl, values.backend, values.authType]);
    }
  };
  let previousScope = credentialScope(providerValues());
  createEffect(() => {
    const scope = credentialScope(providerValues());
    if (scope !== previousScope) {
      previousScope = scope;
      setProviderValue("apiKey", "", { shouldDirty: true });
    }
  });

  const submitProvider = handleProviderSubmit((formValues) => {
    if (providerMutation.isPending) return;
    const apiKey = formValues.apiKey.trim();
    const openAi = providerMode() === "openai-compatible";
    providerMutation.mutate(
      {
        provider: {
          backend: openAi ? "open_ai_compatible" : "anthropic_compatible",
          auth_type: openAi ? "bearer" : "api_key",
          base_url: formValues.baseUrl.trim(),
          model: formValues.model.trim(),
          max_output_tokens: Number(formValues.maxOutputTokens),
          timeout_ms: Number(formValues.timeoutMs),
          api_key: apiKey || null,
          api_key_redacted:
            !apiKey &&
            !!savedProvider() &&
            credentialScope(formValues) === credentialScope(providerDefaults),
        },
      },
      {
        onSuccess: (response) => {
          if (!response.accepted) return;
          Object.assign(providerDefaults, formValues, { apiKey: "" });
          dismissProviderSheet();
        },
      },
    );
  });

  const sendChat = handleChatSubmit((formValues) => {
    const message = formValues.message.trim();
    if (chatMutation.isPending) return;
    const history = userAuthoredHistory(messages());
    setMessages((current) =>
      appendChatMessage(current, { role: "user", content: message }),
    );
    resetChat({ message: "" });
    chatMutation.mutate(
      {
        request: {
          message,
          ...(history.length > 0 ? { history } : {}),
          chat_id: chatId,
        },
        onStatus: (response) =>
          setMessages((current) =>
            reconcileApprovalResponse(current, response),
          ),
      },
      {
        onSuccess: (response) => {
          setMessages((current) => {
            const trackedApproval = current.some(
              (entry) => entry.operationId === response.operation_id,
            );
            const withAnswer = trackedApproval
              ? reconcileApprovalResponse(current, response)
              : appendChatMessage(current, {
                  role: "assistant",
                  content: response.reply,
                });
            if (trackedApproval && response.status !== "complete")
              return withAnswer;
            return response.actions.reduce(
              (next, action) =>
                appendChatMessage(next, {
                  role: "tool",
                  content: `${action.tool}: ${action.detail}`,
                  actionState: action.state,
                }),
              withAnswer,
            );
          });
        },
        onError: (error) =>
          setMessages((current) =>
            appendChatMessage(current, {
              role: "assistant",
              content: error.message || "The assistant request failed.",
            }),
          ),
      },
    );
  });

  const enabledTools = createMemo(() =>
    (toolsQuery.data?.tools ?? []).filter(
      (tool) => tool.available && tool.enabled,
    ),
  );

  return (
    <>
      <unsaved.Confirmation />
      <Card class="w-full overflow-hidden">
        <CardHeader class="gap-4 sm:flex-row sm:items-start sm:justify-between">
          <Flex
            flexDirection="col"
            alignItems="start"
            justifyContent="start"
            class="max-w-3xl gap-2"
          >
            <Badge variant="secondary" class="w-fit">
              Optional assistant
            </Badge>
            <h1 class="text-xl font-semibold tracking-tight">
              Optional Assistant
            </h1>
            <CardDescription>
              Optional chat and setup help. Devices, scenes, schedules,
              automations, and APIs continue working locally when it is off.
            </CardDescription>
          </Flex>
          <Flex class="w-full flex-wrap gap-2 sm:w-auto">
            <Button as="a" href="#/ai/tools" variant="outline" size="sm">
              Permissions
            </Button>
            <Button as="a" href="#/ai/prompts" variant="outline" size="sm">
              Prompt files
            </Button>
            <Button
              as="a"
              href="#/ai/prompts?section=memory"
              variant="outline"
              size="sm"
            >
              Saved context
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
            <Flex
              alignItems="start"
              class="flex-col gap-5 xl:flex-row xl:items-stretch"
            >
              <Card class="flex min-w-0 flex-col xl:min-h-155 xl:flex-[1_1_46rem]">
                <CardHeader>
                  <Flex justifyContent="start" class="gap-3">
                    <div
                      class="grid size-10 place-items-center rounded-2xl bg-muted"
                      aria-hidden="true"
                    >
                      AI
                    </div>
                    <Flex
                      flexDirection="col"
                      alignItems="start"
                      justifyContent="start"
                      class="gap-1"
                    >
                      <CardTitle>Chat</CardTitle>
                      <CardDescription>
                        Answers can use only the local tools enabled in
                        Permissions.
                      </CardDescription>
                    </Flex>
                  </Flex>
                </CardHeader>
                <CardContent
                  role="log"
                  aria-label="Assistant conversation"
                  aria-live="polite"
                  aria-relevant="additions text"
                  tabindex="0"
                  class="scroll-fade flex max-h-96 min-h-48 flex-col gap-4 overflow-y-auto focus-visible:outline-2 focus-visible:outline-ring sm:max-h-112 xl:max-h-none xl:min-h-105 xl:flex-1"
                >
                  <For each={messages()}>
                    {(message) => <ChatMessage message={message} />}
                  </For>
                  <Show when={chatMutation.isError}>
                    <Alert>
                      <AlertTitle>Provider or request failure</AlertTitle>
                      <AlertDescription>
                        {chatMutation.error?.message ??
                          "The provider request failed before a local tool ran."}
                      </AlertDescription>
                    </Alert>
                  </Show>
                  <Show
                    when={
                      chatMutation.isPending &&
                      !messages().some(
                        (message) =>
                          message.proposal?.state === "approval_required" ||
                          message.proposal?.state === "executing",
                      )
                    }
                  >
                    <ChatMessage
                      pending
                      message={{
                        role: "assistant",
                        content: "Core Pilot is thinking…",
                      }}
                    />
                  </Show>
                </CardContent>
                <CardFooter>
                  <form class="w-full" onSubmit={sendChat}>
                    <TextField
                      validationState={chatErrors.message ? "invalid" : "valid"}
                    >
                      <TextFieldLabel for="assistant-message" class="sr-only">
                        Message Core Pilot
                      </TextFieldLabel>
                      <TextFieldTextArea
                        {...registerChat("message")}
                        id="assistant-message"
                        value={chatValues().message}
                        maxlength={entityLimits.chatMessage}
                        class="min-h-20 resize-none"
                        placeholder="Ask Core Pilot to check status, list devices, scenes, or automations…"
                        onKeyDown={(
                          event: KeyboardEvent & {
                            currentTarget: HTMLTextAreaElement;
                          },
                        ) => {
                          if (
                            (event.metaKey || event.ctrlKey) &&
                            event.key === "Enter"
                          ) {
                            event.preventDefault();
                            event.currentTarget.form?.requestSubmit();
                          }
                        }}
                      />
                      <Show when={chatErrors.message?.message}>
                        {(message) => (
                          <TextFieldErrorMessage>
                            {message()}
                          </TextFieldErrorMessage>
                        )}
                      </Show>
                    </TextField>
                    <Flex
                      class="mt-3 flex-col gap-3 sm:flex-row"
                      alignItems="stretch"
                    >
                      <Flex justifyContent="end" class="w-auto gap-3">
                        <span class="hidden text-xs text-foreground/50 sm:inline">
                          Ctrl+Enter to send
                        </span>
                        <Button
                          size="sm"
                          disabled={
                            !chatValues().message.trim() ||
                            chatMutation.isPending
                          }
                          type="submit"
                        >
                          Send
                        </Button>
                      </Flex>
                    </Flex>
                  </form>
                </CardFooter>
              </Card>

              <div class="min-w-0 space-y-5 xl:flex-[1_1_24rem]">
                <Card data-assistant-provider-card>
                  <CardHeader>
                    <Flex alignItems="start" class="flex-col gap-3 sm:flex-row">
                      <Flex
                        flexDirection="col"
                        alignItems="start"
                        justifyContent="start"
                        class="min-w-0 gap-1"
                      >
                        <CardTitle>Assistant provider</CardTitle>
                        <CardDescription>
                          Choose an optional model provider. Local home controls
                          do not depend on this configuration.
                        </CardDescription>
                      </Flex>
                      <Badge variant="outline" class="shrink-0">
                        Encrypted
                      </Badge>
                    </Flex>
                  </CardHeader>
                  <CardContent>
                    <p class="text-sm text-muted-foreground">
                      Provider credentials are configured in a focused setup
                      panel and are never returned by the status API.
                    </p>
                  </CardContent>
                  <CardFooter class="justify-end">
                    <Button
                      type="button"
                      onClick={openProviderSheet}
                      disabled={providerMutation.isPending}
                    >
                      {ai().provider_configured
                        ? savedProvider()
                          ? "Update provider"
                          : "Replace provider"
                        : "Add provider"}
                    </Button>
                  </CardFooter>
                </Card>

                <Card>
                  <CardHeader>
                    <CardTitle>Assistant access</CardTitle>
                    <CardDescription>
                      Core Pilot keeps tool authorization local and checks it
                      again before every action.
                    </CardDescription>
                  </CardHeader>
                  <CardContent class="space-y-2">
                    <p class="text-2xl font-semibold">
                      {enabledTools().length}
                    </p>
                    <p class="text-sm text-muted-foreground">
                      {enabledTools().length === 1
                        ? "local tool is enabled for chat"
                        : "local tools are enabled for chat"}
                    </p>
                  </CardContent>
                  <CardFooter class="justify-end">
                    <Button as="a" href="#/ai/tools" variant="outline">
                      Manage permissions
                    </Button>
                  </CardFooter>
                </Card>

                <Sheet
                  open={providerSheetOpen()}
                  onOpenChange={(open) =>
                    open ? openProviderSheet() : closeProviderSheet()
                  }
                >
                  <SheetContent
                    position="right"
                    class="flex w-full max-w-none flex-col gap-0 overflow-hidden! p-0 sm:max-w-lg"
                  >
                    <SheetHeader class="shrink-0 border-b px-6 py-5 pr-12">
                      <SheetTitle>Assistant provider</SheetTitle>
                      <SheetDescription>
                        Configure an optional encrypted model provider. Local
                        home controls do not depend on this configuration.
                      </SheetDescription>
                    </SheetHeader>
                    <form
                      class="flex min-h-0 flex-1 flex-col"
                      onSubmit={submitProvider}
                    >
                      <fieldset
                        disabled={providerMutation.isPending}
                        class="min-h-0 flex-1 space-y-5 overflow-y-auto px-6 py-5"
                      >
                        <Show
                          when={ai().provider_configured && !savedProvider()}
                        >
                          <p role="status">
                            Current provider details are unavailable. Saving
                            replaces the provider configuration; enter a key for
                            the replacement.
                          </p>
                        </Show>
                        <div
                          class="grid grid-cols-2 gap-2"
                          role="group"
                          aria-label="Provider protocol"
                        >
                          <Button
                            type="button"
                            variant={
                              providerMode() === "openai-compatible"
                                ? "default"
                                : "outline"
                            }
                            onClick={() => chooseProvider("openai-compatible")}
                          >
                            OpenAI
                          </Button>
                          <Button
                            type="button"
                            variant={
                              providerMode() === "anthropic-compatible"
                                ? "default"
                                : "outline"
                            }
                            onClick={() =>
                              chooseProvider("anthropic-compatible")
                            }
                          >
                            Anthropic
                          </Button>
                        </div>
                        <TextField
                          validationState={
                            providerErrors.baseUrl ? "invalid" : "valid"
                          }
                        >
                          <TextFieldLabel for="provider-base-url">
                            Base URL
                          </TextFieldLabel>
                          <TextFieldInput
                            {...registerProvider("baseUrl")}
                            id="provider-base-url"
                            value={providerValues().baseUrl}
                            maxlength={entityLimits.providerUrl}
                            placeholder="https://api.openai.com/v1"
                            readOnly
                            required
                          />
                          <TextFieldDescription>
                            Firmware accepts only the official OpenAI or
                            Anthropic HTTPS API root on port 443.
                          </TextFieldDescription>
                          <Show when={providerErrors.baseUrl?.message}>
                            {(message) => (
                              <TextFieldErrorMessage>
                                {message()}
                              </TextFieldErrorMessage>
                            )}
                          </Show>
                        </TextField>
                        <TextField
                          validationState={
                            providerErrors.model ? "invalid" : "valid"
                          }
                        >
                          <TextFieldLabel for="provider-model">
                            Model
                          </TextFieldLabel>
                          <TextFieldInput
                            {...registerProvider("model")}
                            id="provider-model"
                            value={providerValues().model}
                            maxlength={entityLimits.providerModel}
                            placeholder="Model identifier"
                            required
                          />
                          <Show when={providerErrors.model?.message}>
                            {(message) => (
                              <TextFieldErrorMessage>
                                {message()}
                              </TextFieldErrorMessage>
                            )}
                          </Show>
                        </TextField>
                        <Grid colsSm={2} class="gap-5">
                          <Select<SelectOption<ProviderBackend>>
                            options={providerBackendOptions}
                            disabled
                            optionValue="value"
                            optionTextValue="label"
                            value={providerBackend()}
                            onChange={(value) =>
                              value &&
                              setProviderValue("backend", value.value, {
                                shouldDirty: true,
                              })
                            }
                            itemComponent={(props) => (
                              <SelectItem item={props.item}>
                                {props.item.rawValue.label}
                              </SelectItem>
                            )}
                          >
                            <SelectLabel>Backend</SelectLabel>
                            <SelectTrigger>
                              <SelectValue<SelectOption<ProviderBackend>>>
                                {(state) => state.selectedOption().label}
                              </SelectValue>
                            </SelectTrigger>
                            <SelectContent />
                          </Select>
                          <Select<SelectOption<ProviderAuthType>>
                            options={providerAuthOptions}
                            disabled
                            optionValue="value"
                            optionTextValue="label"
                            value={providerAuthType()}
                            onChange={(value) =>
                              value &&
                              setProviderValue("authType", value.value, {
                                shouldDirty: true,
                              })
                            }
                            itemComponent={(props) => (
                              <SelectItem item={props.item}>
                                {props.item.rawValue.label}
                              </SelectItem>
                            )}
                          >
                            <SelectLabel>Auth header</SelectLabel>
                            <SelectTrigger>
                              <SelectValue<SelectOption<ProviderAuthType>>>
                                {(state) => state.selectedOption().label}
                              </SelectValue>
                            </SelectTrigger>
                            <SelectContent />
                          </Select>
                        </Grid>
                        <p class="rounded-md border border-warning/40 bg-warning/10 p-3 text-sm text-muted-foreground">
                          OpenAI uses Authorization: Bearer; Anthropic uses
                          X-API-Key. When you chat, prompts, recent chat
                          history, and enabled tool results are sent to the
                          selected provider and are subject to that provider’s
                          privacy and retention policies. API keys remain
                          encrypted on the device and are not returned by the
                          status API.
                        </p>
                        <Grid colsSm={2} class="gap-5">
                          <TextField
                            validationState={
                              providerErrors.maxOutputTokens
                                ? "invalid"
                                : "valid"
                            }
                          >
                            <TextFieldLabel for="provider-max-tokens">
                              Max tokens
                            </TextFieldLabel>
                            <TextFieldInput
                              {...registerProvider("maxOutputTokens")}
                              id="provider-max-tokens"
                              value={providerValues().maxOutputTokens}
                              inputmode="numeric"
                              min="1"
                              max="8192"
                            />
                            <Show
                              when={providerErrors.maxOutputTokens?.message}
                            >
                              {(message) => (
                                <TextFieldErrorMessage>
                                  {message()}
                                </TextFieldErrorMessage>
                              )}
                            </Show>
                          </TextField>
                          <TextField
                            validationState={
                              providerErrors.timeoutMs ? "invalid" : "valid"
                            }
                          >
                            <TextFieldLabel for="provider-timeout">
                              Timeout ms
                            </TextFieldLabel>
                            <TextFieldInput
                              {...registerProvider("timeoutMs")}
                              id="provider-timeout"
                              value={providerValues().timeoutMs}
                              inputmode="numeric"
                              min="1000"
                              max="120000"
                            />
                            <Show when={providerErrors.timeoutMs?.message}>
                              {(message) => (
                                <TextFieldErrorMessage>
                                  {message()}
                                </TextFieldErrorMessage>
                              )}
                            </Show>
                          </TextField>
                        </Grid>
                        <TextField
                          validationState={
                            providerErrors.apiKey ? "invalid" : "valid"
                          }
                        >
                          <TextFieldLabel for="provider-api-key">
                            API key
                          </TextFieldLabel>
                          <TextFieldInput
                            {...registerProvider("apiKey")}
                            id="provider-api-key"
                            value={providerValues().apiKey}
                            type="password"
                            autocomplete="off"
                            placeholder="Optional; encrypted on SD"
                            maxlength={entityLimits.apiKey}
                          />
                          <TextFieldDescription>
                            The key is never returned by the status API.
                          </TextFieldDescription>
                          <Show when={providerErrors.apiKey?.message}>
                            {(message) => (
                              <TextFieldErrorMessage>
                                {message()}
                              </TextFieldErrorMessage>
                            )}
                          </Show>
                        </TextField>

                        <Show
                          when={
                            providerMutation.isPending ||
                            providerMutation.isSuccess ||
                            providerMutation.isError
                          }
                        >
                          <OperationStatus
                            title={
                              providerMutation.isPending
                                ? "Saving encrypted provider settings"
                                : providerMutation.isError
                                  ? "Provider setup needs attention"
                                  : "Provider settings saved"
                            }
                            description={
                              providerMutation.error?.message ??
                              providerMutation.data?.detail ??
                              "Saving configuration does not validate external provider interoperability."
                            }
                            active={providerMutation.isPending}
                          />
                        </Show>
                      </fieldset>
                      <SheetFooter class="shrink-0 gap-2 border-t px-6 py-4">
                        <Button
                          type="button"
                          variant="outline"
                          onClick={closeProviderSheet}
                          disabled={providerMutation.isPending}
                        >
                          Cancel
                        </Button>
                        <Button
                          type="submit"
                          disabled={providerMutation.isPending}
                        >
                          Save provider
                        </Button>
                      </SheetFooter>
                    </form>
                  </SheetContent>
                </Sheet>
              </div>
            </Flex>
          </Flex>
        </CardContent>
      </Card>
    </>
  );
};

export default AiAssistant;
