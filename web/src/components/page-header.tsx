import { Show, type JSX } from "solid-js";

type PageHeaderProps = {
  title: string;
  description?: JSX.Element;
  actions?: JSX.Element;
  children?: JSX.Element;
};

function PageHeader(props: PageHeaderProps) {
  return (
    <header class="flex min-w-0 flex-col flex-wrap items-start justify-between gap-4 sm:flex-row">
      <div class="min-w-0 max-w-3xl">
        <h1 class="text-xl font-semibold tracking-tight">{props.title}</h1>
        <Show when={props.description}>
          <p class="text-sm text-muted-foreground">{props.description}</p>
        </Show>
        {props.children}
      </div>
      <Show when={props.actions}>
        <div class="flex w-full min-w-0 flex-wrap items-center gap-2 sm:w-auto">
          {props.actions}
        </div>
      </Show>
    </header>
  );
}

export { PageHeader };
