#version 130
uniform vec4 gPanel;
uniform vec2 gViewport;
void main()
{
    vec2 p = vec2((gl_VertexID == 1 || gl_VertexID == 2) ? 1.0 : 0.0,
                  (gl_VertexID >= 2) ? 1.0 : 0.0);
    vec2 pixel = gPanel.xy - vec2(16.0) + p * (gPanel.zw + vec2(32.0));
    gl_Position = vec4(pixel.x / gViewport.x * 2.0 - 1.0, 1.0 - pixel.y / gViewport.y * 2.0, 0.0, 1.0);
}
