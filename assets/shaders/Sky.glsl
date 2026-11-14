#ifdef SHADER_VERTEX
layout (location = 0) in vec2 aPos;		// quadVAO NDC

out vec2 fNDC;

void main()
{
	fNDC = aPos;
	gl_Position = vec4(aPos, 0.0f, 1.0f);
}
#endif

#ifdef SHADER_FRAGMENT
in vec2 fNDC;
out vec4 FragColor;

uniform mat4 uInvViewProj;		// inverse(proj * rotation-only view)
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uSunDir;			// World -> sun
uniform vec4 uSunsetColor;		// rgb + strength (classic minecraft formula)

void main()
{
	// World-space view ray for this pixel (rotation only -> independent of position)
	vec4 p = uInvViewProj * vec4(fNDC, 1.0f, 1.0f);
	vec3 dir = normalize(p.xyz / p.w);

	// Gradient; whitish at the horizon, deeper overhead - sqrt keeps the light band thin
	float up = clamp(dir.y, 0.0f, 1.0f);
	vec3 color = mix(uHorizonColor, uZenithColor, sqrt(up));

	// Sunset fan: only on the sun's side, with the horizon
	float dLen = length(dir.xz);
	float sLen = length(uSunDir.xz);
	float facing = (dLen > 1e-4f && sLen > 1e-4f) ? dot(dir.xz / dLen, uSunDir.xz / sLen) : 0.0f;
	float side = max(facing, 0.0f);
	side *= side;

	float band = 1.0f - smoothstep(0.0f, 0.5f, abs(dir.y));

	color = mix(color, uSunsetColor.rgb, uSunsetColor.a * side * band);
	FragColor = vec4(color, 1.0f);
}
#endif