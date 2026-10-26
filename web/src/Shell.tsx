import { A, useLocation, useNavigate } from "@solidjs/router";
import { createEffect, For, onMount, Show, type ParentProps } from "solid-js";
import {
  HiOutlineAdjustmentsHorizontal,
  HiOutlineBolt,
  HiOutlineCpuChip,
  HiOutlineHome,
  HiOutlineRectangleGroup,
  HiOutlineSparkles,
  HiOutlineSquares2x2,
  HiOutlineXMark,
} from "solid-icons/hi";

import { CommandPalette } from "~/components/command-palette";
import { Flex } from "~/components/flex";
import {
  Sidebar,
  SidebarContent,
  SidebarGroup,
  SidebarGroupContent,
  SidebarGroupLabel,
  SidebarHeader,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
  SidebarProvider,
  SidebarRail,
  SidebarTrigger,
  useSidebar,
} from "~/components/sidebar";
import { TooltipIconButton } from "~/components/tooltip";
import { pageByPath, pages, type Page } from "~/lib/pages";
import { routePaths } from "~/lib/routePaths";

const icons: Record<Page["icon"], typeof HiOutlineHome> = {
  activity: HiOutlineBolt,

  cpu: HiOutlineCpuChip,
  house: HiOutlineHome,
  boxes: HiOutlineSquares2x2,
  sparkles: HiOutlineSparkles,
  timer: HiOutlineRectangleGroup,
  bot: HiOutlineSparkles,
  settings: HiOutlineAdjustmentsHorizontal,
};

function CoreSidebar() {
  const location = useLocation();
  const { isMobile, setOpenMobile } = useSidebar();
  const isActive = (path: string) =>
    path === "/"
      ? location.pathname === "/"
      : location.pathname === path || location.pathname.startsWith(`${path}/`);

  return (
    <Sidebar collapsible="icon">
      <SidebarHeader
        data-mobile-drawer-header
        class="border-b border-sidebar-border"
      >
        <Flex
          class="h-12 gap-2 px-1 group-data-[collapsible=icon]:justify-center group-data-[collapsible=icon]:px-0"
          justifyContent="start"
        >
          <div class="brand-orb size-8! shrink-0" aria-hidden="true">
            <HiOutlineHome size={20} />
          </div>
          <div class="min-w-0 group-data-[collapsible=icon]:hidden">
            <h2 class="truncate text-sm font-semibold">Core Pilot</h2>
            <p class="truncate text-[0.6rem] font-semibold uppercase tracking-[0.2em] text-sidebar-foreground/50">
              local home control
            </p>
          </div>
          <Show when={isMobile()}>
            <TooltipIconButton
              class="ml-auto shrink-0"
              variant="ghost"
              size="icon"
              aria-label="Close navigation"
              onClick={() => setOpenMobile(false)}
            >
              <HiOutlineXMark aria-hidden="true" />
            </TooltipIconButton>
          </Show>
        </Flex>
      </SidebarHeader>
      <SidebarContent>
        <SidebarGroup>
          <SidebarGroupLabel>Dashboard</SidebarGroupLabel>
          <SidebarGroupContent>
            <nav aria-label="Primary navigation">
              <SidebarMenu>
                <For each={pages}>
                  {(page) => {
                    const Icon = icons[page.icon];
                    return (
                      <SidebarMenuItem>
                        <SidebarMenuButton
                          as={A}
                          href={page.path}
                          end={page.path === "/"}
                          isActive={isActive(page.path)}
                          tooltip={page.label}
                          aria-current={
                            isActive(page.path) ? "page" : undefined
                          }
                          aria-label={page.label}
                          onClick={() => setOpenMobile(false)}
                          class="h-10 gap-3 group-data-[collapsible=icon]:[&>span]:hidden"
                        >
                          <Icon size={18} />
                          <span>{page.label}</span>
                        </SidebarMenuButton>
                      </SidebarMenuItem>
                    );
                  }}
                </For>
              </SidebarMenu>
            </nav>
          </SidebarGroupContent>
        </SidebarGroup>
      </SidebarContent>
      <SidebarRail />
    </Sidebar>
  );
}

export default function Shell(props: ParentProps) {
  const location = useLocation();
  const navigate = useNavigate();

  onMount(() => {
    if (
      !routePaths.includes(location.pathname as (typeof routePaths)[number])
    ) {
      navigate("/", { replace: true });
    }
  });

  createEffect(() => {
    const path = location.pathname;
    document.title = `${pageByPath(path).label} · Core Pilot`;
    queueMicrotask(() =>
      document.querySelector<HTMLElement>("#main-content")?.focus(),
    );
  });

  return (
    <SidebarProvider>
      <a
        class="fixed left-3 top-3 z-60 -translate-y-20 rounded-md bg-primary px-3 py-2 font-semibold text-primary-foreground focus:translate-y-0"
        href="#main-content"
      >
        Skip to content
      </a>
      <CoreSidebar />
      <div class="relative min-h-svh min-w-0 flex-1 bg-background text-foreground before:pointer-events-none before:absolute before:inset-x-0 before:top-0 before:h-72 before:bg-[radial-gradient(circle_at_top_right,hsl(var(--foreground)/0.045),transparent_58%)]">
        <header class="sticky top-0 z-20 h-14 border-b border-border/80 bg-background/85 px-3 backdrop-blur-xl supports-backdrop-filter:bg-background/75">
          <Flex class="h-full gap-3" justifyContent="start">
            <SidebarTrigger class="size-10" aria-label="Toggle navigation" />
            <div class="min-w-0">
              <div class="truncate text-sm font-semibold">
                {pageByPath(location.pathname).label}
              </div>
              <div class="truncate text-xs text-muted-foreground">
                Core Pilot
              </div>
            </div>
            <CommandPalette />
          </Flex>
        </header>
        <main
          id="main-content"
          tabIndex={-1}
          class="relative px-3 py-4 outline-none sm:px-6 sm:py-6 lg:px-8 lg:py-8"
        >
          <div class="mx-auto w-full max-w-6xl animate-in fade-in-0 duration-300">
            {props.children}
          </div>
        </main>
      </div>
    </SidebarProvider>
  );
}
