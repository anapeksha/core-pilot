import { createEffect, createRoot, on } from "solid-js";
import { createForm } from "solid-hook-form";
import { describe, expect, it } from "vitest";

// Exercise the installed library rather than a mock: reset does not replace
// its defaultValues reference, and structured fields use reference equality.
describe("model form dirty baselines", () => {
  it("synchronizes structured fields without dirtying pristine edits or looping", () => {
    let dispose = () => {};
    let writes = 0;
    const form = createRoot((cleanup) => {
      dispose = cleanup;
      const defaults = {
        actions: [{ type: "delay", milliseconds: 1000 }],
        actionSequence: '[{"type":"delay","milliseconds":1000}]',
      };
      const form = createForm({ defaultValues: defaults });
      createEffect(
        on(
          () => JSON.stringify(form.values().actions),
          (value) => {
            if (form.values().actionSequence !== value) {
              writes++;
              form.setValue("actionSequence", value, { shouldDirty: true });
            }
          },
        ),
      );
      return form;
    });
    try {
      expect(form.formState.isDirty()).toBe(false);
      form.setValue("actions", [{ type: "delay", milliseconds: 2000 }]);
      expect(form.formState.isDirty()).toBe(true);
      form.setValue("actions", [{ type: "delay", milliseconds: 1000 }]);
      expect(form.formState.isDirty()).toBe(false);
      expect(writes).toBe(2);
    } finally {
      dispose();
    }
  });
  it("uses supplied defaults after an edit reset and supports change/revert", () => {
    createRoot((dispose) => {
      try {
        const defaults = { name: "" };
        const form = createForm({ defaultValues: defaults });
        Object.assign(defaults, { name: "Kitchen" });
        form.reset({ name: "Kitchen" });
        expect(form.formState.isDirty()).toBe(false);
        form.setValue("name", "Kitchen", { shouldDirty: true });
        expect(form.formState.isDirty()).toBe(false);
        form.setValue("name", "Dining", { shouldDirty: true });
        expect(form.formState.isDirty()).toBe(true);
        form.setValue("name", "Kitchen", { shouldDirty: true });
        expect(form.formState.isDirty()).toBe(false);
      } finally {
        dispose();
      }
    });
  });

  it("preserves action order but ignores group selection order", () => {
    createRoot((dispose) => {
      try {
        const members = (ids: string[]) => JSON.stringify([...ids].sort());
        const defaults = {
          memberSelection: members(["b", "a"]),
          actionSequence: JSON.stringify([
            { type: "delay", milliseconds: 1000 },
            { type: "local_notification", message: "Done" },
          ]),
        };
        const form = createForm({ defaultValues: defaults });
        form.setValue("memberSelection", members(["a", "b"]), {
          shouldDirty: true,
        });
        expect(form.formState.isDirty()).toBe(false);
        form.setValue("memberSelection", members(["a"]), { shouldDirty: true });
        expect(form.formState.isDirty()).toBe(true);
        form.setValue("memberSelection", members(["b", "a"]), {
          shouldDirty: true,
        });
        expect(form.formState.isDirty()).toBe(false);
        const actions = JSON.parse(defaults.actionSequence);
        form.setValue(
          "actionSequence",
          JSON.stringify([...actions].reverse()),
          { shouldDirty: true },
        );
        expect(form.formState.isDirty()).toBe(true);
        form.setValue("actionSequence", defaults.actionSequence, {
          shouldDirty: true,
        });
        expect(form.formState.isDirty()).toBe(false);
      } finally {
        dispose();
      }
    });
  });

  it("keeps a failed draft dirty and clears it only when accepted/reset", () => {
    createRoot((dispose) => {
      try {
        const defaults = { name: "", actionSequence: "[]" };
        const form = createForm({ defaultValues: defaults });
        form.setValue("name", "Evening", { shouldDirty: true });
        form.setValue(
          "actionSequence",
          '[{"type":"delay","milliseconds":1000}]',
          { shouldDirty: true },
        );
        // A rejected mutation must not reset the form.
        expect(form.values().name).toBe("Evening");
        expect(form.formState.isDirty()).toBe(true);
        Object.assign(defaults, { name: "", actionSequence: "[]" });
        form.reset({ ...defaults });
        expect(form.formState.isDirty()).toBe(false);
        expect(form.values()).toEqual(defaults);
      } finally {
        dispose();
      }
    });
  });
});
