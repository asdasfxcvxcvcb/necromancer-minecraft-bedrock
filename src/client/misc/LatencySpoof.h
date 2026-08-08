#pragma once
#include <cstdint>

// Raises the latency the *server* measures for us, without slowing anything we
// receive.
//
// RakNet funnels every outgoing datagram through a single socket send, which is
// below the packet layer, so that is the only place the server's RTT estimate can
// actually be influenced. Delaying packet objects higher up (PacketSender) does
// not move it at all.
//
// Only plain data datagrams are held. ACK/NAK datagrams are always passed
// straight through, which is the one thing a generic traffic shaper cannot do:
// clumsy delays ACKs too, so past ~200ms the server's reliable-send window
// stalls, the world freezes, and the link eventually times out. Letting ACKs run
// at full speed keeps the connection healthy while our data still arrives late.
namespace LatencySpoof {
    // Installs the socket-send hook. Safe to call once hooks are initialised.
    void init();
    void shutdown();

    // 0 disables the delay entirely (hook stays installed but passes everything).
    void setLatency(uint32_t ms);
    uint32_t getLatency();

    // Send the next few datagrams immediately, ignoring the configured latency.
    //
    // At the socket layer a datagram is opaque (encrypted + compressed), so we
    // cannot look inside it to find an attack. Instead the packet layer, which does
    // know, calls this just before the attack goes out; the next datagram to reach
    // the socket is the one carrying it.
    //
    // `count` exists because RakNet may split one packet across datagrams. Anything
    // still un-consumed is dropped after a short window so a mispredicted bypass
    // cannot leak into unrelated traffic.
    void requestBypass(int count = 1);

    // Hold *incoming* datagrams for `ms` before the game processes them, which is
    // what makes remote players appear frozen: their position updates stop arriving
    // even though the server is still sending them.
    //
    // This is deliberately separate from the outbound delay. A traffic shaper slows
    // both directions at once, which also starves the server of ACKs -- its resend
    // queue then grows until the link is declared dead. Here ACKs still leave at
    // full speed, so the connection stays healthy while the view goes stale.
    void setInboundDelay(uint32_t ms);
    uint32_t getInboundDelay();

    // Diagnostics. This feature fails silently by nature -- every failure path
    // just forwards the datagram -- so these exist to tell "working" apart from
    // "doing nothing" without guessing.
    bool hooked();
    uint64_t heldCount();
    uint64_t passedCount();
}
