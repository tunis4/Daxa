#include <shared.inl>

DAXA_DECL_PUSH_CONSTANT(DrawPush, p)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out vec4 v_col;
void main()
{
    DrawVertex vert = deref(p.face_buffer).verts[gl_VertexIndex];
    gl_Position = vec4(vert.pos.xy, 0, 1);
    v_col = vert.col;
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in vec4 v_col;
layout(location = 0) out vec4 color;
void main()
{
    color = v_col;
}

#endif
