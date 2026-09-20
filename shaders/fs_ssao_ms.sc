$input v_texcoord0

// The multisampled twin: same body, reading one sample of the prepass.
#define MU2_PREPASS_MS 1
#include "fs_ssao_body.sh"
