# NOTES

Design: the sender piggybacks each frame with a copy of the previous frame's
payload (skipping the redundant copy on 1 of every 16 frames to keep total
bandwidth under the 2.0x budget), and the receiver holds incoming frames in a
ring-buffer jitter buffer, recovering a lost frame from the redundant copy
carried by the next packet whenever possible. Playout runs on a fixed 20ms
wall-clock schedule anchored at T0 + delay_ms, so a frame is emitted (or
counted as a miss) exactly at its deadline regardless of when — or whether —
it arrived. This trades bandwidth for latency: recovering a lost frame costs
zero extra time (no retransmit round trip), only the one extra network
transit the redundant copy already took.

Grade at delay_ms = TODO — this is the lowest value that stayed valid
(miss% <= 1%, overhead <= 2.0x) across repeated runs on both profiles A and B.

What breaks it: two consecutive frames lost in the same burst, since the
second frame's own redundant backup (carried in the packet after it) also
depends on that packet arriving; deep, sustained bursts beyond that will
push miss% over 1% regardless of delay_ms.
