/*
NETWORK_RULES.H

The distributed netcode's rules that need no game to decide: what a client
does with the host's word on its own player standing on an elevator, and
(below it in later network versions) the bytes of the messages that carry a
rule. network_distributed.c asks, the unit tests (port/linux/tests/
network_rules_test.c) check the answers byte for byte. Nothing here has a
dependency but the C library.
*/

#ifndef __NETWORK_RULES_H
#define __NETWORK_RULES_H

#include <stdint.h>

/* ---------- constants */

/* how far (world units) the host's word on a client's own player may be
from where the client had it before the client moves it there */
#define NETWORK_RULES_LOCAL_CORRECTION_TOLERANCE 3.0f
/* how much of the height of that error a client discards while its player
stands on the same elevator as when it was recorded: the platform's travel
over a round trip */
#define NETWORK_RULES_ELEVATOR_PHASE_TOLERANCE 3.0e38f
/* the ticks a biped's elevator counter is set to on each tick that finds it
on an elevator (bipeds.c) */
#define NETWORK_RULES_ELEVATOR_CONTACT_TICKS 60

/* ---------- prototypes/NETWORK_RULES.C */

/* whether a biped is in floor contact with its elevator: not in the air,
with a support surface, and its elevator counter just set (an elevator it
remembers after a jump or a step off is not contact) */
int network_rules_floor_contact(int airborne, int has_support_surface, int elevator_ticks);

/* the host's word on a client's own player against the client's record of
the same tick: error is the host's position less the record's (x, y, z).
When the record has an elevator (recorded_elevator, -1 for none) and the
player now stands on that same one (current_elevator), the height of the
error is dropped, if the error across is within the correction tolerance and
the height within the phase tolerance (both ends included). Returns whether
what is left is further than the correction tolerance, so that the player is
moved by it (error as changed here); otherwise nothing is corrected. */
int network_rules_elevator_correction(int32_t recorded_elevator, int32_t current_elevator, float *error);

#endif /* __NETWORK_RULES_H */
