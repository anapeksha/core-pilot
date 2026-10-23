import { createQuery } from "@tanstack/solid-query";
import { loadAutomations, type AutomationsResponse } from "../lib/api";

export const fallbackAutomations: AutomationsResponse = {
  automations: [],
  detail: "Create automations to run local rules on schedules and events.",
};

export function automationsQuery() {
  return createQuery(() => ({
    queryKey: ["automations"],
    queryFn: ({ signal }) => loadAutomations(signal),
    placeholderData: fallbackAutomations,
  }));
}
