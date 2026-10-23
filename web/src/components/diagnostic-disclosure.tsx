import type { JSX, ParentComponent } from "solid-js";

import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "~/components/accordion";
import { CardDescription, CardTitle } from "~/components/card";

type DiagnosticDisclosureProps = {
  title: string;
  summary: string;
  children: JSX.Element;
};

/** Keeps implementation diagnostics available without competing with user status. */
const DiagnosticDisclosure: ParentComponent<DiagnosticDisclosureProps> = (
  props,
) => (
  <Accordion collapsible class="rounded-lg border bg-muted/20">
    <AccordionItem value="diagnostics" class="border-0">
      <AccordionTrigger class="px-4 py-3 text-left hover:no-underline">
        <div>
          <CardTitle class="text-sm">{props.title}</CardTitle>
          <CardDescription class="mt-1 font-normal">
            {props.summary}
          </CardDescription>
        </div>
      </AccordionTrigger>
      <AccordionContent class="border-t px-4 pt-3 text-muted-foreground">
        {props.children}
      </AccordionContent>
    </AccordionItem>
  </Accordion>
);

export { DiagnosticDisclosure };
