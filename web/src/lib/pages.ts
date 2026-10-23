export type PageId =
  | "overview"
  | "activity"
  | "devices"
  | "rooms"
  | "groups"
  | "scenes"
  | "automations"
  | "ai"
  | "system";

export type Page = {
  id: PageId;
  path: string;
  label: string;
  icon:
    | "activity"
    | "cpu"
    | "house"
    | "boxes"
    | "sparkles"
    | "timer"
    | "bot"
    | "settings";
  description: string;
};

export const pages: Page[] = [
  {
    id: "overview",
    path: "/",
    label: "Home",
    icon: "house",
    description: "Operational home overview and hub health",
  },
  {
    id: "activity",
    path: "/activity",
    label: "Activity",
    icon: "activity",
    description: "Encrypted firmware event history",
  },
  {
    id: "devices",
    path: "/devices",
    label: "Devices",
    icon: "cpu",
    description: "Connected devices and controls",
  },

  {
    id: "rooms",
    path: "/rooms",
    label: "Rooms",
    icon: "house",
    description: "Room assignment and organization",
  },
  {
    id: "groups",
    path: "/groups",
    label: "Groups",
    icon: "boxes",
    description: "Control devices together",
  },
  {
    id: "scenes",
    path: "/scenes",
    label: "Scenes",
    icon: "sparkles",
    description: "Create, edit, and activate scenes",
  },
  {
    id: "automations",
    path: "/automations",
    label: "Automations",
    icon: "timer",
    description: "Automatic device routines",
  },

  {
    id: "system",
    path: "/system",
    label: "System",
    icon: "settings",
    description: "Network, updates, and reset options",
  },
  {
    id: "ai",
    path: "/ai",
    label: "Assistant",
    icon: "bot",
    description: "Optional chat and setup help",
  },
];

export const dashboardAuthPolicy = "none-trusted-lan" as const;

export function pageByPath(path: string): Page {
  return (
    pages.find((page) => page.path === path) ??
    pages.find(
      (page) => page.path !== "/" && path.startsWith(`${page.path}/`),
    ) ??
    pages[0]
  );
}

export function pageById(id: PageId): Page {
  return pages.find((page) => page.id === id) ?? pages[0];
}
