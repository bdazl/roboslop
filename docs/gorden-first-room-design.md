# Gorden first-room puzzle design

This document proposes a concrete shape for Gorden's first locked-room
vertical slice. It elaborates the accepted M5 direction in
[the roadmap](roadmap.md) and [the architecture](architecture.md), but does
not by itself lock names, commands, data structures or progression values.
Decisions made during implementation belong in [decisions.md](decisions.md).

## Purpose

The first puzzle should teach the player how to collaborate with Gorden, not
merely how to operate a terminal. It should exercise the existing gameplay
interfaces and establish four ideas that can grow with the game:

- the terminal changes real, authoritative world state;
- Gorden can move to and inspect things on the player's behalf;
- Gorden has a deterministic routine mode and an optional model-backed mode;
- conversation, remote communication and cognitive progression can become
  capabilities rather than permanent debug conveniences.

The room must remain completable without an LLM, API key or network service.
Model-backed behaviour may make communication more natural and produce richer
reactions, but must not be a progression gate.

## Proposed puzzle: the safety interlock

The exit door is physically blocked by a safety interlock. The computer can
control the door actuator, but it lacks two pieces of verification data that
must be obtained from the room.

A representative initial exchange is:

```text
$ door status
LOCKED
Interlock verification incomplete:
  actuator power      OK
  maintenance tag     UNKNOWN
  conduit order       UNKNOWN
```

A log or diagnostic command tells the player where the missing information
can be observed:

```text
$ logs interlock
After replacing the controller, verify the maintenance tag in
conduit bay C and enter the attached relay order.
```

The maintenance tag and relay order are placed behind or below the power unit,
where Gorden can inspect them. The player asks Gorden to investigate. Gorden
moves to the target, inspects it and reports something like:

> I found maintenance tag C-17. The relays are blue, yellow, blue, red.

The player combines the physical observation with the computer's instructions:

```text
$ interlock verify C-17 blue yellow blue red
Verification accepted.

$ door open
Opening exit...
```

Puzzle success changes authoritative gameplay state. The door's collision and
visual state change, a world event is emitted, Gorden can perceive and react to
the event, and the progression survives save/load.

The exact tag and sequence need not be random. A stable authored answer is
simpler to test, replay and expose through controller-friendly interaction.
Variation can follow when it creates a concrete gameplay benefit.

## Intended loop

```text
explore room
  -> inspect terminal
  -> discover missing verification
  -> enter conversation with Gorden
  -> ask Gorden to inspect the power unit
  -> receive the physical clue
  -> verify the interlock at the terminal
  -> open the real door
  -> unlock remote communication
  -> leave the room
```

The puzzle deliberately requires information from two channels: the terminal
knows the procedure, while Gorden can observe the physical clue. Neither a
model-generated answer nor an arbitrary password hunt should solve it.

For keyboard play, canonical shell commands are sufficient. Controller play
must offer an equivalent path without requiring free-form text entry, for
example a small command/action picker populated from currently discovered
options. This is a presentation of the same puzzle operations, not a separate
solution or a general command framework.

## Routine and Linked cognition

The player-facing distinction should describe Gorden's capabilities rather
than expose a provider brand. The provisional names are:

| Mode | Behaviour |
|---|---|
| **Routine** | Deterministic local behaviours with a small explicit intent vocabulary. |
| **Linked** | A configured model interprets freer language and may plan or chain validated actions. |
| **Degraded** | A Linked request failed; Gorden continues through Routine rather than becoming unusable. |

Routine should understand enough structured intent to complete the room:
follow, move to a known target, inspect a known target, report, wait and speak
authored responses. The current `ScriptedProvider` is not that mode: it returns
a fixed response queue without interpreting requests. It should remain useful
for tests and demos rather than become the long-term routine controller.

The terminal may expose the mode as a diegetic operation:

```text
$ gorden cognition status
mode: routine
uplink: available

$ gorden cognition link
Neural link established.
```

If no model endpoint is configured, the uplink is simply unavailable and the
game remains in Routine. Credentials and endpoint configuration remain outside
the diegetic filesystem; gameplay may activate an already configured link but
must not expose or manipulate secrets.

The event loop, transcript, memory, observations, tool validation and world
commands remain shared across modes. Only the source of proposed decisions
changes. Both modes must pass through the same simulation-side rules.

A runtime switch should take effect on the next think. Providers or policies
referenced by an in-flight asynchronous completion must remain alive until it
finishes; changing modes must not replace or destroy the active provider under
that work. A failed Linked completion may record its error and schedule a
Routine response without retrying the unavailable model indefinitely.

## Dialogue progression

Conversation should begin as a local interaction and later become a remote
capability.

### Initial state

The player approaches Gorden and interacts to enter the existing conversation
overlay. The generic anywhere-chat shortcut is unavailable or communicates
that no remote link exists yet. Routine offers enough discoverable phrasing or
action choices that the player cannot be blocked by guessing exact sentences.

### Robot-initiated conversation

Gorden may have something to say after authored world events, inspection
results or agent decisions. A spontaneous line appears through the existing
subtitle presentation, accompanied by a non-modal indication that Gorden wants
to talk. The player chooses whether to enter the full dialogue view; speech
must not unexpectedly seize movement or camera input.

Authored story prompts and model-generated speech should enter the same
transcript/subtitle path. The game decides which events create opportunities
to speak, while the active cognition mode decides how an optional response is
formed.

### Remote communication unlock

Completing the room can install or enable a `robot_radio` capability. After
that point, the current anywhere-chat interaction becomes diegetic remote
communication. The same capability can later support facility networking,
cameras or other story unlocks without requiring those systems in the first
slice.

## Token measurement and cognitive progression

The UI should distinguish factual provider usage from gameplay progression.

### Compute used

Record the values already returned in each `ChatResponse`:

- prompt tokens;
- completion tokens;
- total tokens;
- request count;
- session totals and, if persisted, lifetime totals.

Routine operations consume no provider tokens. Unknown or omitted provider
usage must remain unknown rather than be estimated and presented as fact.
Provider/model identity may appear in settings or developer diagnostics while
the gameplay HUD uses the cognition-mode vocabulary.

### Cognitive sync

Raw token consumption should not directly be XP. Doing so rewards verbosity,
replayed context and expensive models rather than useful collaboration, and
token counts are not comparable across all backends.

A separate provisional **Cognitive sync** progression value can instead reward
meaningful firsts and completed outcomes:

- first successful instruction;
- first independently completed task;
- discovery of a relevant clue;
- a jointly completed puzzle;
- a useful stored memory;
- a newly unlocked capability.

Token use may contribute a small capped or diminishing bonus in Linked mode,
but should not be the primary source. Routine must earn equivalent progression
from the same gameplay outcomes so that configuring a paid service does not
accelerate mandatory progression.

Possible later sync unlocks include remote communication, richer perception,
new validated tools, additional memory capacity and longer initiative chains.
Only `robot_radio` is proposed for the first room.

A compact presentation could be:

```text
COGNITIVE SYNC     340 / 500
COMPUTE THIS RUN   12,481 tokens
SESSION REQUESTS   18
MODE               LINKED
```

Exact values, terminology and whether lifetime compute belongs in save data
remain open until the first UI is exercised.

## Minimal gameplay state

Keep the first implementation in Gorden. A small conceptual state is enough:

```text
FirstRoomProgress
  maintenanceTagObserved
  conduitOrderObserved
  interlockVerified
  doorOpen
  robotRadioUnlocked
```

This is illustrative rather than a serialization contract. State transitions
must be made through validated gameplay operations rather than direct VFS
mutation. The terminal command invokes an operation; that operation validates
the current state and changes the world.

Gorden's semantic observation should expose only what it can perceive, such as
the door being locked or opening, a visible inspection target and successful
progression events. Inspecting an entity must return meaningful observable
properties rather than only its coordinates.

Save/load must restore a consistent combination of logical state, collision,
visual door state and unlocked capabilities. Transient interface mode and an
in-flight model request need not be persisted.

## Recommended implementation order

1. Add Gorden-owned first-room progression state and explicit door states.
   Make opening the door change its collision and visible representation.
2. Add validated gameplay operations for interlock verification and door
   opening. Expose thin terminal commands over those operations.
3. Enrich semantic entities and `inspect` results enough for Gorden to observe
   the power-unit clue, door state and relevant world events.
4. Implement an input-aware deterministic Routine policy. Keep the fixed
   `ScriptedProvider` for deterministic tests that need queued responses.
5. Gate initial dialogue by proximity, add non-modal robot-initiated speech,
   and make the existing anywhere-chat path depend on `robot_radio`.
6. Add stable runtime cognition selection with a safe transition around
   in-flight completions and automatic fallback to Routine.
7. Accumulate the existing response usage fields and expose compute totals in
   gameplay and developer status.
8. Add Cognitive sync from gameplay outcomes, awarding equivalent progress in
   Routine and Linked modes.
9. Persist first-room state and the radio capability, then cover the entire
   no-LLM solution with an integration or smoke test.

The first three steps form the smallest puzzle slice. Routine behaviour makes
that slice genuinely completable without a model. Backend switching, telemetry
and progression should follow without delaying the authoritative door loop.

## Open questions to resolve during implementation

- What exact object placement makes Gorden's physical contribution legible
  without feeling artificially inaccessible to the player?
- Should the controller path be a terminal command picker, dialogue choices or
  both?
- Does Routine parse a deliberately small text grammar, consume structured UI
  intents, or combine the two?
- Which component owns cognition-mode selection while preserving the existing
  asynchronous provider lifetime?
- Which spontaneous speech events are authored, and which merely create an
  opportunity for the active policy to respond?
- Is Cognitive sync a robot property, a player/relationship property or a
  capability-progression track?
- Which compute counters are session-only, and which, if any, belong in the
  savegame?
