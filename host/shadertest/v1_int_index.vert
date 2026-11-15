#version 300 es
precision highp float;
precision highp int;

struct DrawParameters
{
    vec4 offset;
};

layout(std140) uniform ubo_DrawParameters
{
    DrawParameters Chunks[256];
} _57;

uniform mat4 u_ModelViewMatrix;
uniform mat4 u_ProjectionMatrix;

in vec4 a_PosId;
in vec2 a_TexCoord;
in vec2 a_LightCoord;
in vec4 a_Color;
out float v_FragDistance;
out vec4 v_Color;
out vec2 v_LightCoord;
out vec2 v_TexCoord;
vec3 _vert_position;
vec2 _vert_tex_diffuse_coord;
vec2 _vert_tex_light_coord;
vec4 _vert_color;
int _draw_id;

void _vert_init()
{
    _vert_position = (a_PosId.xyz * 0.00048828125) + vec3(-8.0);
    _vert_tex_diffuse_coord = a_TexCoord * 1.52587890625e-05;
    _vert_tex_light_coord = a_LightCoord;
    _vert_color = a_Color;
    _draw_id = int(a_PosId.w);
}

void main()
{
    _vert_init();
    vec4 position = u_ModelViewMatrix * vec4(_57.Chunks[_draw_id].offset.xyz + _vert_position, 1.0);
    v_FragDistance = length(position);
    gl_Position = u_ProjectionMatrix * position;
    v_Color = _vert_color;
    v_LightCoord = _vert_tex_light_coord;
    v_TexCoord = _vert_tex_diffuse_coord;
}


