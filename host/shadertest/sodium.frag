#version 300 es
precision highp float;
precision highp int;

uniform highp sampler2D u_BlockTex;
uniform highp sampler2D u_LightTex;
uniform highp vec4 u_FogColor;
uniform highp float u_FogStart;
uniform highp float u_FogEnd;

in highp vec2 v_TexCoord;
in highp vec2 v_LightCoord;
in highp vec4 v_Color;
layout (location = 0) out highp vec4 fragColor;
in highp float v_FragDistance;

highp vec4 _linearFog(highp vec4 fragColor_1, highp float fragDistance, highp vec4 fogColor, highp float fogStart, highp float fogEnd)
{
    highp vec4 result = mix(fogColor, fragColor_1, vec4(smoothstep(fogEnd, fogStart, fragDistance)));
    result.w = fragColor_1.w;
    return result;
}

void main()
{
    highp vec4 sampleBlockTex = texture(u_BlockTex, v_TexCoord);
    highp vec4 sampleLightTex = texture(u_LightTex, v_LightCoord);
    highp vec4 diffuseColor = sampleBlockTex * sampleLightTex;
    diffuseColor *= v_Color;
    highp vec4 param = diffuseColor;
    highp float param_1 = v_FragDistance;
    highp vec4 param_2 = u_FogColor;
    highp float param_3 = u_FogStart;
    highp float param_4 = u_FogEnd;
    fragColor = _linearFog(param, param_1, param_2, param_3, param_4);
}

