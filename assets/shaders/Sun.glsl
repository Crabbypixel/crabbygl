#ifdef SHADER_VERTEX
layout (location = 0) in vec2 aOffset;

layout (std140) uniform Matrices
{
	mat4 matProjection;
	mat4 matView;
};

uniform vec3 uSunWorldPos;
uniform vec3 uSunRight;
uniform vec3 uSunUp;
uniform float uSunSize;

out vec2 fUV;

void main()
{
	fUV = aOffset;

	vec3 worldPos = uSunWorldPos + (aOffset.x * uSunRight + aOffset.y * uSunUp) * uSunSize;
	gl_Position = matProjection * matView * vec4(worldPos, 1.0f);
}
#endif

#ifdef SHADER_FRAGMENT
in vec2 fUV;
out vec4 FragColor;

uniform vec3 uSunColor;

const float PIXELS = 16.0f;	// Pixel-art resolution across the whole quad
const float CORE = 0.6f;

void main()
{
	// Snap to pixel centres -> blocky edges
	vec2 p = (floor((fUV * 0.5f + 0.5f) * PIXELS) + 0.5f) / PIXELS * 2.0f - 1.0f;

	// Chebyshev distance -> square rings, not circles
	float d = max(abs(p.x), abs(p.y));

	float core = step(d, CORE);
	float halo = (1.0f - smoothstep(CORE, 1.0f, d)) * 0.2f;

	vec3 haloColor = uSunColor * vec3(1.0f, 0.85f, 0.5f);
	FragColor = vec4(mix(haloColor, vec3(1.0f), core), max(core, halo));
}
#endif