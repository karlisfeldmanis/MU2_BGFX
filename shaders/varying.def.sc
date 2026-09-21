vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec4 a_tangent   : TANGENT;
vec2 a_texcoord0 : TEXCOORD0;
vec4 a_color0    : COLOR0;

// One instance is a 4x4 model matrix and a colour, five vec4s. Every pass reads the same
// buffer; the depth passes read the first four and leave the fifth alone.
vec4 i_data0 : TEXCOORD31;
vec4 i_data1 : TEXCOORD30;
vec4 i_data2 : TEXCOORD29;
vec4 i_data3 : TEXCOORD28;
vec4 i_data4 : TEXCOORD27;

vec3 v_wpos      : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec2 v_texcoord0 : TEXCOORD1 = vec2(0.0, 0.0);
vec3 v_normal    : NORMAL    = vec3(0.0, 1.0, 0.0);
vec4 v_tangent   : TANGENT   = vec4(1.0, 0.0, 0.0, 1.0);
vec3 v_vnormal   : TEXCOORD2 = vec3(0.0, 0.0, 1.0);
vec3 v_vpos      : TEXCOORD3 = vec3(0.0, 0.0, 0.0);
vec4 v_colour    : COLOR0    = vec4(1.0, 1.0, 1.0, 0.0);
vec4 v_light     : TEXCOORD4 = vec4(1.0, 1.0, 1.0, 1.0);

// The skin: four joint indices and their weights. uvec4 and not ivec4, for the reason
// common.sh's skinMatrix states.
uvec4 a_indices  : BLENDINDICES;
vec4  a_weight   : BLENDWEIGHT;

// A sixth instance vec4, x being the row this figure's bones occupy in the palette texture.
// Static draws leave it unread; the stride is what the buffer is walked by, not what a
// shader reads.
vec4 i_data5 : TEXCOORD26;
