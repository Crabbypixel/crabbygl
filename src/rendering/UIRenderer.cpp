#include "stb/stb_image.h"

#include <glad/glad.h>

#include <iostream>

#include "rendering/UIRenderer.h"
#include "player/Inventory.h"

int UIRenderer::screenWidth = 0;
int UIRenderer::screenHeight = 0;

void UIRenderer::Init(int screenWidth, int screenHeight)
{
	this->screenWidth = screenWidth;
	this->screenHeight = screenHeight;

	matProjection = glm::ortho(0.0f, (float)screenWidth, 0.0f, (float)screenHeight, -1.0f, 1.0f);

	InitInventory();
	InitHotbar();
	InitHotbarCursor();
	InitDebugRect();
	InitIcons();
	InitASCII();
}

void UIRenderer::LoadIcons(const char* path)
{
	int w, h, channels;
	unsigned char* data = stbi_load(path, &w, &h, &channels, 0);
	if (!data)
	{
		std::cerr << "Atlas load failed: " << path << '\n';
		return;
	}

	unsigned int id;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);

	GLenum fmt = (channels == 4) ? GL_RGBA : GL_RGB;
	glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);

	// Nearest-neighbor keeps pixel art crisp
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	stbi_image_free(data);

	m_iconTexture = id;
}

void UIRenderer::LoadASCII(const char* path)
{
	int w, h, channels;
	unsigned char* data = stbi_load(path, &w, &h, &channels, 1); // 1 = force grayscale

	if (!data)
	{
		std::cerr << "Atlas load failed: " << path << '\n';
		return;
	}

	unsigned int id;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	stbi_image_free(data);

	m_asciiTexture = id;
}

// Returns the UV coordinates for a tile in the texture atlas based on its index
static std::pair<glm::vec2, glm::vec2> AtlasUV(int tileIndex)
{
	static constexpr float TILE_WIDTH = 32.0f;
	static constexpr float TILE_HEIGHT = 32.0f;
	static constexpr float TEXTURE_MAP_WIDTH = 512.0f;
	static constexpr float TEXTURE_MAP_HEIGHT = 512.0f;
	static constexpr float SCREEN_TILE_WIDTH = TILE_WIDTH / TEXTURE_MAP_WIDTH;
	static constexpr float SCREEN_TILE_HEIGHT = TILE_HEIGHT / TEXTURE_MAP_HEIGHT;
	static constexpr int SIZE = 16;

	int col = tileIndex % SIZE;
	int row = tileIndex / SIZE;

	float u0 = col * SCREEN_TILE_WIDTH;
	float u1 = (col + 1) * SCREEN_TILE_WIDTH;

	float v0 = 1.0f - (row + 1) * SCREEN_TILE_HEIGHT;
	float v1 = 1.0f - row * SCREEN_TILE_HEIGHT;

	return { { u0, v0 }, { u1, v1 } };
}

// Same as AtlasUV but for ASCII character map
static std::pair<glm::vec2, glm::vec2> ASCIICharUV(char ch)
{
	static constexpr float TILE_WIDTH = 8.0f;
	static constexpr float TILE_HEIGHT = 8.0f;
	static constexpr float TEXTURE_MAP_WIDTH = 128.0f;
	static constexpr float TEXTURE_MAP_HEIGHT = 128.0f;
	static constexpr float SCREEN_TILE_WIDTH = TILE_WIDTH / TEXTURE_MAP_WIDTH;
	static constexpr float SCREEN_TILE_HEIGHT = TILE_HEIGHT / TEXTURE_MAP_HEIGHT;
	static constexpr int SIZE = 16;

	int col = (int)ch % SIZE;
	int row = (int)ch / SIZE;

	float u0 = col * SCREEN_TILE_WIDTH;
	float u1 = (col + 1) * SCREEN_TILE_WIDTH;

	float v0 = 1.0f - (row + 1) * SCREEN_TILE_HEIGHT;
	float v1 = 1.0f - row * SCREEN_TILE_HEIGHT;

	return { { u0, v0 }, { u1, v1 } };
}

// Mapping between BlockType enum and the corresponding tile index in the texture atlas
static int GetIconIndex(BlockType t) noexcept
{
	switch (t)
	{
	case BlockType::AIR:
		return 0;
	case BlockType::GRASS_BLOCK:
		return 1;
	case BlockType::DIRT:
		return 2;
	case BlockType::COBBLESTONE:
		return 3;
	case BlockType::PLANK:
		return 4;
	case BlockType::BEDROCK:
		return 5;
	case BlockType::BRICK:
		return 6;
	case BlockType::TREE_LEAVES:
		return 7;
	case BlockType::TREE_LOG_Y:
	case BlockType::TREE_LOG_X:
	case BlockType::TREE_LOG_Z:
		return 8;
	case BlockType::STONE:
		return 9;
	case BlockType::SAND:
		return 10;
	case BlockType::GRAVEL:
		return 11;
	case BlockType::GLASS:
		return 12;
	case BlockType::SMOOTH_STONE:
		return 13;
	case BlockType::GLOWSTONE:
		return 14;

	case BlockType::GOLD_ORE:
		return 16;
	case BlockType::COAL_ORE:
		return 17;
	case BlockType::IRON_ORE:
		return 18;
	case BlockType::DIAMOND_ORE:
		return 19;
	case BlockType::WATER_FLOWING:
		return 20;
	case BlockType::WATER:
		return 21;

	case BlockType::SAPLING:
		return 48;
	case BlockType::ROSE:
		return 49;
	case BlockType::DANDELION:
		return 50;

	case BlockType::GRASS:
		return 64;
	case BlockType::RED_MUSHROOM:
		return 65;
	case BlockType::BROWN_MUSHROOM:
		return 66;
	// No mapping exists, return -1
	default:
		std::cerr << "Unknown block type: " << (int)t << '\n';
		return -1;
	}
}

glm::vec2 UIRenderer::GetInventorySlotPos(int slotIndex) noexcept
{
	// Scaled panel origin (bottom-left in screen space, Y-up ortho)
	float inventorySlotScreenPosX = (screenWidth - INV_TEX_W * INVENTORY_SCALE) * 0.5f;
	float inventorySlotScreenPosY = (screenHeight - INV_TEX_H * INVENTORY_SCALE) * 0.5f;

	// Texture Y -> Screen Y conversion (stbi flips, texture Y starts from top, screen Y starts from bottom)
	auto texYToScreenY = [&](float texY) {
		return inventorySlotScreenPosY + (INV_TEX_H - texY - INV_SLOT_SIZE) * INVENTORY_SCALE;
		};

	// Inventory has 36 slots: 0-8 are hotbar, 9-35 are main inventory
	// Deal with 0-8 first as it is separate from the main inventory
	if (slotIndex < 9)
	{
		// Hotbar slots 0-8
		return {
			inventorySlotScreenPosX + (INV_SLOT_START_X + slotIndex * INV_SLOT_SIZE) * INVENTORY_SCALE,
			texYToScreenY(HOTBAR_TEX_Y)
		};
	}

	// Main inventory slots 9-35 (3 rows of 9)
	int inventoryIndex = slotIndex - 9;
	int row = inventoryIndex / 9;
	int col = inventoryIndex % 9;

	return {
		inventorySlotScreenPosX + (INV_SLOT_START_X + col * INV_SLOT_SIZE) * INVENTORY_SCALE,
		texYToScreenY(INV_TEX_Y + row * INV_SLOT_SIZE)
	};
}

glm::vec2 UIRenderer::GetHotbarSlotPos(int slotIndex) noexcept
{
	float hotbarSlotScreenPosX = (screenWidth - HOTBAR_TEX_W) * 0.5f;
	float hotbarSlotScreenPosY = HOTBAR_SLOT_Y;

	float hotbarSlotPosX = hotbarSlotScreenPosX + 4.0f + (slotIndex * SLOT_SPACING);

	return { hotbarSlotPosX, hotbarSlotScreenPosY };
}

// Get inventory slot index based on mouse position, returns -1 if not hovering over any slot
int UIRenderer::GetMouseInventorySlot(float mouseX, float mouseY) noexcept
{
	constexpr float SLOT_SIZE = 32;

	for (int i = 0; i < 36; ++i)
	{
		glm::vec2 pos = GetInventorySlotPos(i);

		bool inside =
			mouseX >= pos.x && mouseX < pos.x + SLOT_SIZE
			&& mouseY >= pos.y && mouseY < pos.y + SLOT_SIZE;

		if (inside)
			return i;
	}

	return -1;
}

void UIRenderer::InitDebugRect()
{
	glGenVertexArrays(1, &m_debugRectVAO);
	glGenBuffers(1, &m_debugRectVBO);

	glBindVertexArray(m_debugRectVAO);

	glBindBuffer(GL_ARRAY_BUFFER, m_debugRectVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

	glEnableVertexAttribArray(0);
	glBindVertexArray(0);

	debugRectShader.load("assets/shaders/DebugRect.glsl");
}

void UIRenderer::InitHotbar()
{
	float w = (float)HOTBAR_TEX_W;
	float h = (float)HOTBAR_TEX_H;

	float x = (screenWidth - HOTBAR_TEX_W) * 0.5f;
	float y = HOTBAR_Y;

	float hotbarVertices[] =
	{
		x + w, y + h, 0.0f,   1.0f, 1.0f,
		x + w, y,     0.0f,   1.0f, 0.0f,
		x,     y,     0.0f,   0.0f, 0.0f,
		x,     y + h, 0.0f,   0.0f, 1.0f
	};

	unsigned int hotbarIndices[] = {
		0, 1, 3,   // first triangle
		1, 2, 3    // second triangle
	};

	// Load
	glGenVertexArrays(1, &m_hotbarVAO);
	glGenBuffers(1, &m_hotbarVBO);
	glGenBuffers(1, &m_hotbarEBO);

	glBindVertexArray(m_hotbarVAO);

	glBindBuffer(GL_ARRAY_BUFFER, m_hotbarVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(hotbarVertices), hotbarVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_hotbarEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(hotbarIndices), hotbarIndices, GL_STATIC_DRAW);

	// position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	// texture coord attribute
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	// Textures
	glGenTextures(1, &m_hotbarTexture);
	glBindTexture(GL_TEXTURE_2D, m_hotbarTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Load and generate texture
	int width, height, nrChannels;
	unsigned char* data = stbi_load("assets/textures/hotbar.png", &width, &height, &nrChannels, 0);

	if (data)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else
	{
		std::cerr << "Failed to load texture" << std::endl;
	}
	stbi_image_free(data);

	// Load shader
	hotbarShader.load("assets/shaders/Hotbar.glsl");
}

void UIRenderer::InitHotbarCursor()
{
	float w = (float)HOTBAR_CURSOR_W;
	float h = (float)HOTBAR_CURSOR_H;

	float x = (screenWidth - HOTBAR_CURSOR_W) * 0.5f;
	float y = HOTBAR_CURSOR_Y;

	float hotbarCursorVertices[] =
	{
		x + w, y + h, 0.0f,   1.0f,1.0f,
		x + w, y,     0.0f,   1.0f,0.0f,
		x,     y,     0.0f,   0.0f,0.0f,
		x,     y + h, 0.0f,   0.0f,1.0f
	};

	unsigned int hotbarCursorIndices[] = {
		0, 1, 3,   // first triangle
		1, 2, 3    // second triangle
	};

	// Load
	glGenVertexArrays(1, &m_hotbarCursorVAO);
	glGenBuffers(1, &m_hotbarCursorVBO);
	glGenBuffers(1, &m_hotbarCursorEBO);

	glBindVertexArray(m_hotbarCursorVAO);

	glBindBuffer(GL_ARRAY_BUFFER, m_hotbarCursorVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(hotbarCursorVertices), hotbarCursorVertices, GL_DYNAMIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_hotbarCursorEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(hotbarCursorIndices), hotbarCursorIndices, GL_DYNAMIC_DRAW);

	// position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	// texture coord attribute
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	// Textures
	glGenTextures(1, &m_hotbarCursorTexture);
	glBindTexture(GL_TEXTURE_2D, m_hotbarCursorTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Load and generate texture
	int width, height, nrChannels;
	unsigned char* data = stbi_load("assets/textures/hotbar_selector.png", &width, &height, &nrChannels, 0);

	if (data)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else
	{
		std::cerr << "Failed to load texture" << std::endl;
	}
	stbi_image_free(data);

	// Load shader
	hotbarCursorShader.load("assets/shaders/Hotbar_Selector.glsl");
}

void UIRenderer::InitInventory()
{
	float w = (float)INV_TEX_W * INVENTORY_SCALE;
	float h = (float)INV_TEX_H * INVENTORY_SCALE;

	int x = (screenWidth - (int)w) / 2;
	int y = (screenHeight - (int)h) / 2;

	float inventoryVertices[] =
	{
		(float)x + w, (float)y + h, 0.0f,   1.0f, 1.0f,
		(float)x + w, (float)y,     0.0f,   1.0f, 0.0f,
		(float)x,     (float)y,     0.0f,   0.0f, 0.0f,
		(float)x,     (float)y + h, 0.0f,   0.0f, 1.0f
	};

	unsigned int inventoryIndices[] = {
		0, 1, 3,   // first triangle
		1, 2, 3    // second triangle
	};

	// Load
	glGenVertexArrays(1, &m_inventoryVAO);
	glGenBuffers(1, &m_inventoryVBO);
	glGenBuffers(1, &m_inventoryEBO);

	glBindVertexArray(m_inventoryVAO);

	glBindBuffer(GL_ARRAY_BUFFER, m_inventoryVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(inventoryVertices), inventoryVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_inventoryEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(inventoryIndices), inventoryIndices, GL_STATIC_DRAW);

	// position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	// texture coord attribute
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	// Textures
	glGenTextures(1, &m_inventoryTexture);
	glBindTexture(GL_TEXTURE_2D, m_inventoryTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Load and generate texture
	int width, height, nrChannels;
	unsigned char* data = stbi_load("assets/textures/player_inventory.png", &width, &height, &nrChannels, 0);

	if (data)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		//glGenerateMipmap(GL_TEXTURE_2D);
	}
	else
	{
		std::cerr << "Failed to load texture" << std::endl;
	}
	stbi_image_free(data);

	// Load shader
	inventoryShader.load("assets/shaders/Inventory.glsl");
}

void UIRenderer::InitIcons()
{
	glGenVertexArrays(1, &m_iconVAO);
	glGenBuffers(1, &m_iconVBO);

	glBindVertexArray(m_iconVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_iconVBO);

	glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	iconShader.load("assets/shaders/Icons.glsl");
}

void UIRenderer::InitASCII()
{
	glGenVertexArrays(1, &m_asciiVAO);
	glGenBuffers(1, &m_asciiVBO);

	glBindVertexArray(m_asciiVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_asciiVBO);

	glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW); // sized at draw time

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);

	asciiShader.load("assets/shaders/ASCII.glsl");
}

void UIRenderer::DrawText(float x, float y, float scale, const std::string& text, glm::vec4 color) noexcept
{
	auto GlyphAdvance = [](char c, float cw) -> float
		{
			switch (c)
			{
			case 'i':
			case 'l':
			case '!':
			case 'I':
				return cw * 0.5f;
			case 't':
				return cw * 0.7f;

			default:
				return cw;
			}
		};
	constexpr float CHAR_W = 8.0f;
	constexpr float CHAR_H = 8.0f;

	float cw = CHAR_W * scale;
	float ch = CHAR_H * scale;

	// 6 verts * 4 floats (xy + uv) per char
	std::vector<float> verts;
	verts.reserve(text.size() * 6 * 4);

	float cx = x;
	for (char c : text)
	{
		if (c == ' ')
		{
			cx += cw;
			continue;
		}

		auto [uvMin, uvMax] = ASCIICharUV(c);

		float x0 = cx, y0 = y;
		float x1 = cx + cw, y1 = y + ch;

		float quad[] = {
			x0, y0, uvMin.x, uvMin.y,
			x1, y0, uvMax.x, uvMin.y,
			x1, y1, uvMax.x, uvMax.y,

			x0, y0, uvMin.x, uvMin.y,
			x1, y1, uvMax.x, uvMax.y,
			x0, y1, uvMin.x, uvMax.y,
		};

		verts.insert(verts.end(), std::begin(quad), std::end(quad));	// O(1)
		//cx += cw
		//cx += m_glyphWidths[(unsigned char)c] * scale;
		cx += GlyphAdvance(c, cw);
	}

	if (verts.empty())
		return;

	glBindBuffer(GL_ARRAY_BUFFER, m_asciiVBO);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);

	asciiShader.use();
	asciiShader.setMat4("matProjection", matProjection);
	asciiShader.setInt("uAsciiTexture", 6);
	asciiShader.setVec4("uColor", color);

	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, m_asciiTexture);

	glBindVertexArray(m_asciiVAO);
	glDrawArrays(GL_TRIANGLES, 0, (int)(verts.size() / 4));

	glBindVertexArray(0);
}

void UIRenderer::DrawTextBold(float x, float y, float scale, const std::string& text, glm::vec4 color) noexcept
{
	glm::vec4 shadow = { color.r * 0.25f, color.g * 0.25f, color.b * 0.25f, color.a };

	DrawText(x + 1.0f, y - 1.0f, scale, text, shadow);  // shadow pass
	DrawText(x, y, scale, text, color);					// main pass
}

void UIRenderer::DrawHotbar() noexcept
{
	hotbarShader.use();
	hotbarShader.setInt("uHotbarTexture", 2);
	hotbarShader.setMat4("matProjection", matProjection);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, m_hotbarTexture);

	glBindVertexArray(m_hotbarVAO);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void UIRenderer::DrawHotbarCursor(int index) noexcept
{
	float hotbarX = (screenWidth - HOTBAR_TEX_W) * 0.5f;

	float x = hotbarX - 1.0f + (index * SLOT_SPACING);
	float y = HOTBAR_CURSOR_Y;

	float w = (float)HOTBAR_CURSOR_W;
	float h = (float)HOTBAR_CURSOR_H;

	float verts[] =
	{
		x + w, y + h, 0.0f, 1.0f, 1.0f,
		x + w, y,     0.0f, 1.0f, 0.0f,
		x,     y,     0.0f, 0.0f, 0.0f,
		x,     y + h, 0.0f, 0.0f, 1.0f
	};

	glBindBuffer(GL_ARRAY_BUFFER, m_hotbarCursorVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

	hotbarCursorShader.use();
	hotbarCursorShader.setInt("uHotbarSelectorTexture", 3);
	hotbarCursorShader.setMat4("matProjection", matProjection);

	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, m_hotbarCursorTexture);

	glBindVertexArray(m_hotbarCursorVAO);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void UIRenderer::DrawInventory() noexcept
{
	inventoryShader.use();
	inventoryShader.setInt("uInventoryTexture", 4);
	inventoryShader.setMat4("matProjection", matProjection);

	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, m_inventoryTexture);

	glBindVertexArray(m_inventoryVAO);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void UIRenderer::DrawHighlightRect(float x, float y, float w, float h, const glm::vec4& color) noexcept
{
	float verts[] =
	{
		x,     y,
		x + w,   y,
		x + w,   y + h,

		x,     y,
		x + w,   y + h,
		x,     y + h
	};

	debugRectShader.use();

	debugRectShader.setMat4("matProjection", matProjection);
	debugRectShader.setVec4("uColor", color);

	glBindVertexArray(m_debugRectVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_debugRectVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	glBindVertexArray(0);
}

void UIRenderer::DrawInventoryIcons(const Inventory& inv) noexcept
{
	for (int i = 0; i < Inventory::INVENTORY_SIZE; ++i)
	{
		auto [uvMin, uvMax] = AtlasUV(GetIconIndex(inv.At(i).type));
		glm::vec2 pos = GetInventorySlotPos(i);

		DrawTexturedQuad(pos.x, pos.y, 32, 32, uvMin, uvMax, m_iconTexture);
	}
}

void UIRenderer::DrawHotbarIcons(const Inventory& inv) noexcept
{
	for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i)
	{
		auto [uvMin, uvMax] = AtlasUV(GetIconIndex(inv.At(i).type));
		glm::vec2 pos = GetHotbarSlotPos(i);

		DrawTexturedQuad(pos.x, pos.y, 32, 32, uvMin, uvMax, m_iconTexture);
	}
}

void UIRenderer::DrawTexturedQuad(float x, float y, float w, float h, glm::vec2 uvMin, glm::vec2 uvMax, unsigned int texID) noexcept
{
	float verts[] = {
		x,     y,     uvMin.x, uvMin.y,
		x + w, y,     uvMax.x, uvMin.y,
		x + w, y + h, uvMax.x, uvMax.y,

		x,     y,     uvMin.x, uvMin.y,
		x + w, y + h, uvMax.x, uvMax.y,
		x,     y + h, uvMin.x, uvMax.y,
	};

	glBindBuffer(GL_ARRAY_BUFFER, m_iconVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

	iconShader.use();
	iconShader.setMat4("matProjection", matProjection);
	iconShader.setInt("uIconTexture", 5);

	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, texID);

	glBindVertexArray(m_iconVAO);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

void UIRenderer::DrawHeldItem(const Inventory& inv, float mouseX, float mouseY) noexcept
{
	if (inv.GetDragItem().IsEmpty())
		return;

	constexpr float SIZE = INV_SLOT_SIZE * INVENTORY_SCALE;
	auto [uvMin, uvMax] = AtlasUV(GetIconIndex(inv.GetDragItem().type));

	// Draw item on cursor
	DrawTexturedQuad(mouseX - SIZE * 0.5f, mouseY - SIZE * 0.5f, SIZE, SIZE, uvMin, uvMax, m_iconTexture);
}

UIRenderer::~UIRenderer() noexcept
{
	// Hotbar
	glDeleteVertexArrays(1, &m_hotbarVAO);
	glDeleteBuffers(1, &m_hotbarVBO);
	glDeleteBuffers(1, &m_hotbarEBO);
	glDeleteTextures(1, &m_hotbarTexture);

	// Hotbar cursor
	glDeleteVertexArrays(1, &m_hotbarCursorVAO);
	glDeleteBuffers(1, &m_hotbarCursorVBO);
	glDeleteBuffers(1, &m_hotbarCursorEBO);
	glDeleteTextures(1, &m_hotbarCursorTexture);

	// Inventory
	glDeleteVertexArrays(1, &m_inventoryVAO);
	glDeleteBuffers(1, &m_inventoryVBO);
	glDeleteBuffers(1, &m_inventoryEBO);
	glDeleteTextures(1, &m_inventoryTexture);

	// Highlight - debug
	glDeleteVertexArrays(1, &m_debugRectVAO);
	glDeleteBuffers(1, &m_debugRectVBO);

	// Icons
	glDeleteVertexArrays(1, &m_iconVAO);
	glDeleteBuffers(1, &m_iconVBO);
	glDeleteTextures(1, &m_iconTexture);

	// Font
	glDeleteVertexArrays(1, &m_asciiVAO);
	glDeleteBuffers(1, &m_asciiVBO);
	glDeleteTextures(1, &m_asciiTexture);
}
