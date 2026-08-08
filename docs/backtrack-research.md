# Backtrack Feasibility Research — Minecraft.Windows.exe 1.26.30.5

Status: COMPLETE. Verified in IDA against
`D:\Games\MINECRAFT BEDROCK\Versions\Minecraft-1.26.30.5\Minecraft.Windows.exe.i64`.
All addresses absolute for imagebase 0x140000000. Convert to pattern sigs before use
(vtables are data — reach them through your existing `MinecraftPackets::createPacket`
hook instead of sig-scanning data).

---

# BUCKET A — what lives in the client binary

## A1. RakPeer::GetAveragePing (you already hook this, vtable idx 0x27)

- Computes ping **live**: loops up to 5 `uint16` history samples in RakNet's
  per-remote-system record and averages them (divides by sample count).
- Data read: remote-system array at `RakPeer+0x248` (584), record stride 9880 bytes.
  RakNet fills the 5 samples from its own reliability-layer ACK timing.
- Call sites: only two data xrefs — RakPeer vtable slots `0x14dcba538` and
  `0x14e454fa8`. Called purely virtually (matches vtable idx 0x27).
- **VERDICT: cosmetic only.** Overriding the return changes what the client displays
  (HUD/scoreboard). The server never asks the client for this value — it measures
  client latency itself at the RakNet layer. Useless for backtrack except as a
  visual lie.

## A2. NetworkStackLatency packet (0x73 / 115)

- Vtable: `0x14dd10b30`
  - `+0x08` getId thunk `0x14288b940` (returns 115)
  - `+0x10` getName `0x14288b950`
  - `+0x28` write `0x14288b970`
  - `+0x30` read `0x14288c000`
- Struct layout (packet data starts at +0x30 like every packet):
  - `+0x30` int64 "Creation Time" timestamp — **nanoseconds from local QPC clock**
  - `+0x38` bool needsResponse
  - `+0x40` dword (secondary field, copied by clone path)
- Embedded schema string says the packet is **DEPRECATED** ("Was for
  testing/debug/telemetry... Sent from both client & server").
- Construction: `0x14143FF00` (createPacket factory path) and shared create-helper
  `0x142914420` — both stamp Creation Time from `QueryPerformanceCounter` scaled to
  nanoseconds at construction time.
- **Receive behavior (confirmed prior session):** when the client gets this packet
  with needsResponse==1, it builds a reply and copies the incoming timestamp
  **verbatim** — no adjustment. It is a pure echo. The server computes
  RTT = now - timestamp when the echo comes back.
- **What this means for fake latency:** the only client-side lever is *delaying the
  echo reply* (hold the outbound packet for N ms). That inflates the RTT the server
  computes **from this packet**. You cannot meaningfully forge the timestamp itself
  to inflate ping (echo is verbatim; server compares against its own clock).
- **Caveat that kills half the value:** RakNet's own connected-ping (below the game
  packet layer) keeps reporting your true RTT to the server independently. A server
  that uses RakNet ping (most do — it's what shows in the player list) is not fooled
  by a delayed 0x73 echo. Only a server whose lag-comp specifically consumes the
  NetworkStackLatency RTT would grant you extra rewind. Also note DEPRECATED status —
  current server software may not send/consume it at all. Verify empirically per
  target server.

## A3. PlayerAuthInput packet (0x90 / 144) — the tick carrier

- Vtable: getName slot at `0x14dd10f30`.
- Struct layout:
  - `+0x88`/`+0x90` 128-bit InputData bitset (input flags)
  - `+0x98` Input Mode (int)
  - `+0x9C` Play Mode (int)
  - `+0xA0` New Interaction Model (int)
  - `+0xB0` ptr -> ItemUseTransaction (attacks embed HERE in server-auth mode)
  - `+0xB8` ptr -> ItemStackRequest
  - `+0x168` uint64 **mTick ("Input tick")** — unsigned varint on wire, monotonic
    per session, filled by the client-input ECS path
    (`ClientInputUpdateSystemInternal::tickUpdateClientInput` — you already hook it).
- **ANTICHEAT-CRITICAL:** mTick must stay non-decreasing and roughly aligned with
  real time. If you delay an attack by N ticks, the attack rides a PlayerAuthInput
  whose mTick is *current*, while the transaction's claimed positions describe the
  past — see A6 for why the server cross-checks exactly that.

## A4. Attack packet routing — how a hit actually leaves the client

Three paths exist in the binary; which one fires depends on server authority mode:

1. **Server-auth (current default on BDS/Realms/most servers):**
   attack is an `ItemUseOnActorInventoryTransaction` embedded in PlayerAuthInput
   at `+0xB0`. No separate packet. The only tick field in play is mTick (A3).
2. **Legacy transaction:** `InventoryTransactionPacket` (0x1E / 30) carrying the
   same `ItemUseOnActorInventoryTransaction`. getId thunk returns 30. No tick field
   in this packet — transaction data only.
3. **InteractPacket (0x21 / 33):** vtable `0x14dd0c5c0` (getId returns 33,
   getName thunk `0x14355AC60`). Payload starts at `+0x30` (confirmed from its
   serialize wrapper `0x14355ac80` passing `a1+48` as payload base). Protocol
   layout: action byte, targetRuntimeID (varint64), optional Vec3 position.
   Attack = action 2. **No tick/timestamp field at all** — this is the simplest
   packet to delay; nothing inside says *when* it happened.

## A5. ItemUseOnActorInventoryTransaction — full layout + server-side validation

Layout recovered from its `handle` validation function `0x1424445E0`
(this is the server-authoritative validation code compiled into the client — the
same logic BDS runs):

- `+0x68` uint64 target runtime ID
- `+0x70` uint32 actionType (**0 = Interact, 1 = Attack** — confirmed by `cmp` against 1)
- `+0x74` int32 hotbar slot (checked against player's actual selected slot)
- `+0x78` held ItemStack (network descriptor; checked against actual held item)
- `+0x98` int32 slot/predicted field
- `+0xA8` bool flag
- `+0xD8` Vec3 **playerPos** ("from pos" — your position when attacking)
- `+0xE4` Vec3 clickPos (expected next per protocol; not directly observed — verify
  if you touch it)

What the server-side validation checks, in order (all return
InventoryTransactionError on failure — error 7 = StateMismatch, 3 = SourceItemMismatch,
2 = BalanceMismatch):

1. Player alive.
2. Target runtime ID resolves to a live entity **in the server's current level**.
3. Held item matches the player's real held item (`playerSuppliesSlot`,
   `selectedSlot` consistency).
4. **playerDistanceToFromPos**: distance between the player's *actual current
   position* and the transaction's claimed playerPos (+0xD8) must be **<= 6.0**.
5. **playerDistanceToEntity**: distance from the player's actual position to the
   target's **current server-side position** must be <= `maxPickRange`
   (read from player attributes, `a2[340]`).
6. **View-direction raycast**: ray from the player's eye along their actual view
   direction, length `maxPickRange + 1.0`, must intersect the target's **current
   AABB** (merged from its shape components); plus a dot-product check between
   attack direction and view direction (`playerViewDirection` / `attackDirection`).

**This is the single most important finding for backtrack:** vanilla-style
validation hit-tests against the target's **CURRENT** position and AABB — not a
rewound one. The "lag compensation" a backtrack cheat needs is NOT in this code
path. If the target server runs this stock validation, aiming at a past position
just misses (raycast fails or distance check fails). Any rewind window would have
to come from the server software itself (Bucket B).

## A6. Incoming entity-position packets (for buffering past positions / the box)

- **MoveActorDelta (0x6F / 111) — drives remote-player positions.** Vtable getName
  at `0x14dd10900`, getId at `0x14dd108f8` (`0x142872280`), write `0x1428722b0`:
  - `+0x30` uint64 runtime ID
  - `+0x38` uint16 header flags (bit0=posX, bit1=posY, bit2=posZ, bit3=rotX,
    bit4=rotY, bit5=onGround/teleport)
  - `+0x40` float posX/posY/posZ
  - `+0x48` byte rotX, `+0x49` byte rotY
- **MovePlayer (0x13)** — `onMovePlayerPacketNormal` applies to the LOCAL player
  only (server self-correction). Not for tracking enemies.
- **MoveActorAbsolute (0x12)** — same +0x30 runtimeID convention; used for
  teleports/first-spawn snaps.
- **SetActorMotion (0x28)** — `+0x30` runtimeID, velocity only.
- Practical note: deltas are relative. For a position history buffer, hook the
  apply step (or read the resolved live Actor position each tick after deltas are
  applied) rather than summing deltas yourself.

## A7. Packet base layout (universal)

`+0x00` vtable; header fields through ~`+0x28`; packet data from `+0x30`.
Consistent across every packet above.

---

# BUCKET B — what is NOT in the client binary (do not fabricate)

- **Whether the target server does lag-compensation rewind at all, and the window
  size.** The vanilla validation in this binary (A5) hit-tests the CURRENT
  position — there is no rewind in stock code. Any rewind window is a
  server-software/plugin feature. Determine empirically per server or read its
  source:
  - PocketMine-MP: no melee lag-comp by default (check plugins).
  - Nukkit/NukkitX: no melee lag-comp by default.
  - BDS/Realms: stock validation as documented in A5.
  - **CubeCraft: CUSTOM — non-stock behavior confirmed. See FIELD REPORT below.**
  - Protocol refs: mojang/bedrock-protocol, PrismarineJS/bedrock-protocol,
    gophertunnel. Community threads: UnknownCheats Bedrock section.
- **Do not guess a window number.** Test against the actual target server.

---

# FIELD REPORT — CubeCraft (user-observed, 2026-07-27)

**Observation:** player with ~300ms real ping landing melee hits at ~9 blocks
of current-position separation. "9 blocks" is an eyeballed estimate — treat as
approximate, but clearly far beyond vanilla reach (3.0) and beyond every stock
tolerance found in the binary.

**IDA re-check verdict (definitive):** stock Bedrock has NO mechanic that
produces this. Every stock validation tolerance is spatial and fixed, none
scale with ping, none rewind:
- entity attack: distance-to-entity <= maxPickRange, fromPos within 6.0,
  view raycast maxPickRange+1.0 (A5, `0x1424445E0`)
- block use: distance-to-block <= maxPickRange + **0.5** "wiggle"
  (`ItemUseInventoryTransaction::handle` @ `0x14243F650`)
=> CubeCraft's custom server software is responsible. Three candidate
explanations, in order of likelihood:

1. **CubeCraft runs lag-compensation rewind keyed to attacker latency.**
   The friend aimed at his 300ms-stale render of the target; the server
   rewound the target ~300ms; hit landed. This is the backtrack mechanic
   working NATURALLY off real ping — validates Modules 1-3 for this server.
2. **CubeCraft scales reach tolerance by measured ping** (no rewind, just a
   bigger allowed hit distance for laggy players). Same observable result.
3. **The friend was running a reach cheat** and CubeCraft's anticheat is
   slow/lenient. Keep on the table — do not build on an unverified anecdote.
   The probe below distinguishes all three.

**Exploit avenues for CubeCraft (in probe order):**

A. **Low-ping reach probe (cheapest, do first).** At normal ping, attempt
   hits at 3.5 / 4 / 5 blocks of current separation (needs code-generated
   attacks — the vanilla client can't click-target past its ~3 block
   crosshair pick; Module 3's send path bypasses this because the server
   does its own raycast). If far hits land at LOW ping -> tolerance is
   ping-independent -> skip backtrack entirely, it's just soft reach.

B. **Backtrack proper (Modules 1-3).** Aim at the stale box, delay attack
   packets by the configured window. If hits land on stale positions ->
   rewind confirmed (hypothesis 1). Tune the delay to find the window edge.

C. **Selective attack-delay as fake-latency.** Hold ONLY attack packets
   ~200-300ms; movement/ticks untouched (monotonicity safe). If CubeCraft
   grants leniency per-PACKET-lateness, you get 300-ping treatment with
   crisp movement. If it keys off connection RTT (RakNet-measured), this
   does nothing — that's the discriminator.

D. **Full-path induced latency (control case, guaranteed to replicate the
   friend).** A traffic shaper (clumsy etc.) adding real 300ms to ALL
   packets. This IS real lag: RakNet reports it, ticks stay monotonic
   (uniform delay reorders nothing), exactly reproduces the friend's
   conditions. If even THIS doesn't produce far hits for you, hypothesis 3
   (friend had reach) is the answer.

**Client-side note:** vanilla click-to-attack is gated by a crosshair
raycast at ~3 blocks, but Module 3 generates attacks from code with an
explicit target runtimeID, so the client pick is not a gate. The only
aim-dependent check is the SERVER's view-direction raycast — which, on a
rewind/lenient server, is exactly what aiming at the backtrack box satisfies.

---

# FEASIBILITY VERDICT

**(a) Pure client-side attack-delay: mechanically implementable, effect
server-dependent.** You can buffer enemy positions from MoveActorDelta (A6) and
draw the past-position box 100% client-side, and you can delay/timestamp attacks
client-side. BUT: against a server running stock validation (A5), the hit
raycast runs against the target's CURRENT position — delaying your attack does
not make the server accept hits on stale positions. It only helps if the server
software itself rewinds (Bucket B). So: build the box and the delay machinery,
but expect it to matter only on servers with real lag-comp.

**(b) Fake latency via NetworkStackLatency/ping manipulation: weak.** The 0x73
echo is verbatim — your only lever is delaying the reply, which inflates only the
game-layer RTT from that packet. RakNet-layer ping (what servers actually use,
and what GetAveragePing mirrors client-side) still reports your true latency.
GetAveragePing spoofing is pure HUD cosmetics. Verdict: not a reliable vector.

**(c) Not feasible:** forging timestamps/ticks. mTick must stay monotonic (A3),
and the 0x73 timestamp is server-clocked.

**Bottom line:** viable as client machinery (position buffer + visual box +
attack timing), but the *payoff* hinges entirely on the target server's rewind
behavior, which you must probe per-server. On BDS/Realms-style stock validation,
backtrack does nothing — the server hit-tests the live position.

# ANTICHEAT TRIPWIRES (ranked)

1. **CRITICAL — mTick monotonicity** (PlayerAuthInput +0x168): must be
   non-decreasing and time-aligned. Replaying/holding inputs desyncs it.
2. **CRITICAL — fromPos consistency** (A5 check 4): claimed playerPos in the
   attack transaction must be within 6.0 of where the server thinks you are NOW.
   A delayed attack carrying an old fromPos fails instantly.
3. **CRITICAL — distance-to-entity** (A5 check 5): measured to the target's
   CURRENT position, max `maxPickRange`. Attacking a past position that's since
   moved away fails.
4. **HIGH — view raycast** (A5 check 6): your real current view direction must
   still hit the target's CURRENT AABB.
5. **MEDIUM — slot/item match** (A5 check 3): hotbar slot and held item in the
   transaction must match server state at processing time.
6. **LOW — 0x73 echo delay:** no client-side validation, but inconsistent RTT vs
   RakNet ping is trivially observable server-side.

# HOOK POINTS (you already have sigs for most)

| What | How |
|---|---|
| Enemy position stream | createPacket hook (have) -> filter ID 111, read +0x30/+0x40; or read resolved Actor pos each tick |
| Attack send (all 3 paths) | createPacket hook -> IDs 144/30/33; or `Actor::attack` (have sig) |
| Tick on outgoing input | PlayerAuthInput obj +0x168 at send time |
| Ping display | RakPeer::GetAveragePing (have) — cosmetic only |
| 0x73 echo timing | intercept outbound NetworkStackLatency send (createPacket/send path), delay reply |

# SIGNATURES (1.26.30.5, generated + user-confirmed)

## Functions (direct sigs, Addresses.h format — `return res;`)

NetworkStackLatencyPacket::write @ 0x14288B970:
41 57 41 56 56 57 53 48 83 EC ? 4D 89 CE 4C 89 C3 48 89 D6 48 89 CF 48 8B 01 48 8B 40 ? FF 15 ? ? ? ? 41 89 C7 49 0F BA E6 ? 45 0F 42 FE 4C 8D 77 ? 48 8B 07 48 8B 40 ? 48 89 F9 FF 15 ? ? ? ? 41 8D 47 ? 83 F8 ? 72 ? 41 83 FF ? 75 ? 48 89 D9 4C 89 F2 49 89 F0 48 83 C4 ? 5B 5F 5E 41 5E 41 5F E9 ? ? ? ? 48 8B 57 ? 48 8B 06 48 8B 40 ? 4C 8D 05 ? ? ? ? 48 89 F1 45 31 C9 FF 15 ? ? ? ? 0F B6 57 ? 48 8B 06 48 8B 40 ? 4C 8B 15

NetworkStackLatencyPacket::read @ 0x14288C000:
55 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 75 ? 48 C7 45 ? ? ? ? ? 48 89 D6 48 89 CF 48 8D 4D ? 0F 57 F6 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? C7 45 ? ? ? ? ? BA ? ? ? ? E8

NetworkStackLatencyPacket factory ctor (QPC stamp) @ 0x14143FF00:
56 57 53 48 83 EC ? 48 89 CE 8B 05 ? ? ? ? 8B 0D ? ? ? ? 65 48 8B 14 25 ? ? ? ? 48 8B 0C CA 3B 81 ? ? ? ? 0F 8F ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 01 48 8B 40 ? BA ? ? ? ? FF 15 ? ? ? ? 48 89 C7 48 85 C0 75 ? 48 8D 05 ? ? ? ? 48 89 44 24 ? 48 C7 44 24 ? ? ? ? ? 48 8D 0D ? ? ? ? 48 8D 15 ? ? ? ? 4C 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ? 48 B8 ? ? ? ? ? ? ? ? 48 89 47 ? 48 8D 05 ? ? ? ? 48 89 07 E8

NetworkStackLatencyPacket shared create helper @ 0x142914420:
56 57 53 48 83 EC ? 48 89 CE 48 8B 41 ? 48 85 C0 74 ? 48 89 F1

MoveActorDeltaPacket::write @ 0x1428722B0:
56 57 48 83 EC ? 48 89 D6 48 89 CF 48 8B 51 ? 48 8B 06 48 8B 40 ? 4C 8D 05 ? ? ? ? 48 89 F1 45 31 C9 FF 15 ? ? ? ? 0F B7 57

ItemUseOnActorInventoryTransaction::handle (server validation) @ 0x1424445E0:
55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 85 ? ? ? ? 0F 29 BD ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 45 89 C7

NetworkStackLatencyPacket getId thunk @ 0x14288B940 (includes next-fn bytes for uniqueness — fragile):
B8 ? ? ? ? C3 CC CC CC CC CC CC CC CC CC CC 48 89 D0 48 8D 0D ? ? ? ? 48 89 0A 48 C7 42 ? ? ? ? ? C3

MoveActorDeltaPacket getId thunk @ 0x142872280 (same caveat):
B8 ? ? ? ? C3 CC CC CC CC CC CC CC CC CC CC 48 89 D0 48 8D 0D ? ? ? ? 48 89 0A 48 C7 42 ? ? ? ? ? C3 CC CC CC CC CC CC CC CC CC CC 56 57 48 83 EC

NOT SIGGABLE (skip, not needed — reach via vtables + createPacket hook):
0x14355AC80 (Interact serialize wrapper), 0x1424AADC0 (InvTransaction getName),
0x14355AC50 (Interact getId), 0x14355AC60 (Interact getName).
Reason: tiny thunks byte-identical to hundreds of others; no unique sig exists.

## Vtables (sig the LEA instruction, resolve with deref(3))

| Vtable | Value | Sig anchor | Resolver |
|---|---|---|---|
| NetworkStackLatencyPacket | 0x14DD10B30 | 0x141440093 (lea rcx, vtable) | deref(3) |
| PlayerAuthInputPacket | 0x14DD10F20 | 0x141439CEE (lea rcx, vtable) | deref(3) |
| MoveActorDeltaPacket | 0x14DD108F0 | 0x1414368DE (lea rcx, vtable) | deref(3) |
| InteractPacket | 0x14DD0C5C0 | 0x1414380DE (lea rcx, vtable) | deref(3) |

NetworkStackLatencyPacket vtable (anchor 0x141440093):
48 8D 0D ? ? ? ? 48 89 4F ? 48 89 57

PlayerAuthInputPacket vtable (anchor 0x141439CEE):
48 8D 0D ? ? ? ? 48 89 4F ? 0F 11 47 ? 0F 11 47 ? 0F 11 47 ? 0F 11 47 ? 0F 11 87 ? ? ? ? 0F 11 87 ? ? ? ? 0F 11 87

MoveActorDeltaPacket vtable (anchor 0x1414368DE):
48 8D 0D ? ? ? ? 48 89 4F ? 48 C7 47 ? ? ? ? ? 66 C7 47 ? ? ? 48 C7 47

InteractPacket vtable (anchor 0x1414380DE — FRAGILE, spans into next fn):
48 8D 0D ? ? ? ? 48 89 4F ? 0F 11 47 ? 0F 11 47 ? C7 47 ? ? ? ? ? 48 8D 0D ? ? ? ? 48 89 4F ? 48 89 06 48 89 7E ? 48 89 F0 48 83 C4 ? 5F 5E C3 48 8D 0D ? ? ? ? E8 ? ? ? ? 83 3D ? ? ? ? ? 0F 85 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? E9 ? ? ? ? CC CC 56 57 48 83 EC ? 48 89 CE 8B 05 ? ? ? ? 8B 0D ? ? ? ? 65 48 8B 14 25 ? ? ? ? 48 8B 0C CA 3B 81 ? ? ? ? 0F 8F ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 01 48 8B 40 ? BA ? ? ? ? FF 15 ? ? ? ? 48 89 C7 48 85 C0 75 ? 48 8D 05 ? ? ? ? 48 89 44 24 ? 48 C7 44 24 ? ? ? ? ? 48 8D 0D ? ? ? ? 48 8D 15 ? ? ? ? 4C 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ? 48 B8 ? ? ? ? ? ? ? ? 48 89 47 ? 48 8D 05 ? ? ? ? 48 89 07 48 89 F8 48 83 C0 ? 48 B9 ? ? ? ? ? ? ? ? 48 89 4F ? 66 C7 47 ? ? ? 0F 57 C0 0F 11 47 ? C7 47 ? ? ? ? ? 48 8D 0D ? ? ? ? 48 89 4F ? 0F 11 47 ? 0F 11 47 ? C7 47

# READY-TO-PASTE Addresses.h ENTRIES

```cpp
inline static SigImpl NetworkStackLatencyPacket_write {
    [](memory::signature_store&, uintptr_t res) { return res; },
    "41 57 41 56 56 57 53 48 83 EC ? 4D 89 CE 4C 89 C3 48 89 D6 48 89 CF 48 8B 01 48 8B 40 ? FF 15 ? ? ? ? 41 89 C7 49 0F BA E6 ? 45 0F 42 FE 4C 8D 77 ? 48 8B 07 48 8B 40 ? 48 89 F9 FF 15 ? ? ? ? 41 8D 47 ? 83 F8 ? 72 ? 41 83 FF ? 75 ? 48 89 D9 4C 89 F2 49 89 F0 48 83 C4 ? 5B 5F 5E 41 5E 41 5F E9 ? ? ? ? 48 8B 57 ? 48 8B 06 48 8B 40 ? 4C 8D 05 ? ? ? ? 48 89 F1 45 31 C9 FF 15 ? ? ? ? 0F B6 57 ? 48 8B 06 48 8B 40 ? 4C 8B 15"_sig,
    "NetworkStackLatencyPacket::write"
};

inline static SigImpl NetworkStackLatencyPacket_read {
    [](memory::signature_store&, uintptr_t res) { return res; },
    "55 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 75 ? 48 C7 45 ? ? ? ? ? 48 89 D6 48 89 CF 48 8D 4D ? 0F 57 F6 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? 0F 29 75 ? C7 45 ? ? ? ? ? BA ? ? ? ? E8"_sig,
    "NetworkStackLatencyPacket::read"
};

inline static SigImpl MoveActorDeltaPacket_write {
    [](memory::signature_store&, uintptr_t res) { return res; },
    "56 57 48 83 EC ? 48 89 D6 48 89 CF 48 8B 51 ? 48 8B 06 48 8B 40 ? 4C 8D 05 ? ? ? ? 48 89 F1 45 31 C9 FF 15 ? ? ? ? 0F B7 57"_sig,
    "MoveActorDeltaPacket::write"
};

inline static SigImpl ItemUseOnActorInventoryTransaction_handle {
    [](memory::signature_store&, uintptr_t res) { return res; },
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 85 ? ? ? ? 0F 29 BD ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 45 89 C7"_sig,
    "ItemUseOnActorInventoryTransaction::handle"
};

struct BacktrackVtable {
    inline static SigImpl NetworkStackLatencyPacket {
        [](memory::signature_store& store, uintptr_t) { return store.deref(3); },
        "48 8D 0D ? ? ? ? 48 89 4F ? 48 89 57"_sig,
        "const NetworkStackLatencyPacket::`vftable'"
    };
    inline static SigImpl PlayerAuthInputPacket {
        [](memory::signature_store& store, uintptr_t) { return store.deref(3); },
        "48 8D 0D ? ? ? ? 48 89 4F ? 0F 11 47 ? 0F 11 47 ? 0F 11 47 ? 0F 11 47 ? 0F 11 87 ? ? ? ? 0F 11 87 ? ? ? ? 0F 11 87"_sig,
        "const PlayerAuthInputPacket::`vftable'"
    };
    inline static SigImpl MoveActorDeltaPacket {
        [](memory::signature_store& store, uintptr_t) { return store.deref(3); },
        "48 8D 0D ? ? ? ? 48 89 4F ? 48 C7 47 ? ? ? ? ? 66 C7 47 ? ? ? 48 C7 47"_sig,
        "const MoveActorDeltaPacket::`vftable'"
    };
    inline static SigImpl InteractPacket {
        [](memory::signature_store& store, uintptr_t) { return store.deref(3); },
        "48 8D 0D ? ? ? ? 48 89 4F ? 0F 11 47 ? 0F 11 47 ? C7 47 ? ? ? ? ? 48 8D 0D ? ? ? ? 48 89 4F ? 48 89 06 48 89 7E ? 48 89 F0 48 83 C4 ? 5F 5E C3"_sig,
        "const InteractPacket::`vftable'"
    };
};
```

NOTE on InteractPacket vtable sig: trimmed at the first function boundary (ret + next
LEA). If it fails uniqueness on your build, extend with more bytes from the raw sig
above (full version archived in the Vtables section).

# RAW ADDRESSES (reference)

| Item | Address |
|---|---|
| NetworkStackLatency vtable | 0x14DD10B30 |
| NSL write (Creation Time) | 0x14288B970 |
| NSL read | 0x14288C000 |
| NSL factory ctor (QPC stamp) | 0x14143FF00 |
| NSL shared create helper | 0x142914420 |
| PlayerAuthInput vtable | 0x14DD10F20 (getName slot 0x14DD10F30) |
| MoveActorDelta vtable | 0x14DD108F0 (getName slot 0x14DD10900) |
| MoveActorDelta write | 0x1428722B0 |
| InteractPacket vtable | 0x14DD0C5C0 |
| InteractPacket serialize wrapper | 0x14355AC80 |
| InventoryTransaction getName thunk | 0x1424AADC0 |
| ItemUseOnActorInventoryTransaction::handle (server-side validation) | 0x1424445E0 |
| RakPeer remote-system array offset | RakPeer+0x248, stride 9880 |

---

# IMPLEMENTATION BRIEF (for the AI building the feature)

Read the FEASIBILITY VERDICT and ANTICHEAT TRIPWIRES sections above FIRST.
Do not skip them — they define what is safe to touch and what will get the user
kicked. Then build these three independent modules. Each works standalone;
do not couple them.

## Module 1 — Position history buffer (the core, 100% client-side)

Purpose: keep the last N ms of every remote player's position so the box and
the (optional) attack timing have data.

- Do NOT hook MoveActorDelta and sum deltas — deltas are relative and you will
  drift/desync on teleports. Instead, once per client tick (hook you already
  have: `ClientInputUpdateSystemInternal::tickUpdateClientInput`, or the game
  tick), iterate remote players and record their RESOLVED live position.
- Store per runtimeID a ring buffer of { clientTimeMs, vec3 pos, float yaw,
  float pitch }. Capacity: enough for your max configurable delay + margin
  (e.g. 1000 ms at 20 tps = 20 entries minimum; use 64 and be done).
- Timestamp entries with the client's own steady clock, not game ticks — tick
  rate stutters, wall time doesn't.
- If the client already exposes an entity list for ESP/target HUD, reuse it.
  If you want the packet-level source instead: hook `MinecraftPackets::createPacket`
  (sig exists), filter ID 111 (MoveActorDelta), read runtimeID at +0x30 and
  position floats at +0x40 AFTER the packet's read() has run — but the
  resolved-actor approach above is simpler and robust. Prefer it.

## Module 2 — Backtrack box render (the visual)

Purpose: draw a hitbox where the target WAS ~delayMs ago, so the user can aim
at it.

- In your render hook (sigs exist: `ActorRenderDispatcher::render`,
  `LevelRendererPlayer::renderOutlineSelection`, `Tessellator_begin/vertex/color`,
  `MeshHelpers_renderMeshImmediately`), for each tracked remote player:
  - Look up the buffered entry closest to (now - configuredDelayMs).
  - Draw a wireframe box at that stored position with the target's current
    AABB dimensions (height/width are stable; position is what moves).
- Color-code freshness: green if the sample age is within tolerance of the
  configured delay, red if the buffer had no sample that old (target too new
  or buffer too short) — the user needs to know when the box is a lie.
- This module is pure client-side rendering. Zero anticheat risk. It works on
  EVERY server regardless of lag comp, because it only visualizes.

## Module 3 — Attack timing (the actual "backtrack" effect)

Purpose: delay outgoing attacks by configuredDelayMs so that IF the server
runs lag-compensation rewind, hits land on the stale position shown by the box.

- Intercept attacks at the send layer. All three routing paths converge in
  `MinecraftPackets::createPacket` (sig exists) or can be caught per-ID:
  - ID 144 PlayerAuthInput (server-auth; attack = ItemUseOnActor txn at +0xB0)
  - ID 30 InventoryTransactionPacket (legacy; same txn type)
  - ID 33 InteractPacket (action 2 = Attack, target runtimeID at +0x38)
- Queue the packet, release after configuredDelayMs. Delay ONLY attack
  packets — delaying movement or everything is how you get flagged (see
  tripwires).
- HARD RULES for this module (violating these = instant validation failure
  on stock servers, per A5):
  - NEVER modify mTick (PlayerAuthInput +0x168). It must stay monotonic and
    time-aligned. You are delaying the SEND, not editing the packet.
  - NEVER edit the transaction's playerPos (+0xD8) or clickPos. The server
    rejects if claimed fromPos is >6.0 from where it thinks you are.
  - Expect this module to do NOTHING on BDS/Realms/stock servers: their
    validation raycasts the target's CURRENT position (A5 checks 5-6).
    It only pays off on servers with real lag-comp rewind (Bucket B).
- Ship Module 3 DISABLED by default behind a config flag, with a per-server
  toggle. See "Probing a server" below.

## What NOT to build (researched, dead ends)

- GetAveragePing spoof: cosmetic only, server never reads it. (A1)
- NetworkStackLatency timestamp forgery: echo is verbatim; nothing to forge.
  Delaying the 0x73 reply inflates only the game-layer RTT while RakNet ping
  still reports true latency — trivially detectable, near-zero payoff. (A2)
- Replaying old PlayerAuthInput packets: mTick monotonicity breaks. (A3)

## Probing a server for lag comp (empirical, required per target server)

The rewind window is NOT in the client binary (Bucket B). To test a server:
1. Enable Module 1 + 2, set delay to ~100ms, fight a moving player aiming at
   the BACKTRACK box (not the live player).
2. If hits register consistently while aiming at the stale box and NOT the
   live model -> server rewinds at least 100ms. Increase delay and repeat to
   find the window edge (hits stop registering = past the window).
3. If hits only register when aiming at the live model -> stock validation,
   no lag comp. Leave Module 3 off for that server.
4. Check server software first when possible: PocketMine/Nukkit = no melee
   lag-comp by default (plugins may add it); BDS/Realms = stock (A5).

## Acceptance criteria

- Box tracks a configured-delay-old position smoothly for every remote player.
- No modification of mTick, playerPos, clickPos, or ping values anywhere.
- Module 3 per-server toggle with default OFF.
- No per-frame allocations in the render path; ring buffer reused.

---

# UI / SETTINGS SPEC (menu layout for the feature)

Design rule: the user-facing knobs must map to the mechanics in the
IMPLEMENTATION BRIEF. Do not expose knobs that can be set to self-defeating
combinations.

```
Backtrack
├── Enabled                    master toggle
├── Mode                       [Visual only] / [Visual + Attack delay]
│                              default: Visual only (Module 1+2, zero risk)
├── Backtrack Time             0-500 ms, ONE slider driving BOTH the box
│                              sample age AND the attack delay. These must
│                              always match: the server rewinds by how late
│                              the packet is, so the box must show that exact
│                              age. Never expose them as separate sliders.
├── Jitter                     0-50 ms, randomizes each attack delay +/-.
│                              Exact-integer delays every hit are a
│                              fingerprint; jitter looks human.
├── Per-Server Profile         auto-save Mode + Backtrack Time per server.
│                              Feature is dead weight on stock servers and
│                              valuable on lag-comp ones; auto-switch.
│
├── Lag Record Hitbox          on/off (Module 2 box)
│   ├── Color                  color picker + opacity (NOT an on/off)
│   ├── Style                  [Outline] / [Filled] / [Both]
│   ├── Through Walls          on/off
│   └── Freshness Fade         on/off. Green tint = buffer has a sample at
│                              the configured delay; red tint = no sample
│                              that old (target too new / buffer too short)
│                              = box is unreliable, don't swing.
│
└── Position Trail             on/off, renders last ~1s of buffered movement
                               as a line, not just the single stale box.
```

Hard rules for the implementer (not menu items):
- Attack delay applies to ATTACK PACKETS ONLY (IDs 144/30/33 attack paths).
  Never expose "delay everything" — movement delay breaks mTick monotonicity.
- Internal position buffer capacity: >= 1000 ms per tracked player regardless
  of slider cap.
- 500 ms slider cap is deliberate; real lag-comp windows are ~100-500 ms.
