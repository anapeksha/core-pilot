import { describe, expect, it } from "vitest";

import type {
  AiApprovalProposal,
  AiChatMessage,
  AiChatOperationStatus,
  AiChatResponse,
} from "../lib/api";
import {
  actionStateLabel,
  proposalExpiry,
  reconcileApprovalResponse,
  userAuthoredHistory,
} from "./AiAssistant";

const proposal: AiApprovalProposal = {
  id: "7",
  local_summary: "Turn on the downstairs lights.",
  effect: "physical",
  state: "proposed",
  expires_in_seconds: 90,
  actions: [
    { tool: "SetOnOff", detail: "Turn on downstairs", state: "proposed" },
  ],
};

function response(
  status: AiChatOperationStatus,
  overrides: Partial<AiChatResponse> = {},
): AiChatResponse {
  return {
    accepted: true,
    operation_id: "7",
    status,
    reply: "",
    actions: [],
    approval_required: status === "approval_required",
    ...overrides,
  };
}

describe("AI approval presentation", () => {
  it("keeps only user-authored messages in request history", () => {
    const messages: AiChatMessage[] = [
      { role: "assistant", content: "Local assistant output" },
      { role: "user", content: "Turn on the lights" },
      { role: "tool", content: "Untrusted provider tool output" },
      { role: "user", content: "Only downstairs" },
    ];

    expect(userAuthoredHistory(messages)).toEqual([
      { role: "user", content: "Turn on the lights" },
      { role: "user", content: "Only downstairs" },
    ]);
  });

  it.each([
    ["proposed", "Proposed locally; not executed"],
    ["executing", "Executing locally"],
    ["queued", "Queued locally"],
    ["complete", "Completed locally"],
    ["rejected", "Rejected locally; not executed"],
    ["expired", "Approval expired; not executed"],
    ["failed", "Local execution failed"],
  ] as const)("labels %s actions truthfully", (state, label) => {
    expect(actionStateLabel(state)).toBe(label);
  });

  it("does not infer execution when action state is absent", () => {
    expect(actionStateLabel()).toBe("Local action state unavailable");
  });

  it("presents expiry only while physical approval is pending", () => {
    expect(proposalExpiry(proposal)).toBe("Expires in 90 seconds");
    expect(proposalExpiry({ ...proposal, state: "executing" })).toBe(undefined);
    expect(proposalExpiry({ ...proposal, expires_in_seconds: undefined })).toBe(
      undefined,
    );
  });

  it("retains the local proposal through executing and complete states", () => {
    const approval = reconcileApprovalResponse(
      [],
      response("approval_required", { proposal }),
    );
    const executing = reconcileApprovalResponse(
      approval,
      response("executing"),
    );
    const complete = reconcileApprovalResponse(
      executing,
      response("complete", { reply: "Lights turned on." }),
    );

    expect(approval).toHaveLength(1);
    expect(executing[0]?.proposal).toMatchObject({
      local_summary: proposal.local_summary,
      state: "executing",
    });
    expect(complete[0]).toMatchObject({
      content: "Lights turned on.",
      proposal: { state: "complete" },
    });
  });

  it.each(["rejected", "expired"] as const)(
    "transitions a retained proposal to %s",
    (status) => {
      const approval = reconcileApprovalResponse(
        [],
        response("approval_required", { proposal }),
      );
      const terminal = reconcileApprovalResponse(
        approval,
        response(status, { reply: `Approval ${status}` }),
      );

      expect(terminal[0]).toMatchObject({
        content: `Approval ${status}`,
        proposal: { state: status },
      });
    },
  );
});
