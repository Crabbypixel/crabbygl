#ifdef SHADER_VERTEX
layout (location = 0) in vec3 aOffset;

layout (std140) uniform Matrices
{
	mat4 matProjection;
	mat4 matView;
};

uniform vec3 uSunWorldPos;
uniform float uSunSize;

out vec2 fUV;

void main()
{
	fUV = aOffset;

	// Transform to view space, then offset in view-space XY. Since view-space X/Y are
	// always camera-facing this billboards without needing camera right/up vectors on CPU.
	vec4 viewPos = matView * vec4(uSunWorldPos, 1.0f);
	viewPos.xy += aOffset * uSunSize;

	gl_Position = matProjection * viewPos;
}
#endif

#ifdef SHADER_FRAGMENT
in vec2 fUV;
out vec4 FragColor;

uniform vec3 uSunColor;

void main()
{
	float d = length(fUV);
	if(d > 1.0f)
		discard;

	// Soft glow falloff towards edge, hard on center
	float glow = smoothstep(1.0f, 0.35f, d);
	FragColor = vec4(uSunColor, glow);
}
#endif