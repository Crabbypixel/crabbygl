#ifdef SHADER_VERTEX
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;

out vec2 fUV;
uniform mat4 matProjection;

void main()
{
	fUV = aUV;
	gl_Position = matProjection * vec4(aPos, 0.0f, 1.0f);
}
#endif

#ifdef SHADER_FRAGMENT
in vec2 fUV;
out vec4 FragColor;

uniform sampler2D uAsciiTexture;
uniform vec4 uColor;

void main()
{
	float brightness = texture(uAsciiTexture, fUV).r;
	if (brightness < 0.05) discard;
	FragColor = vec4(uColor.rgb, uColor.a * brightness);
}
#endif