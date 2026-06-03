# Claw Core / Agent Tool Architecture Refactor TODO

This TODO tracks the refactor needed to make tool visibility, authorization, agent identity, and agent type policy consistent across root agents and subagents.

## Implementation Priority

Recommended implementation order:

1. P0: Runtime agent identity
2. P0: Unified tool visibility and execution authorization
3. P1: Access policy replacing special-case root-only behavior
4. P1: Fine-grained per-tool policy beyond capability groups
5. P2: Agent type runtime profiles
6. P2: Prompt role and runtime permission binding
7. P3: Separate session visibility from agent permission

Priority rationale:

- Runtime identity is the foundation. Without `agent_id` and `agent_type` in capability context, later policies cannot reliably distinguish one subagent from another.
- Unified authorization is urgent because tool visibility and actual execution must agree before adding more policy dimensions.
- Access policy should replace one-off flags early to avoid growing more special cases.
- Fine-grained per-tool policy depends on the unified authorization path and should be added before agent profiles consume it.
- Agent profiles and prompt-policy binding are higher-level orchestration features; they depend on identity and policy primitives.
- Session/agent permission separation is important for architecture hygiene, but it is less urgent than preventing incorrect tool exposure and execution.

## P0. Runtime Agent Identity

Original issue: Replace hard-coded root/sub with runtime agent identity.

Status: Implemented.

### Problem

Current capability context only distinguishes root and subagent. It does not carry the full identity needed for fine-grained permissions.

### TODO

- Extend `claw_cap_call_context_t` with:
  - `agent_id`
  - `agent_type`
  - optional `parent_agent_id`
  - optional `parent_session_id`
- Extend `claw_cap_core_call_user_ctx_t` with the same identity fields needed to populate call context.
- Populate identity in `claw_agent_mgr_start_agent_core()` for root and subagents.
- Ensure `claw_cap_call_from_core()` copies identity from `cap_user_ctx` into `claw_cap_call_context_t`.
- Ensure tool collection providers also receive enough identity to build the correct tool list.

### Acceptance Criteria

- Capability execution can identify the exact calling agent.
- Subagents carry their configured `agent_type` into capability policy checks.
- Root agent has stable identity, using `CLAW_AGENT_MGR_ROOT_AGENT_ID`.

## P0. Unified Tool Visibility And Execution Authorization

Original issue: Unify tool visibility and execution authorization.

### Problem

Current tool visibility and execution authorization are partially coupled through `claw_cap_is_llm_visible()`, but the policy model is incomplete. It mostly knows caller role, session-visible groups, and `CLAW_CAP_FLAG_ROOT_AGENT_ONLY`.

### TODO

- Introduce one internal authorization function used by both tool JSON generation and `claw_cap_call()`.
- Make the function evaluate:
  - capability availability
  - callable-by-LLM flag
  - global/group/session visibility
  - caller role
  - agent identity
  - agent type
  - descriptor access policy
- Ensure a tool that is not exposed to the current agent cannot be executed by direct tool-call name.
- Add log output for denied calls with cap name, caller role, agent id, agent type, and session id.
- Keep existing behavior compatible when no explicit policy is configured.

### Acceptance Criteria

- LLM tool list and actual tool execution use the same policy decision.
- A hidden tool cannot be executed by direct `claw_cap_call()` from root/subagent context.
- Existing root-only tools still behave as root-only.

## P1. Access Policy Replacing Special-Case Root-Only Behavior

Original issue: Replace special-case root-only flag with access policy.

### Problem

`CLAW_CAP_FLAG_ROOT_AGENT_ONLY` is a special-case flag. Adding sub-only, type-only, or instance-only behavior as more flags will not scale cleanly.

### TODO

- Add `claw_cap_access_policy_t`.
- Add `const claw_cap_access_policy_t *access_policy` to `claw_cap_descriptor_t`.
- Treat `CLAW_CAP_FLAG_ROOT_AGENT_ONLY` as a backward-compatible shorthand.
- Prefer explicit access policies for new capabilities.
- Gradually migrate root-only descriptors, starting with `cap_agent_mgr`.

### Acceptance Criteria

- New capabilities can express root-only, sub-only, and type-specific access without new flags.
- Existing descriptors using `CLAW_CAP_FLAG_ROOT_AGENT_ONLY` continue to work.
- `cap_agent_mgr` remains inaccessible to subagents.

## P1. Fine-Grained Tool Policy Beyond Capability Groups

Original issue: Add fine-grained tool policy beyond capability groups.

### Problem

Capability group visibility is too coarse for common agent patterns such as read-only file tools, web-only research agents, or hardware-specific workers.

### TODO

- Add optional per-tool access policy to `claw_cap_descriptor_t`.
- Support policy fields for:
  - root allowed
  - subagent allowed
  - allowed agent types
  - allowed agent ids
- Keep group visibility as a coarse outer filter.
- Apply descriptor policy as the final per-tool authorization check.
- Avoid splitting capability groups solely to express one tool's permission.

### Acceptance Criteria

- A single capability group can contain tools with different root/sub/type policies.
- A subagent can be allowed to call one tool in a group while being denied another.
- Existing group whitelist behavior remains supported.

## P2. Agent Type Runtime Profiles

Original issue: Make agent type a runtime profile, not just prompt text.

### Problem

`agent_type` currently affects the subagent system prompt, but not runtime permissions or tool visibility.

### TODO

- Define an agent profile structure for each agent type.
- Include at minimum:
  - `agent_type`
  - system prompt overlay
  - allowed capability groups
  - allowed tool names
  - denied tool names
- Update `claw_agent_mgr_config_t` to accept agent type profiles.
- Use the selected profile when spawning subagents.
- Continue supporting default prompt-only agent types for compatibility.
- Decide precedence:
  - descriptor hard deny wins
  - explicit agent profile deny wins over allow
  - explicit tool allow can narrow group allow

### Acceptance Criteria

- `research`, `coding`, `worker`, or custom subagent types can have different tool sets.
- Prompt role and runtime tool permissions are derived from the same agent type profile.
- Unknown agent types fall back to safe default subagent behavior.

## P2. Prompt Role And Runtime Permission Binding

Original issue: Bind prompt role and runtime permission policy.

### Problem

Prompt instructions are currently relied on for some behavior boundaries, but prompt text is not a hard security boundary.

### TODO

- Ensure every built-in subagent type has both:
  - prompt instructions
  - runtime tool policy
- Keep prompt instructions aligned with available tools.
- Avoid giving a subagent tools that contradict its role prompt.
- Add tests for common built-in types:
  - generic subagent
  - research
  - coding/worker
  - debug/debugger
- Document that prompts guide behavior while access policy enforces boundaries.

### Acceptance Criteria

- A `research` subagent does not receive implementation or device-control tools unless explicitly configured.
- A `coding` subagent receives only the tools intended by its profile.
- Tool denial does not rely on the model following prompt text.

## P3. Separate Session Visibility From Agent Permission

Original issue: Separate session visibility from agent permission.

### Problem

Session-specific tool visibility exists today, but session is a conversation/history boundary, not the best owner for agent permission.

### TODO

- Keep `claw_cap_set_session_llm_visible_groups()` as compatibility and override mechanism.
- Introduce agent-level tool grants keyed by `agent_id`.
- Use session visibility as an outer compatibility filter only.
- Use agent identity and policy as the primary permission source.
- Clean up agent-level grants when deleting an agent.
- Decide whether closing an agent preserves grants for later resume.
- Document the lifecycle behavior.

### Acceptance Criteria

- Tool permissions can be assigned to a specific agent instance.
- Deleting a subagent removes its instance-specific grants.
- Session history and agent permission are not treated as the same concept.
