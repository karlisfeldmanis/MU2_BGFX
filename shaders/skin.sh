// The skin: every posed figure's bones for the whole frame, in one buffer.
//
// A bone is three vec4s, the three rows of its 4x3, and a figure's bones lie one after another
// from where its pose begins -- the number its instance carries in i_data5.x, in vec4s
// (Renderer::addPalette). A vertex reads its four bones straight out of the buffer, so thirty
// figures of the same breed are still one instanced draw and the only thing uploaded per frame
// is the matrices.
//
// A buffer and not a texture since 2026-10-09: a hundred players in Noria spent 14% of the
// main thread in the Metal driver twiddling the palette texture into its tiled layout on every
// update (agxsTwiddleAddressCommon). A buffer is copied as it is, and packed, a figure takes
// the bones it has rather than a row of 128.
//
// Which pose a figure wears rides in the instance data beside its model matrix, so the shadow
// pass, the prepass and the shade pass all read the one buffer and a skinned draw differs from
// a static one by this program and an integer.
#ifndef MU2_SKIN_SH
#define MU2_SKIN_SH

#include <bgfx_compute.sh>

BUFFER_RO(b_bones, vec4, 12);

mat4 boneMatrix(int bone, int base)
{
	int x = base + bone * 3;
	vec4 r0 = b_bones[x + 0];
	vec4 r1 = b_bones[x + 1];
	vec4 r2 = b_bones[x + 2];
	// Rows in, and the fourth is the affine one. core/maths.h wrote the TRANSPOSE of its own
	// row-vector matrix into these three vec4s, because everything here multiplies on the
	// left -- the same reason vs_static reads its instance matrix with mtxFromCols.
	return mtxFromRows(r0, r1, r2, vec4(0.0, 0.0, 0.0, 1.0));
}

// Joint indices arrive as uvec4: Metal reads an unsigned byte attribute only into an
// unsigned shader type, and an ivec4 has the pipeline refused at link with no error and no
// geometry. docs/reference/conventions.md.
mat4 skinMatrix(uvec4 indices, vec4 weights, int base)
{
	mat4 m = boneMatrix(int(indices.x), base) * weights.x;
	m += boneMatrix(int(indices.y), base) * weights.y;
	m += boneMatrix(int(indices.z), base) * weights.z;
	m += boneMatrix(int(indices.w), base) * weights.w;
	return m;
}

#endif // MU2_SKIN_SH
