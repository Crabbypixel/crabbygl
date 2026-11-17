// Axes
float line_vertices[] = {
	0.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,

	0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f,

	0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f
};

// Crosshair
float crosshairVertices[] = {
	// Horizontal
	-0.012f,  0.0f,
	 0.012f,  0.0f,

	// Vertical
	 0.0f, -0.01f,
	 0.0f,  0.01f
};

// Vertex attributes for a quad that fills the entire screen in Normalized Device Coordinates
float quadVertices[] = {
	// positions   // texCoords
	-1.0f,  1.0f,  0.0f, 1.0f,
	-1.0f, -1.0f,  0.0f, 0.0f,
	 1.0f, -1.0f,  1.0f, 0.0f,

	-1.0f,  1.0f,  0.0f, 1.0f,
	 1.0f, -1.0f,  1.0f, 0.0f,
	 1.0f,  1.0f,  1.0f, 1.0f
};

