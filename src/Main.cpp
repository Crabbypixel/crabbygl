#include "rendering/GL.h"

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
#include "rendering/Sun.h"
#include "world/DayNightCycle.h"

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
	Shader skyShader;

	// Player
	Player player;

	// World
	World world;

	// Sky
	Sun sun;
	Sun moon;
	DayNightCycle dayCycle;
	float minLight = 0.3f;

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
		player.SetPos(glm::vec3(35.0f, 250.0f, 77.0f));
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

		// ----- World ------------------------------------------------
		chunkMeshShader.load("assets/shaders/ChunkMesh.glsl");

		world.SetChunkShader(chunkMeshShader);
		world.LoadAtlasTexture("assets/textures/textures.png");

		// ----- Sky, Sun & Moon ------------------------------------------------
		sun.Init("assets/shaders/Sun.glsl");
		moon.Init("assets/shaders/Sun.glsl");
		skyShader.load("assets/shaders/Sky.glsl");

		moon.size = 20.0f;
		moon.coreColor = glm::vec3(0.85f, 0.88f, 0.95f);	// Pale blue-white
		moon.color = glm::vec3(0.6f, 0.65f, 0.8f);			// Cool halo
		moon.haloStrength = 0.08f;

		// Spawn 4 threads for loading chunks and 4 threads for generating meshes
		// This is the optimal spot for good performance without overly increasing number of threads
		world.StartChunkLoadWorkers(4);
		world.StartMeshWorkers(4);

		// ----- Shaders ------------------------------------------------
		InitShaders();

		{
			// ----- Framebuffers ------------------------------------------------
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

			// ------------------------------------------------------

			// ----- UBOs ------------------------------------------------
			// UBOs to unnecessarily avoid settings uniforms in shaders repeatedly
			// Bind "Matrices" uniform to binding index 0 in every shader
			glUniformBlockBinding(axesShader.getID(), glGetUniformBlockIndex(axesShader.getID(), "Matrices"), 0);
			glUniformBlockBinding(chunkMeshShader.getID(), glGetUniformBlockIndex(chunkMeshShader.getID(), "Matrices"), 0);

			ErrorLog();

			// Same for the other side
			glGenBuffers(1, &uboMatrices);
			glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
			glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), NULL, GL_STATIC_DRAW);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);

			glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboMatrices);
			// ------------------------------------------------------
		}

		// ImGui context + GLFW backend are created on the main thread (OpenGL_3D::Start);
		// only the GL backend belongs here, where the context is current
#ifdef __EMSCRIPTEN__
		ImGui_ImplOpenGL3_Init("#version 300 es");
#else
		ImGui_ImplOpenGL3_Init("#version 330");
#endif

		// Enable transparency
		glEnable(GL_BLEND);

		return true;
	}

	bool Update(float dt) override
	{
		// ImGui - new frame settings
		ImGui_ImplOpenGL3_NewFrame();
		DrainUiEvents();
		// Replaces ImGui_ImplGlfw_NewFrame() : it polls glfwGetCursorPos / glfwGetInputMode from this (non - main)
		// thread, racing glfwPollEvents, all it provided is either already queued (input) or known here
		ImGuiIO & io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)ScreenWidth(), (float)ScreenHeight());	// Window is non-resizable
		io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;							// NewFrame asserts > 0
		ImGui::NewFrame();

		// Bind to the custom framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

		// Clear colorbuffer
		if (!bIsPaused)
			dayCycle.Update(dt);

		const glm::vec3 horizon = dayCycle.HorizonColor();
		glClearColor(horizon.r, horizon.g, horizon.b, 1.0f);

		// Default skycolor used before introducing day-night cycle
		//glClearColor(0.227f, 0.757f, 1.0f, 1.0f);

		// Clear depthbuffer and stencilbuffer
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		// Use the depth buffer
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);

		// Get user controls
		UserControls(dt);

		// ----- Physics ------------------------------------------------
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
			glm::ivec2 startPos = { 0, 0 };
			int height = 60;
			int c = 2;
			if (GetKey(GLFW_KEY_LEFT_CONTROL).bHeld && GetKey('U').bPressed)
			{
				for(int x = 0; x < c; ++x)
				{
					for(int z = 0; z < c; ++z)
						world.SetBlock(startPos.x + x, height, startPos.y + z, BlockType::GRAVEL);
				}
			}
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
				world.GetWorldPhysics().NotifyBlockChanged(raycastHitPos.x, raycastHitPos.y, raycastHitPos.z);
				world.GetLightingSystem().NotifyBlockRemoved(raycastHitPos.x, raycastHitPos.y, raycastHitPos.z);
			}
		}

		// Place block
		if (!bIsPaused && shouldUpdateCamera && GetMouseButton(Mouse::RIGHT).bPressed && (playerPos != raycastPlacePos) && (glm::ivec3(playerPos.x, playerPos.y + 1, playerPos.z) != raycastPlacePos) && m_currentHit.hit)
		{
			bool isBlockPlaceValid = world.PlaceBlock(m_currentHit, inventory.GetHeldBlock());

			if (isBlockPlaceValid)
			{
				world.GetWorldPhysics().NotifyBlockChanged(raycastPlacePos.x, raycastPlacePos.y, raycastPlacePos.z);
				world.GetLightingSystem().NotifyBlockPlaced(raycastPlacePos.x, raycastPlacePos.y, raycastPlacePos.z, inventory.GetHeldBlock());
			}
		}

		// Update world physics & world lighting
		if(!bIsPaused)
			world.GetWorldPhysics().Update(dt);

		world.GetLightingSystem().Update();

		// ----- Rendering ------------------------------------------------
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

		// ----- Sky ------------------------------------------------
		// Sky - Day/night: per-frame uniforms only, world/lighting state untouched
		const glm::vec3 lightDir = dayCycle.LightDir();
		//chunkMeshShader.setVec3("u_lightDir", lightDir);
		chunkMeshShader.setVec3("u_lightDir", glm::vec3(0.0f, -1.0f, 0.0f));
		chunkMeshShader.setFloat("u_skyFactor", dayCycle.SkyFactor());
		chunkMeshShader.setFloat("u_minLight", minLight);
		
		// Fog
		const float fogEnd = glm::max(static_cast<float>(world.GetViewDist() * CX - 24), 8.0f);
		chunkMeshShader.setVec3("u_cameraPos", camera.position);
		chunkMeshShader.setFloat("u_fogEnd", fogEnd);
		chunkMeshShader.setFloat("u_fogStart", fogEnd * 0.7f);
		chunkMeshShader.setVec3("u_zenithColor", dayCycle.ZenithColor());
		chunkMeshShader.setVec3("u_horizonColor", dayCycle.HorizonColor());
		chunkMeshShader.setVec3("u_sunDir", dayCycle.SunDir());
		chunkMeshShader.setVec4("u_sunsetColor", dayCycle.SunsetColor());

		// Sky gradient + sunset fan
		{
			// This converts camera space coords containing rotation but no translation
			const glm::mat4 skyView = glm::mat4(glm::mat3(camera.getLookAt()));

			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);

			skyShader.use();
			skyShader.setMat4("uInvViewProj", glm::inverse(matProjection * skyView));
			skyShader.setVec3("uZenithColor", dayCycle.ZenithColor());
			skyShader.setVec3("uHorizonColor", dayCycle.HorizonColor());
			skyShader.setVec3("uSunDir", dayCycle.SunDir());
			skyShader.setVec4("uSunsetColor", dayCycle.SunsetColor());
			skyShader.setFloat("uSkyAngle", dayCycle.CelestialAngle());
			skyShader.setFloat("uStarStrength", dayCycle.StarStrength());

			quadVAO.bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);

			glDepthMask(GL_TRUE);
			glEnable(GL_DEPTH_TEST);
		}

		// Draw sun & moon
		sun.Draw(camera.position, -dayCycle.SunDir());
		moon.Draw(camera.position, dayCycle.SunDir());		// Draw opposite of sun

		// Draw world
#ifndef __EMSCRIPTEN__
		// WebGL2 has no glPolygonMode: wireframe toggle is desktop-only (debug feature).
		glPolygonMode(GL_FRONT_AND_BACK, shouldDrawAsWireframe ? GL_LINE : GL_FILL);
#endif
		world.DrawAll(matProjection, camera.getLookAt());
#ifndef __EMSCRIPTEN__
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // restore for UI/other draws
#endif

		// Draw chunk boundaries if enabled
		chunkDebug.DrawChunkBoundary(camera.position);

		// Coordinate axis
		RenderAxis();

		// ----- UI pass ------------------------------------------------
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

		// ----- ImGui ------------------------------------------------

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
		ImGui::Text("Solar angle: %d degrees", static_cast<int>(std::round(dayCycle.CelestialAngle() * 180 / PI)));

		static int teleportX = 0;
		static int teleportY = 0;
		static int teleportZ = 0;

		ImGui::InputInt("X", &teleportX);
		ImGui::InputInt("Y", &teleportY);
		ImGui::InputInt("Z", &teleportZ);

		if (ImGui::Button("Teleport"))
			player.SetPos(glm::vec3(playerPos.x + teleportX, playerPos.y + teleportY, playerPos.z + teleportZ));

		ImGui::SliderFloat("Time of day", &dayCycle.time, 0.0f, 0.999f);
		ImGui::Checkbox("Pause time", &dayCycle.paused);
		ImGui::SliderFloat("Min light", &minLight, 0.05f, 1.0f);

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

		// Toggle daycycle update
		if (GetKey('L').bPressed)
			dayCycle.paused = !dayCycle.paused;

		RequestCursor(bIsPaused || inventory.IsOpen());
	}

	void InitShaders()
	{
		chunkMeshShader.use();
		// "u_lightDir" is now done by the day-night cycle
		chunkMeshShader.setVec3("u_ambient", glm::vec3(0.3f));
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
* To be added in Github:
* https://www.reddit.com/r/gamedev/comments/2iru8i/fast_flood_fill_lighting_in_a_blocky_voxel_game/
* https://0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/
* https://learnopengl.com/
*/