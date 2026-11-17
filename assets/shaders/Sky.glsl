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

uniform mat4 uInvViewProj;		// inverse(proj * rotation-only view) -> reconstruct a dir in world space from screen-space coords
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uSunDir;			// World -> sun
uniform vec4 uSunsetColor;		// rgb + strength (classic minecraft formula)
uniform float uSkyAngle;		// Sun's orbit angle (stars rotate with it)
uniform float uStarStrength;	// 0 at day, 1 at midnight

const float STAR_GRID = 400.0f;		// Cells per unit direction -> star size (2-3 px at 1080p)
const float STAR_DENSITY = 0.9996f;	// Fraction of cells left empty; lower = more stars

float Hash(vec3 p3)
{
	p3 = fract(p3 * 0.1031f);
	p3 += dot(p3, p3.zyx + 31.32f);
	return fract((p3.x + p3.y) * p3.z);
}

void main()
{
	// World-space view ray for this pixel (rotation only -> independent of position)
	vec4 p = uInvViewProj * vec4(fNDC, 1.0f, 1.0f);
	vec3 dir = normalize(p.xyz / p.w);

	// Gradient; whitish at the horizon, deeper overhead - sqrt keeps the light band thin
	float up = clamp(dir.y, 0.0f, 1.0f);
	vec3 color = mix(uHorizonColor, uZenithColor, sqrt(up));

	// Stars: fixed to a sky sphere that rotates with the sun
	if(uStarStrength > 0.0f)
	{
		// Undo the orbit rotation (around Z) so that stars rotate with the sun
		float c = cos(uSkyAngle);
		float s = sin(uSkyAngle);
		vec3 sd = vec3(c * dir.x + s * dir.y, -s * dir.x + c * dir.y, dir.z);

		float h = Hash(floor(sd * STAR_GRID));
		if(h > STAR_DENSITY)
		{
			// Tail of the same hash -> per-star brightness 0.5 to 1
			float brightness = 0.5f + 0.5f * fract(h * 1000.0f);

			// Thicker atmosphere near the horizon
			float horizonFade = smoothstep(0.0f, 0.2f, dir.y);
			
			color += vec3(brightness * uStarStrength * horizonFade);
		}
	}

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