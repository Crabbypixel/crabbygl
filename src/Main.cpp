#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/Camera.h"
#include "core/OpenGL_3D.h"

#include "models/VertexData.h"
#include "debug/ChunkDebug.h"

#include "world/Raycast.h"
#include "world/World.h"
#include "world/Chunk.h"
#include "world/BlockRegistry.h"
#include "world/LightingSystem.h"

#include "player/Player.h"
#include "player/Inventory.h"

#include "physics/WorldPhysics.h"

#include "rendering/VertexArray.h"
#include "rendering/VertexBuffer.h"
#include "rendering/BufferLayout.h"
#include "rendering/UIRenderer.h"

#include "imgui/imgui_includes.h"
#include "imgui/imgui_internal.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>

class Window : public OpenGL_3D
{
private:
	// Axes
	VertexArray axesVAO;
	VertexBuffer<float> axesVBO;
	BufferLayout axesLayout;

	// Quad
	VertexArray quadVAO;
	VertexBuffer<float> quadVBO;
	BufferLayout quadLayout;

	// Crosshair
	VertexArray crosshairVAO;
	VertexBuffer<float> crosshairVBO;
	BufferLayout crosshairLayout;

	// Shaders
	Shader axesShader;
	Shader framebufferShader;
	Shader crosshairShader;
	Shader chunkMeshShader;
	Shader waterMeshShader;

	// Player
	Player player;

	// World
	World world;

	// Chunk outlines for debugging
	ChunkDebug chunkDebug;

	// Hotbar & Inventory
	UIRenderer uiRenderer;
	Inventory inventory;

	// Framebuffer variables
	unsigned int framebuffer = 0;
	unsigned int textureColorBuffer = 0;
	unsigned int rbo = 0;

	// Jump fly
	const float DOUBLE_TAP_WINDOW = 0.3f;
	float spaceTimer = 0.0f;
	bool waitingForSecondTap = false;
	bool isAOEnabled = true;
	bool shouldDrawAsWireframe = false;

public:
	bool Setup() override
	{
		player.SetPos(glm::vec3(35.0f, 97.0f, 77.0f));
		//player.SetPos(glm::vec3(65.0f, 256.0f, 38.0f));
		//player.SetPos(glm::vec3(7526.0f, 49.0f, 3784.0f));
		camera.Init(player.GetPos(), glm::vec3(0.0f, 0.0f, -1.0f), ScreenWidth(), ScreenHeight());

		// Axes
		axesVAO.generate();
		axesVBO.generate(3);		// 3 floats per vertex
		axesVBO.setBuffer(sizeof(line_vertices), (const void*)line_vertices);
		axesLayout.setBufferLayout(axesVAO, axesVBO, 3, BufferType::FLOAT);
		axesShader.load("assets/shaders/Line.glsl");

		// Quad
		quadVAO.generate();
		quadVBO.generate(4);		// 4 floats per vertex
		quadVBO.setBuffer(sizeof(quadVertices), (const void*)quadVertices);
		quadLayout.setBufferLayout(quadVAO, quadVBO, 2, BufferType::FLOAT);			// 2 for NDC vertices
		quadLayout.setBufferLayout(quadVAO, quadVBO, 2, BufferType::FLOAT);			// 2 for texture coords

		// Crosshair
		crosshairVAO.generate();
		crosshairVBO.generate(2);	// 2 floats per vertex
		crosshairVBO.setBuffer(sizeof(crosshairVertices), (const void*)crosshairVertices);
		crosshairLayout.setBufferLayout(crosshairVAO, crosshairVBO, 2, BufferType::FLOAT);	// 2 for positions (NDC)
		crosshairShader.load("assets/shaders/Crosshair.glsl");

		// Chunk boundaries
		chunkDebug.Init("assets/shaders/ChunkDebug.glsl");

		// UI Renderer
		uiRenderer.Init(ScreenWidth(), ScreenHeight());
		uiRenderer.LoadIcons("assets/textures/icons.png");
		uiRenderer.LoadASCII("assets/textures/ascii.png");

		// Inventory
		if (!inventory.Load("saves/player_inventory.bin"))
			std::cout << "Error loading player inventory\n";

		// ───── World ──────────────────────────────────────────────────
		chunkMeshShader.load("assets/shaders/ChunkMesh.glsl");
		waterMeshShader.load("assets/shaders/WaterMesh.glsl");

		world.SetChunkShader(chunkMeshShader, waterMeshShader);
		world.LoadAtlasTexture("assets/textures/textures.png");

		// Spawn 4 threads for loading chunks and 4 threads for generating meshes
		// This is the optimal spot for good performance without overly increasing number of threads
		world.StartChunkLoadWorkers(4);
		world.StartMeshWorkers(4);

		// ───── Shaders ──────────────────────────────────────────────────
		InitShaders();

		{
			// ───── Framebuffers ──────────────────────────────────────────────────
			// Generate and bind the framebuffer
			framebufferShader.load("assets/shaders/Framebuffer.glsl");

			// Generate and bind the framebuffer
			glGenFramebuffers(1, &framebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

			// Create a texture attachment for color attachment
			glGenTextures(1, &textureColorBuffer);
			glBindTexture(GL_TEXTURE_2D, textureColorBuffer);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenWidth(), ScreenHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(GL_TEXTURE_2D, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureColorBuffer, 0);			// Finally attach the texture attachment to the currently bound framebuffer as color attachment

			// Create a renderbuffer object for depth and stencil attachments (as we won't be sampling these, renderbuffer is a better choice)
			glGenRenderbuffers(1, &rbo);
			glBindRenderbuffer(GL_RENDERBUFFER, rbo);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, ScreenWidth(), ScreenHeight());
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);				// Finally attach the renderbuffer to the currently bound framebuffer as depth & stencil attachment

			// Check if the custom framebuffer is complete
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				std::cerr << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;

			// Bind back to the default framebuffer
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			// ──────────────────────────────────────────────────────────────

			// ───── UBOs ──────────────────────────────────────────────────
			// UBOs to unnecessarily avoid settings uniforms in shaders repeatedly
			// Bind "Matrices" uniform to binding index 0 in every shader
			glUniformBlockBinding(axesShader.getID(), glGetUniformBlockIndex(axesShader.getID(), "Matrices"), 0);
			glUniformBlockBinding(chunkMeshShader.getID(), glGetUniformBlockIndex(chunkMeshShader.getID(), "Matrices"), 0);
			glUniformBlockBinding(waterMeshShader.getID(), glGetUniformBlockIndex(waterMeshShader.getID(), "Matrices"), 0);

			ErrorLog();

			// Same for the other side
			glGenBuffers(1, &uboMatrices);
			glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
			glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), NULL, GL_STATIC_DRAW);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);

			glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboMatrices);
			// ──────────────────────────────────────────────────────────────
		}

		// Initialize ImGui
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		ImGui::StyleColorsDark();
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 330");

		// Enable transparency
		glEnable(GL_BLEND);

		return true;
	}

	bool Update(float dt) override
	{
		// ImGui - new frame settings
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Bind to the custom framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

		// Clear colorbuffer, depthbuffer and stencilbuffer
		glClearColor(0.227f, 0.757f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		// Use the depth buffer
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);

		// Get user controls
		UserControls(dt);

		// ───── Physics ───────────────────────────────────────────────
		if (!bIsPaused)
		{
			bool shouldUpdatePlayerMovement = !inventory.IsOpen();

			player.Update(
				dt,
				camera.front,
				GetKey('W').bHeld,
				GetKey('S').bHeld,
				GetKey('A').bHeld,
				GetKey('D').bHeld,
				GetKey(GLFW_KEY_SPACE).bHeld,
				GetKey(GLFW_KEY_LEFT_SHIFT).bHeld,
				GetKey(GLFW_KEY_LEFT_CONTROL).bHeld,
				shouldUpdatePlayerMovement,
				world
			);

			// Camera tracks player head
			camera.position = player.EyePos();

			// Physics test - generate a gravel platform to test physics (in development - prone to bugs)
			/*
			if (GetKey('U').bPressed)
			{
				world.SetBlock(0, 60, 0, BlockType::WATER_FLOWING);
			}
			*/
		}

		// Raycast
		RaycastHit m_currentHit = RaycastDDA(camera.position, camera.front, world);
		const glm::ivec3& raycastHitPos = m_currentHit.blockPos;
		const glm::ivec3& raycastPlacePos = m_currentHit.blockPos + m_currentHit.normal;
		glm::ivec3 playerPos = { (int)floor(player.GetPos().x), (int)floor(player.GetPos().y), (int)floor(player.GetPos().z)};

		// Select block
		if (!bIsPaused && shouldUpdateCamera && GetMouseButton(Mouse::MIDDLE).bPressed && m_currentHit.hit)
		{
			BlockType picked = world.GetBlock(raycastHitPos.x, raycastHitPos.y, raycastHitPos.z);
			inventory.AddBlock(picked);
		}

		// Break block
		if (!bIsPaused && shouldUpdateCamera && GetMouseButton(Mouse::LEFT).bPressed && m_currentHit.hit)
		{
			bool isBlockBreakValid = world.BreakBlock(m_currentHit);

			if (isBlockBreakValid)
			{	
				world.GetWorldPhysics().NotifyBlockRemoved(raycastHitPos.x, raycastHitPos.y, raycastHitPos.z);
				world.GetLightingSystem().NotifyBlockRemoved(raycastHitPos.x, raycastHitPos.y, raycastHitPos.z);
			}
		}

		// Place block
		if (!bIsPaused && shouldUpdateCamera && GetMouseButton(Mouse::RIGHT).bPressed && (playerPos != raycastPlacePos) && (glm::ivec3(playerPos.x, playerPos.y + 1, playerPos.z) != raycastPlacePos) && m_currentHit.hit)
		{
			bool isBlockPlaceValid = world.PlaceBlock(m_currentHit, inventory.GetHeldBlock());

			if (isBlockPlaceValid)
			{
				world.GetWorldPhysics().NotifyBlockRemoved(raycastPlacePos.x, raycastPlacePos.y, raycastPlacePos.z);

				if (inventory.GetHeldBlock() == BlockType::WATER)
					world.GetWorldPhysics().NotifyWaterSourcePlaced(raycastPlacePos.x, raycastPlacePos.y, raycastPlacePos.z);

				world.GetLightingSystem().NotifyBlockPlaced(raycastPlacePos.x, raycastPlacePos.y, raycastPlacePos.z, inventory.GetHeldBlock());
			}
		}

		// Update world physics & world lighting
		world.GetWorldPhysics().Update(dt);
		world.GetLightingSystem().Update();

		// ───── Rendering ───────────────────────────────────────────────
		// Update chunk streaming state based on player position:
		// - Enqueue new chunks for generation within view distance
		// - Identify chunks outside unload distance for removal
		// - Maintains streaming window around the player
		world.UpdateChunkStreaming(player.GetPos());

		// Promote fully generated chunks from staging into the main world: 
		// - Transfers ownership into `chunks` map (main thread)
		// - Ensures chunks become visible/usable only after complete generation
		world.CommitGeneratedChunks();

		// Synchronize CPU-side world state with GPU rendering:
		// - Enqueue dirty chunks for meshing
		// - Upload completed mesh data to GPU buffers
		// - Remove meshes for unloaded chunks
		world.SyncRenderer();

		// Check for any errors
		Debug(dt, world);

		// Highlight targeted block
		chunkMeshShader.use();
		if (m_currentHit.hit)
		{
			chunkMeshShader.setBool("u_isSelected", true);
			chunkMeshShader.setIvec3("u_selectedBlock", m_currentHit.blockPos);
		}
		else
			chunkMeshShader.setBool("u_isSelected", false);

		// Toggle AO
		chunkMeshShader.setBool("u_isAOEnabled", isAOEnabled);

		// Draw world
		glPolygonMode(GL_FRONT_AND_BACK, shouldDrawAsWireframe ? GL_LINE : GL_FILL);
		world.DrawAll(matProjection, camera.getLookAt());
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // restore for UI/other draws

		// Draw chunk boundaries if enabled
		chunkDebug.DrawChunkBoundary(camera.position);

		// Coordinate axis
		RenderAxis();

		// ── UI pass ────────────────────────────────────────
		// Disable writing & using the depth buffer
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Crosshair
		RenderCrosshair();

		// Hotbar
		uiRenderer.DrawHotbar();

		// Hotbar icons
		uiRenderer.DrawHotbarIcons(inventory);

		// Hotbar selector
		uiRenderer.DrawHotbarCursor(inventory.GetHotbarIndex());

		// Inventory
		if (inventory.IsOpen())
		{
			// Draw inventory screen
			uiRenderer.DrawInventory();

			// Draw items
			uiRenderer.DrawInventoryIcons(inventory);

			// Get inventory slot under mouse cursor
			int inventoryMouseHoverIndex = UIRenderer::GetMouseInventorySlot(GetMousePosX(), ScreenHeight() - GetMousePosY());
			if (inventoryMouseHoverIndex != -1)
			{
				glm::vec2 highlightPos = uiRenderer.GetInventorySlotPos(inventoryMouseHoverIndex);
				
				// Highlight the slot under the mouse cursor
				uiRenderer.DrawHighlightRect(highlightPos.x, highlightPos.y, 32, 32, glm::vec4(0.7f, 0.7f, 0.7f, 0.6f));

				// Remove item if Q selected while hovering over inventory slot
				if (GetKey('Q').bPressed)
				{
					if (!inventory.GetDragItem().IsEmpty())
						inventory.ClearHeld();
					else
						inventory.RemoveFromSlot(inventoryMouseHoverIndex);
				}
			}

			float mouseX = GetMousePosX();
			float mouseY = ScreenHeight() - GetMousePosY();

			// If item clicked while inventory is open
			if (GetMouseButton(Mouse::LEFT).bPressed)
			{
				int slot = UIRenderer::GetMouseInventorySlot(mouseX, mouseY);
				if (slot >= 0) inventory.ClickSlot(slot);
			}

			// Draw held item above all
			uiRenderer.DrawHeldItem(inventory, mouseX, mouseY);
		}

		// Write and use the depth buffer
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);	// Disable alpha blending

		// Bind back to the default framebuffer & draw quad keeping the texture rendered in the custom framebuffer bounded
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

		quadVAO.bind();
		framebufferShader.use();
		framebufferShader.setInt("uScreenTexture", 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textureColorBuffer);

		glDrawArrays(GL_TRIANGLES, 0, 6);

		// ───── ImGui ───────────────────────────────────────────────

		glm::ivec2 playerChunk = World::ChunkCoord(player.GetPos().x, player.GetPos().z);
		glm::ivec3 playerLocalChunk = World::ChunkLocalCoord(player.GetPos().x, player.GetPos().y, player.GetPos().z);
		BlockType raycastHitBlock = world.GetBlock(raycastHitPos.x, raycastHitPos.y, raycastHitPos.z);

		ImGui::Begin("Debug Console");
		ImGui::Text("Hello World!");
		ImGui::Text("Player Position: %d %d %d", (int)player.GetPos().x, (int)player.GetPos().y, (int)player.GetPos().z);
		ImGui::Text("Currently at chunk: %d %d", playerChunk.x, playerChunk.y);
		ImGui::Text("Local chunk coord: %d %d", playerLocalChunk.x, playerLocalChunk.z);

		ImGui::Text("Selected block: %s", GetDef(inventory.GetHeldBlock()).name);
		ImGui::Text("Raycast hit block: %s", GetDef(raycastHitBlock).name);
		ImGui::Text("Raycast place position: %d %d %d", raycastPlacePos.x, raycastPlacePos.y, raycastPlacePos.z);
		ImGui::Text("Chunk borders (G to toggle): %s", chunkDebug.visible ? "Enabled" : "Disabled");
		ImGui::Text("AO: %s", isAOEnabled ? "Enabled" : "Disabled");

		static int teleportX = 0;
		static int teleportY = 0;
		static int teleportZ = 0;

		ImGui::InputInt("X", &teleportX);
		ImGui::InputInt("Y", &teleportY);
		ImGui::InputInt("Z", &teleportZ);

		if (ImGui::Button("Teleport"))
			player.SetPos(glm::vec3(teleportX, teleportY, teleportZ));

		ImGui::End();
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		return true;
	}

	void UserControls(float dt)
	{
		// Fly & jump mechanics
		{
			if (GetKey(GLFW_KEY_SPACE).bPressed)
			{
				if (waitingForSecondTap)
				{
					if (spaceTimer <= DOUBLE_TAP_WINDOW)
					{
						// Double
						player.ToggleFly();

						waitingForSecondTap = false;
						spaceTimer = DOUBLE_TAP_WINDOW + 1.0f;	// invalidate
					}
					else
					{
						// Too late -> restart as first tap
						spaceTimer = 0.0f;
					}
				}
				else
				{
					// First tap
					waitingForSecondTap = true;
					spaceTimer = 0.0f;
				}
			}

			// Fly toggle - timer update
			if (waitingForSecondTap)
			{
				spaceTimer += dt;

				if (spaceTimer > DOUBLE_TAP_WINDOW)
				{
					waitingForSecondTap = false;
				}
			}

			// Temp
			if(GetKey(GLFW_KEY_RIGHT_CONTROL).bPressed)
			{
				glm::vec3 pos = player.GetPos();
				player.SetPos(glm::vec3(pos.x, pos.y + 100.0f, pos.z));
			}
		}

		// Toggle rendering chunk borders
		if (GetKey('G').bPressed)
			chunkDebug.visible = !chunkDebug.visible;

		// Toggle player inventory
		if (GetKey('E').bPressed)
		{
			// If inventory closes, dump the held item on the inventory
			if (inventory.IsOpen())
				inventory.Dump();

			// Toggle open and close
			inventory.Toggle();

			// If inventory is closed, make last mouse coords as
			// current mouse coords to avoid jump spikes
			if (!inventory.IsOpen())
			{
				camera.fLastX = (float)GetMousePosX();
				camera.fLastY = (float)GetMousePosY();
			}

			shouldUpdateCamera = !shouldUpdateCamera;
		}

		// Remove item from hotbar
		if (GetKey('Q').bPressed && !inventory.IsOpen())
			inventory.RemoveFromSlot(inventory.GetHotbarIndex());

		// Scroll hotbar cursor
		if (GetMouseScroll() == Mouse::SCROLL_DOWN)
			inventory.Scroll(1);
		else if (GetMouseScroll() == Mouse::SCROLL_UP)
			inventory.Scroll(-1);

		// Toggle draw as wireframes or entire block
		if (GetKey('J').bPressed)
			shouldDrawAsWireframe = !shouldDrawAsWireframe;

		// Toggle AO
		if (GetKey('H').bPressed)
			isAOEnabled = !isAOEnabled;

		RequestCursor(bIsPaused || inventory.IsOpen());
	}

	void InitShaders()
	{
		chunkMeshShader.use();
		chunkMeshShader.setVec3("u_lightDir", glm::vec3(0.0f, -1.0f, 0.0f));
		chunkMeshShader.setVec3("u_ambient", glm::vec3(0.25f));
		chunkMeshShader.setVec3("u_diffuse", glm::vec3(0.7f));
	}

	void Debug(float dt, const World& world)
	{
		static float fDebugTimer = 0.5f;

		// Prints on the screen every 500ms
		// Nothing as of now - be happy!
		fDebugTimer += dt;

		if (fDebugTimer >= 0.5f)
		{
			fDebugTimer = 0.0f;
		}
	}

	void RenderAxis()
	{
		/*
		* Rendering process
		*	1) Bind shader
		*   2) Bind vertex array
		*   3) Activate and use shaders
		*	4) Set uniforms in shaders
		*	5) Compute model matrix
		*	6) Call glDrawElements() or glDrawArrays() to draw
		*/

		axesShader.use();
		axesVAO.bind();

		glm::mat4 matModel = glm::mat4(1.0f);
		matModel = glm::scale(matModel, glm::vec3(5.0f, 5.0f, 5.0f));
		axesShader.setMat4("matModel", matModel);

		// Increase line width
		glLineWidth(2.0f);

		// Draw axes lines
		axesShader.setVec3("uColor", 1.0f, 0.0f, 0.0f);
		glDrawArrays(GL_LINES, 0, 2);
		axesShader.setVec3("uColor", 0.0f, 1.0f, 0.0f);
		glDrawArrays(GL_LINES, 2, 2);
		axesShader.setVec3("uColor", 0.0f, 0.0f, 1.0f);
		glDrawArrays(GL_LINES, 4, 2);

		// Set line width back to normal
		glLineWidth(1.0f);
	}

	void RenderCrosshair()
	{
		glDisable(GL_DEPTH_TEST);
		glLineWidth(2.0f);

		crosshairShader.use();
		crosshairVAO.bind();
		crosshairShader.setFloat("uAspect", static_cast<float>(ScreenWidth()) / static_cast<float>(ScreenHeight()));

		glDrawArrays(GL_LINES, 0, 4);
		glEnable(GL_DEPTH_TEST);

		glLineWidth(1.0f);
	}

	void Destroy() override
	{
		if(inventory.IsOpen())
			inventory.Dump();

		if (!inventory.Save())
			std::cerr << "Error saving player inventory.\n";

		world.StopAllWorkers();			// Stop all threads
		world.UnloadChunks();			// Unload all chunks

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		glDeleteFramebuffers(1, &framebuffer);
		glDeleteRenderbuffers(1, &rbo);
		glDeleteTextures(1, &textureColorBuffer);

		// Check for any errors - debug
		ErrorLog("Destroy()");

		std::cout << "\nDuration: " << std::fixed << std::setprecision(2) << fTimeSinceStart << 's' << std::endl;
	}
};

int main()
{
	Window window;
	window.ConstructWindow(1200, 675, "OpenGL");
	window.Start();

	std::cout << "Goodbye!" << std::endl;

	return 0;
}

/*
* TBA in Github:
* https://www.reddit.com/r/gamedev/comments/2iru8i/fast_flood_fill_lighting_in_a_blocky_voxel_game/
* https://0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/
* https://learnopengl.com/
*/