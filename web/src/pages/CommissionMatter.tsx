import { For, Show } from "solid-js";

import { Alert } from "~/components/alert";
import { Button } from "~/components/button";
import { DiagnosticDisclosure } from "~/components/diagnostic-disclosure";
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
import { fallbackMatter } from "~/lib/api";
import { cn } from "~/lib/utils";
import { boolStatus, okWhen, type StatusItem } from "~/lib/status";
import { matterQuery as createMatterQuery } from "~/queries/matter";

function statusToneClass(tone: StatusItem["tone"]): string {
  return tone === "muted" ? "text-muted-foreground" : "text-foreground";
}

function StatusGrid(props: { items: StatusItem[] }) {
  return (
    <Grid colsMd={2} class="gap-3">
      <For each={props.items}>
        {(item) => (
          <Card class="min-h-24">
            <CardHeader>
              <CardTitle class="text-xs font-medium text-muted-foreground">
                {item.label}
              </CardTitle>
              <CardDescription
                class={cn(
                  "text-base font-semibold",
                  statusToneClass(item.tone),
                )}
              >
                {item.value}
              </CardDescription>
            </CardHeader>
          </Card>
        )}
      </For>
    </Grid>
  );
}

function CommissionMatterSkeleton() {
  return (
    <Flex
      flexDirection="col"
      alignItems="stretch"
      class="gap-5"
      aria-busy="true"
      aria-live="polite"
    >
      <Grid colsMd={2} class="gap-3">
        <For each={Array.from({ length: 13 })}>
          {() => (
            <Card class="min-h-24">
              <CardHeader>
                <Skeleton class="h-3 w-24" />
                <Skeleton class="h-5 w-32" />
              </CardHeader>
            </Card>
          )}
        </For>
      </Grid>
      <Alert>
        <Skeleton class="h-4 w-full max-w-xl" />
      </Alert>
    </Flex>
  );
}

export default function CommissionMatter() {
  const matterQuery = createMatterQuery();
  const matter = () => matterQuery.data ?? fallbackMatter;

  return (
    <Card class="w-full overflow-hidden">
      <CardHeader>
        <Flex
          alignItems="start"
          justifyContent="between"
          class="flex-col gap-4 sm:flex-row"
        >
          <div class="max-w-3xl">
            <div class="mb-3 h-px w-12 bg-foreground" />
            <h1 class="text-xl font-semibold tracking-tight">Device Setup</h1>
            <CardDescription>
              Check pairing status and device discovery
            </CardDescription>
          </div>
          <Button
            as="a"
            href="#/devices/add"
            size="sm"
            class="shrink-0 self-start"
          >
            Add device
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
          <Show
            when={!(matterQuery.isFetching && !matterQuery.data)}
            fallback={<CommissionMatterSkeleton />}
          >
            <StatusGrid
              items={
                [
                  {
                    label: "Paired devices",
                    value: boolStatus(matter().commissioned),
                    tone: okWhen(matter().commissioned),
                  },
                  {
                    label: "Matter network",
                    value: boolStatus(
                      matter().fabric_configured,
                      "set up",
                      "setup needed",
                    ),
                    tone: okWhen(matter().fabric_configured),
                  },
                  {
                    label: "Live updates",
                    value: matter().subscriptions_active > 0 ? "on" : "off",
                    tone: matter().subscriptions_active > 0 ? "ok" : "muted",
                  },
                  {
                    label: "Network support",
                    value: boolStatus(
                      matter().ipv6_ready,
                      "ready",
                      "setup needed",
                    ),
                    tone: okWhen(matter().ipv6_ready),
                  },
                  {
                    label: "Device discovery",
                    value: boolStatus(
                      matter().mdns_ready,
                      "ready",
                      "setup needed",
                    ),
                    tone: okWhen(matter().mdns_ready),
                  },
                  {
                    label: "Device details",
                    value: boolStatus(
                      matter().dns_sd_ready,
                      "ready",
                      "setup needed",
                    ),
                    tone: okWhen(matter().dns_sd_ready),
                  },
                  {
                    label: "Thread dataset",
                    value: boolStatus(
                      matter().thread_dataset_configured,
                      "generated and encrypted",
                      "boot check failed",
                    ),
                    tone: matter().thread_dataset_configured ? "ok" : "muted",
                  },
                  {
                    label: "Thread pairing",
                    value: boolStatus(
                      matter().thread_ready,
                      "ready",
                      "blocked",
                    ),
                    tone: matter().thread_ready ? "ok" : "warn",
                  },
                  {
                    label: "rs-matter parser",
                    value: boolStatus(
                      matter().parser_ready,
                      "ready",
                      "missing",
                    ),
                    tone: okWhen(matter().parser_ready),
                  },
                  {
                    label: "Transport adapter",
                    value: boolStatus(
                      matter().transport_adapter_ready,
                      "ready",
                      "missing",
                    ),
                    tone: matter().transport_adapter_ready ? "ok" : "warn",
                  },
                  {
                    label: "Fabric credentials",
                    value: boolStatus(
                      matter().fabric_credentials_ready,
                      "ready",
                      "missing",
                    ),
                    tone: matter().fabric_credentials_ready ? "ok" : "warn",
                  },
                  {
                    label: "PASE / CASE",
                    value:
                      matter().pase_ready && matter().case_ready
                        ? "ready"
                        : "missing",
                    tone:
                      matter().pase_ready && matter().case_ready
                        ? "ok"
                        : "warn",
                  },
                  {
                    label: "Subscriptions",
                    value: boolStatus(
                      matter().subscriptions_ready,
                      "ready",
                      "missing",
                    ),
                    tone: matter().subscriptions_ready ? "ok" : "warn",
                  },
                ].slice(0, 7) as StatusItem[]
              }
            />
            <DiagnosticDisclosure
              title="Matter implementation diagnostics"
              summary="Protocol stack readiness and subscription support"
            >
              <StatusGrid
                items={[
                  {
                    label: "rs-matter parser",
                    value: boolStatus(
                      matter().parser_ready,
                      "ready",
                      "missing",
                    ),
                    tone: okWhen(matter().parser_ready),
                  },
                  {
                    label: "Transport adapter",
                    value: boolStatus(
                      matter().transport_adapter_ready,
                      "ready",
                      "missing",
                    ),
                    tone: matter().transport_adapter_ready ? "ok" : "warn",
                  },
                  {
                    label: "Fabric credentials",
                    value: boolStatus(
                      matter().fabric_credentials_ready,
                      "ready",
                      "missing",
                    ),
                    tone: matter().fabric_credentials_ready ? "ok" : "warn",
                  },
                  {
                    label: "PASE / CASE",
                    value:
                      matter().pase_ready && matter().case_ready
                        ? "ready"
                        : "unavailable",
                    tone:
                      matter().pase_ready && matter().case_ready
                        ? "ok"
                        : "warn",
                  },
                  {
                    label: "Subscriptions",
                    value: boolStatus(
                      matter().subscriptions_ready,
                      "ready",
                      "missing",
                    ),
                    tone: matter().subscriptions_ready ? "ok" : "warn",
                  },
                ]}
              />
            </DiagnosticDisclosure>

            <Alert>
              <span>{matter().detail}</span>
            </Alert>
          </Show>
        </Flex>
      </CardContent>
    </Card>
  );
}
