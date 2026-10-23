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
import { Skeleton } from "~/components/skeleton";
import {
  Switch,
  SwitchControl,
  SwitchLabel,
  SwitchThumb,
} from "~/components/switch";

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
  type AiChatMessage,
  type AiMemorySection,
} from "../lib/api";

import {
  aiChatMutation,
  aiMemoryMutation,
  aiProviderMutation,
} from "../mutations/ai";
import {
  aiMemoryQuery as createAiMemoryQuery,
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

const providerSchema = z.object({
  baseUrl: bytesAtMost(
    entityLimits.providerUrl,
    "Base URL is too long.",
  ).refine((value) => value.trim().length > 0, "Enter a base URL."),
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

const memorySchema = z.object({
  markdown: bytesAtMost(
    entityLimits.memoryMarkdown,
    "Memory must be 12 KiB or less.",
  ),
});

type ChatFormValues = z.input<typeof chatSchema>;
type ProviderFormValues = z.input<typeof providerSchema>;
type MemoryFormValues = z.input<typeof memorySchema>;

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

function redactToolResult(result: string): string {
  return result
    .replace(
      /(api[_-]?key|password|token)\s*[:=]\s*[^,\s}]+/gi,
      "$1: [redacted]",
    )
    .slice(0, 2048);
}

function messageLabel(role: AiChatMessage["role"]): string {
  if (role === "user") return "You";
  if (role === "tool") return "Tool";
  return "Core Pilot";
}

const ChatMessage: Component<{ message: AiChatMessage; pending?: boolean }> = (
  props,
) => {
  const isUserMessage = () => props.message.role === "user";
  return (
    <Message
      align={isUserMessage() ? "end" : "start"}
      aria-live={props.pending ? "polite" : undefined}
    >
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
        <Show when={props.message.role === "tool"}>
          <MessageFooter>Executed local tool</MessageFooter>
          <details class="mt-2 rounded border px-3 py-2 text-xs">
            <summary class="min-h-6 cursor-pointer font-medium">
              Redacted tool details
            </summary>
            <pre class="mt-2 max-h-48 overflow-auto whitespace-pre-wrap wrap-break-word text-muted-foreground">
              {redactToolResult(props.message.content)}
            </pre>
          </details>
        </Show>
      </MessageContent>
    </Message>
  );
};

const MemorySectionForm: Component<{
  section: AiMemorySection;
  pending: boolean;
  onSave: (markdown: string) => void;
}> = (props) => {
  const { errors, handleSubmit, register, reset, values } =
    createForm<MemoryFormValues>({
      defaultValues: { markdown: props.section.markdown },
      mode: "onSubmit",
      resolver: zodResolver(memorySchema),
    });

  createEffect(() => reset({ markdown: props.section.markdown }));

  return (
    <form
      onSubmit={handleSubmit((formValues) => props.onSave(formValues.markdown))}
    >
      <Card>
        <CardHeader class="flex-row items-start justify-between">
          <CardTitle>{props.section.title}</CardTitle>
          <Button
            variant="outline"
            size="sm"
            type="button"
            onClick={() => reset({ markdown: props.section.markdown })}
          >
            Reset
          </Button>
        </CardHeader>
        <CardContent>
          <TextField validationState={errors.markdown ? "invalid" : "valid"}>
            <TextFieldLabel for={`memory-${props.section.id}`} class="sr-only">
              {props.section.title} markdown
            </TextFieldLabel>
            <TextFieldTextArea
              {...register("markdown")}
              id={`memory-${props.section.id}`}
              value={values().markdown}
              class="min-h-36 font-mono text-xs"
            />
            <Show when={errors.markdown?.message}>
              {(message) => (
                <TextFieldErrorMessage>{message()}</TextFieldErrorMessage>
              )}
            </Show>
          </TextField>
        </CardContent>
        <CardFooter class="justify-end">
          <Button size="sm" type="submit" disabled={props.pending}>
            Save encrypted
          </Button>
        </CardFooter>
      </Card>
    </form>
  );
};

const MAX_RENDERED_CHAT_MESSAGES = 50;

function appendChatMessage(
  current: AiChatMessage[],
  message: AiChatMessage,
): AiChatMessage[] {
  return [...current, message].slice(-MAX_RENDERED_CHAT_MESSAGES);
}

const AiAssistant: Component = () => {
  const aiQuery = createAiQuery();
  const memoryQuery = createAiMemoryQuery();
  const toolsQuery = createMcpToolsQuery();

  const providerMutation = aiProviderMutation();
  const chatMutation = aiChatMutation();
  const memoryMutation = aiMemoryMutation();

  const [providerMode, setProviderMode] =
    createSignal<ProviderMode>("openai-compatible");
  const [providerBackend, setProviderBackend] = createSignal<
    SelectOption<ProviderBackend>
  >(providerBackendOptions[0]);
  const [providerAuthType, setProviderAuthType] = createSignal<
    SelectOption<ProviderAuthType>
  >(providerAuthOptions[0]);
  const [assistantEnabled, setAssistantEnabled] = createSignal(false);

  const [providerSheetOpen, setProviderSheetOpen] = createSignal(false);
  const {
    errors: providerErrors,
    handleSubmit: handleProviderSubmit,
    register: registerProvider,
    reset: resetProvider,
    values: providerValues,
  } = createForm<ProviderFormValues>({
    defaultValues: {
      baseUrl: "https://api.openai.com/v1",
      model: "gpt-4o-mini",
      maxOutputTokens: "1024",
      timeoutMs: "30000",
      apiKey: "",
    },
    mode: "onSubmit",
    resolver: zodResolver(providerSchema),
  });
  const {
    errors: chatErrors,
    handleSubmit: handleChatSubmit,
    register: registerChat,
    reset: resetChat,
    values: chatValues,
  } = createForm<ChatFormValues>({
    defaultValues: { message: "" },
    mode: "onSubmit",
    resolver: zodResolver(chatSchema),
  });
  const [messages, setMessages] = createSignal<AiChatMessage[]>([
    {
      role: "assistant",
      content:
        "Hi — I can help with Core Pilot after you configure an encrypted HTTPS provider. Local tool execution is not available yet.",
    },
  ]);
  const chatId = newChatId();
  const ai = createMemo(() => aiQuery.data ?? fallbackAi);

  const shouldShowToast = createToastGate();
  createEffect(() => {
    const success = providerMutation.isSuccess || memoryMutation.isSuccess;
    const error = providerMutation.isError || memoryMutation.isError;
    const successMessage =
      providerMutation.data?.detail ??
      memoryMutation.data?.detail ??
      "AI settings updated.";
    const errorMessage =
      providerMutation.error?.message ??
      memoryMutation.error?.message ??
      "AI settings could not be updated.";
    const key = success
      ? `success:${successMessage}`
      : error
        ? `error:${errorMessage}`
        : "";
    if (!shouldShowToast(key)) return;
    if (success) toast.success(successMessage);
    if (error) toast.error(errorMessage);
  });

  const providerDefaults = (): ProviderFormValues => ({
    baseUrl: "https://api.openai.com/v1",
    model: "gpt-4o-mini",
    maxOutputTokens: "1024",
    timeoutMs: "30000",
    apiKey: "",
  });

  function closeProviderSheet() {
    setProviderSheetOpen(false);
    setProviderMode("openai-compatible");
    setProviderBackend(providerBackendOptions[0]);
    setProviderAuthType(providerAuthOptions[0]);
    setAssistantEnabled(false);
    resetProvider(providerDefaults());
  }

  function chooseProvider(mode: ProviderMode) {
    setProviderMode(mode);
    if (mode === "openai-compatible") {
      setProviderBackend(providerBackendOptions[0]);
      setProviderAuthType(providerAuthOptions[0]);
      resetProvider({
        ...providerValues(),
        baseUrl: "https://api.openai.com/v1",
        model: "gpt-4o-mini",
      });
    } else {
      setProviderBackend(providerBackendOptions[1]);
      setProviderAuthType(providerAuthOptions[1]);
      resetProvider({
        ...providerValues(),
        baseUrl: "https://api.anthropic.com/v1",
        model: "claude-sonnet-4-5",
      });
    }
  }

  const submitProvider = handleProviderSubmit((formValues) => {
    const apiKey = formValues.apiKey.trim();
    providerMutation.mutate(
      {
        provider: {
          backend: providerBackend().value,
          auth_type: providerAuthType().value,
          base_url: formValues.baseUrl.trim(),
          model: formValues.model.trim(),
          max_output_tokens: Number(formValues.maxOutputTokens),
          timeout_ms: Number(formValues.timeoutMs),
          api_key: apiKey || null,
          api_key_redacted: Boolean(apiKey),
        },
      },
      { onSuccess: (response) => response.accepted && closeProviderSheet() },
    );
  });

  const sendChat = handleChatSubmit((formValues) => {
    const message = formValues.message.trim();
    if (chatMutation.isPending) return;
    const history = messages().slice(-12);
    setMessages((current) =>
      appendChatMessage(current, { role: "user", content: message }),
    );
    resetChat({ message: "" });
    chatMutation.mutate(
      {
        message,
        history,
        chat_id: chatId,
        allow_actions: false,
      },
      {
        onSuccess: (response) => {
          setMessages((current) => {
            const withAnswer = appendChatMessage(current, {
              role: "assistant",
              content: response.reply,
            });
            return response.actions.reduce(
              (next, action) =>
                appendChatMessage(next, {
                  role: "tool",
                  content: `${action.tool}: ${action.result_json}`,
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

  const availableTools = createMemo(() =>
    (toolsQuery.data?.tools ?? []).filter((tool) => tool.available),
  );

  return (
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
                      Model-generated answers only. Local tool execution is not
                      available yet.
                    </CardDescription>
                  </Flex>
                </Flex>
              </CardHeader>
              <CardContent class="scroll-fade flex max-h-96 min-h-48 flex-col gap-4 overflow-y-auto sm:max-h-112 xl:max-h-none xl:min-h-105 xl:flex-1">
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
                <Show when={chatMutation.isPending}>
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
                          !chatValues().message.trim() || chatMutation.isPending
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
                    Provider credentials are configured in a focused setup panel
                    and are never returned by the status API.
                  </p>
                </CardContent>
                <CardFooter class="justify-end">
                  <Button
                    type="button"
                    onClick={() => setProviderSheetOpen(true)}
                  >
                    {ai().provider_configured
                      ? "Update provider"
                      : "Add provider"}
                  </Button>
                </CardFooter>
              </Card>

              <Card>
                <CardHeader>
                  <CardTitle>Local tool trust</CardTitle>
                  <CardDescription>
                    Tool permissions are stored locally, but chat cannot execute
                    tools in this release.
                  </CardDescription>
                </CardHeader>
                <CardContent class="space-y-3">
                  <Show
                    when={availableTools().length > 0}
                    fallback={
                      <p class="text-sm text-muted-foreground">
                        No executable local tools are currently available.
                      </p>
                    }
                  >
                    <ul
                      class="flex flex-wrap gap-2"
                      aria-label="Available local tools"
                    >
                      <For each={availableTools()}>
                        {(tool) => (
                          <li>
                            <Badge variant="outline">{tool.title}</Badge>
                          </li>
                        )}
                      </For>
                    </ul>
                  </Show>
                </CardContent>
              </Card>

              <Sheet
                open={providerSheetOpen()}
                onOpenChange={(open) =>
                  open ? setProviderSheetOpen(true) : closeProviderSheet()
                }
              >
                <SheetContent
                  position="right"
                  class="flex w-full max-w-none flex-col gap-0 overflow-hidden! p-0 sm:max-w-lg"
                >
                  <SheetHeader class="shrink-0 border-b px-6 py-5 pr-12">
                    <SheetTitle>Assistant provider</SheetTitle>
                    <SheetDescription>
                      Configure an optional encrypted model provider. Local home
                      controls do not depend on this configuration.
                    </SheetDescription>
                  </SheetHeader>
                  <form
                    class="flex min-h-0 flex-1 flex-col"
                    onSubmit={submitProvider}
                  >
                    <div class="min-h-0 flex-1 space-y-5 overflow-y-auto px-6 py-5">
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
                          onClick={() => chooseProvider("anthropic-compatible")}
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
                          required
                        />
                        <TextFieldDescription>
                          HTTPS hostname on port 443; private and IP-literal
                          endpoints are rejected.
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
                          optionValue="value"
                          optionTextValue="label"
                          value={providerBackend()}
                          onChange={(value) =>
                            value && setProviderBackend(value)
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
                          optionValue="value"
                          optionTextValue="label"
                          value={providerAuthType()}
                          onChange={(value) =>
                            value && setProviderAuthType(value)
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
                      <Grid colsSm={2} class="gap-5">
                        <TextField
                          validationState={
                            providerErrors.maxOutputTokens ? "invalid" : "valid"
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
                          <Show when={providerErrors.maxOutputTokens?.message}>
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
                      <Switch
                        checked={assistantEnabled()}
                        onChange={setAssistantEnabled}
                        class="flex items-start gap-3 rounded-md border p-3"
                      >
                        <SwitchControl class="mt-0.5">
                          <SwitchThumb />
                        </SwitchControl>
                        <Flex
                          flexDirection="col"
                          alignItems="start"
                          justifyContent="start"
                          class="gap-1"
                        >
                          <SwitchLabel>Enable assistant</SwitchLabel>
                          <p class="text-sm text-muted-foreground">
                            Local controls always remain available.
                          </p>
                        </Flex>
                      </Switch>
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
                    </div>
                    <SheetFooter class="shrink-0 gap-2 border-t px-6 py-4">
                      <Button
                        type="button"
                        variant="outline"
                        onClick={closeProviderSheet}
                      >
                        Cancel
                      </Button>
                      <Button
                        type="submit"
                        disabled={
                          providerMutation.isPending || !assistantEnabled()
                        }
                      >
                        Save provider
                      </Button>
                    </SheetFooter>
                  </form>
                </SheetContent>
              </Sheet>
            </div>
          </Flex>

          <Card>
            <CardHeader>
              <Flex alignItems="start" class="flex-col gap-3 sm:flex-row">
                <Flex
                  flexDirection="col"
                  alignItems="start"
                  justifyContent="start"
                  class="min-w-0 gap-1"
                >
                  <CardTitle>Memory</CardTitle>
                  <CardDescription>
                    Markdown sections are stored only as encrypted SD records.
                  </CardDescription>
                </Flex>
                <Show
                  when={!(memoryQuery.isFetching && !memoryQuery.data)}
                  fallback={<Skeleton class="h-5 w-20 rounded-full" />}
                >
                  <Badge variant="outline" class="shrink-0">
                    {memoryQuery.data?.encrypted ? "encrypted" : "not writable"}
                  </Badge>
                </Show>
              </Flex>
            </CardHeader>
            <CardContent>
              <Show
                when={!(memoryQuery.isFetching && !memoryQuery.data)}
                fallback={
                  <Grid colsLg={2} class="gap-4" aria-busy="true">
                    <For each={[0, 1]}>
                      {() => (
                        <Card>
                          <CardHeader class="flex-row items-center justify-between gap-3">
                            <Skeleton class="h-5 w-32" />
                            <Skeleton class="h-8 w-16" />
                          </CardHeader>
                          <CardContent>
                            <Skeleton class="h-36 w-full" />
                          </CardContent>
                          <CardFooter class="justify-end">
                            <Skeleton class="h-8 w-28" />
                          </CardFooter>
                        </Card>
                      )}
                    </For>
                  </Grid>
                }
              >
                <Grid colsLg={2} class="gap-4">
                  <For each={memoryQuery.data?.sections ?? []}>
                    {(section) => (
                      <MemorySectionForm
                        section={section}
                        pending={memoryMutation.isPending}
                        onSave={(markdown) =>
                          memoryMutation.mutate({ id: section.id, markdown })
                        }
                      />
                    )}
                  </For>
                </Grid>
              </Show>
            </CardContent>
          </Card>
        </Flex>
      </CardContent>
    </Card>
  );
};

export default AiAssistant;
