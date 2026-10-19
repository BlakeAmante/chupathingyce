/*
NETWORK_RULES.C

The rules of network_rules.h.
*/

#include "network_rules.h"

int network_rules_floor_contact(int airborne, int has_support_surface, int elevator_ticks)
{
	return !airborne && has_support_surface && elevator_ticks == NETWORK_RULES_ELEVATOR_CONTACT_TICKS;
}

int network_rules_riding_elevator(int elevator_is_machine, int elevator_moving, int floor_contact)
{
	return elevator_is_machine && (elevator_moving || floor_contact);
}

int network_rules_elevator_correction(int32_t recorded_elevator, int32_t current_elevator, float *error)
{
	float across_squared = error[0] * error[0] + error[1] * error[1];
	const float tolerance = NETWORK_RULES_LOCAL_CORRECTION_TOLERANCE;

	if (recorded_elevator != -1 && recorded_elevator == current_elevator &&
		across_squared <= tolerance * tolerance &&
		error[2] >= -NETWORK_RULES_ELEVATOR_PHASE_TOLERANCE && error[2] <= NETWORK_RULES_ELEVATOR_PHASE_TOLERANCE)
	{
		error[2] = 0.0f;
	}
	return error[0] * error[0] + error[1] * error[1] + error[2] * error[2] > tolerance * tolerance;
}
