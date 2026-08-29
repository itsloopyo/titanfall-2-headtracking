#pragma once

namespace headtracking {

// Makes the Titan cockpit follow the head instead of being swept across by it.
//
// ---- What goes wrong without this -------------------------------------------
//
// The head rotation goes into the angles the frame's camera is built from
// (view_angles_hook.h), and nothing the game owns moves with it - which is the
// whole point, and is exactly right for a pilot. The weapon is placed from the
// player's clean angles, so turning your head leaves the gun where the aim is
// and swings it across the frame, the way looking away from something you are
// holding actually looks.
//
// Inside a Titan the same rule reads completely differently. `C_Titan_Cockpit`
// is a viewmodel too, so the cockpit shell is also placed from the clean angles
// and also stays put while the view turns - but the shell WRAPS the player. So
// turning your head does not show you more of the world, it walks your eyes
// across the inside of the cockpit, and the head tracking is worse than useless
// there. Reported from the chair as looking around inside the helmet.
//
// A Titan cockpit is a helmet, not a room: it should turn with the head and keep
// the view out of it pointed wherever the head is pointed. So the head delta is
// composed onto the cockpit's own placement, which puts it back where it was in
// the picture and leaves the head free to look through it.
//
// ---- Where it goes in -------------------------------------------------------
//
// `C_Titan_Cockpit::CalcView(this, Vector* origin, QAngle* angles)`
// (client.dll+0x4e4000), the cockpit's own transform: it scales the player's
// pitch by `cockpit_pitch_up_frac` / `_down_frac`, subtracts the cockpit drift
// and finishes on Source's usual ApplyShake(origin, angles, 1.0) tail. Its only
// caller __RTDynamicCasts the entity to C_Titan_Cockpit first, so the detour
// runs when there is a cockpit to place and at no other time - the Titan gate is
// the hook itself, and there is no "am I in a Titan" flag to read or to get
// wrong.
//
// The ORIGIN is deliberately left alone. The positional lean moves the eye and
// not the cockpit, so leaning still buys parallax against the frame and the
// canopy - which is the part of 6DOF that a cockpit makes better, not worse.
bool InstallCockpitHook();

}  // namespace headtracking
