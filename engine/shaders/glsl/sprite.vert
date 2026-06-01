#version 460

layout(location = 0) in ivec2 a_position;  // packed half2 (int16)
layout(location = 1) in ivec2 a_uv;        // packed half2
layout(location = 2) in uvec4 a_color;     // u8vec4 RGBA

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(binding = 1) uniform CameraUBO {
    mat4 view_proj;
} camera;

void main() {
    vec2 pos = vec2(a_position);
    vec4 world = vec4(pos, 0.0, 1.0);
    gl_Position = camera.view_proj * world;
    v_uv = vec2(a_uv);
    v_color = vec4(a_color) / 255.0;
}
