$input v_texcoord0, v_colour, v_material

// The multisampled twin: same body, reading one sample of the prepass for a soft sprite's
// depth (gfx::Effects::setPrepass).
#define MU2_PREPASS_MS 1
#include "fs_effect_body.sh"
