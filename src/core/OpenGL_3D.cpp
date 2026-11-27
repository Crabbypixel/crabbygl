#include "core/OpenGL_3D.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <imgui.h>
#include <imgui_impl_glfw.h>

ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int keycode, int scancode);

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

void OpenGL_3D::RendererThread()
{
	// We want the OpenGL context to be in the renderer thread (important!)
	glfwMakeContextCurrent(window);

	if (!Setup())
		m_bIsRunning = false;

	m_frameLast = std::chrono::steady_clock::now();

	// Update projection matrix
	UpdateProjectionMatrix();

	// Run as fast as possible
	while (m_bIsRunning)
		Frame();

	// Calls derived virtual function
	Destroy();

	// Not in destructor since by then the context is dead
	glDeleteBuffers(1, &uboMatrices);

	// Give the window context back to the main thread
	glfwMakeContextCurrent(nullptr);
}

// Runs every frame
bool OpenGL_3D::Frame()
{
	// FPS calculation
	auto now = std::chrono::steady_clock::now();
	std::chrono::duration<float> elapsedTime = now - m_frameLast;
	m_frameLast = now;

	float fElapsedTime = elapsedTime.count();
	fTimeSinceStart += fElapsedTime;

	// Capture mouse positions into a per-frame stable snapshot and query from this
	// GetMousePosX() and GetMousePosY() return this stable snapshot
	m_mousePosXFrame = m_mousePosX.load(std::memory_order_relaxed);
	m_mousePosYFrame = m_mousePosY.load(std::memory_order_relaxed);

	// Capture scroll atomic once per frame into a stable snapshot
	// This also resets the atomic to 0, so the main thread can update it for the next frame without worrying about synchronization
	m_mouseScrollFrame = m_mouseScroll.exchange(0, std::memory_order_relaxed);

	// Capture key buffer to thread-local key buffer
	bool localKeyRaw[MAX_KEYS];
	{
		std::lock_guard<std::mutex> lock(m_keyMutex);
		std::memcpy(localKeyRaw, m_keyRaw, sizeof(m_keyRaw));
	}

	// Update key and mouse states on each frame, they may be used later by the programmer
	// 1. Update key states
	for (int i = 0; i < MAX_KEYS; ++i)
	{
		m_keyNewState[i] = localKeyRaw[i];		// Use the raw state (captured by the main thread callback)

		m_keys[i].bPressed = false;
		m_keys[i].bReleased = false;

		if (m_keyNewState[i] != m_keyOldState[i])
		{
			if (m_keyNewState[i])
			{
				m_keys[i].bPressed = !m_keys[i].bHeld;
				m_keys[i].bHeld = true;
			}
			else
			{
				m_keys[i].bReleased = true;
				m_keys[i].bHeld = false;
			}
		}

		m_keyOldState[i] = m_keyNewState[i];
	}

	// 2. Update mouse states
	for (int i = 0; i < MAX_MOUSE_BUTTONS; ++i)
	{
		// Drain the atomic button held states into the new state array for processing
		m_mouseNewState[i] = m_bMouseButtonHeld[i].load(std::memory_order_relaxed);

		m_mouse[i].bPressed = false;
		m_mouse[i].bReleased = false;

		if (m_mouseNewState[i] != m_mouseOldState[i])
		{
			if (m_mouseNewState[i])
			{
				m_mouse[i].bPressed = !m_mouse[i].bHeld;
				m_mouse[i].bHeld = true;
			}
			else
			{
				m_mouse[i].bReleased = true;
				m_mouse[i].bHeld = false;
			}
			m_mouseOldState[i] = m_mouseNewState[i];
		}
	}

	// Pause/resume the renderer
	if (GetKey(GLFW_KEY_P).bPressed)
	{
		bIsPaused = !bIsPaused;

		if (bIsPaused)
		{
			shouldUpdateCamera = false;

			std::cout << "Engine: paused\n";
		}
		else
		{
			shouldUpdateCamera = true;

			camera.fLastX = (float)GetMousePosX();
			camera.fLastY = (float)GetMousePosY();
			std::cout << "Engine: unpaused\n";
		}
	}

	if (!Update(fElapsedTime))
	{
		m_bIsRunning = false;
	}

	// Control inputs - Change Projection and View matrices & handle keyboard inputs
	UpdateCameraControls(fElapsedTime);

	// FPS calculation
	++m_fpsFrameCount;
	m_fpsAccumulatedTime += fElapsedTime;

	// Update FPS every 0.5 seconds
	if (m_fpsAccumulatedTime >= 0.5f)
	{
		int fps = (int)(m_fpsFrameCount / m_fpsAccumulatedTime);

		if (window)
		{
			snprintf(m_titleBuf, 32, "%s : %d FPS", m_sAppName.c_str(), fps);
			m_titleDirty.store(true, std::memory_order_release);
		}

		m_fpsAccumulatedTime = 0.0f;
		m_fpsFrameCount = 0;
	}

	// Swap buffers, this GLFW function is thread-safe to call from renderer thread
	// web: no-op, the browser presents when the rAF callback returns
	glfwSwapBuffers(window);

	return m_bIsRunning;
}


void OpenGL_3D::UpdateCameraControls(float fElapsedTime)
{
	if (shouldUpdateCamera)
	{
		if (GetKey('C').bHeld)
		{
			if (fFov > 10.0f)
				fFov -= fElapsedTime * 200.0f;
			UpdateProjectionMatrix();
		}

		// Zoom out if the 'C' key is released
		else if (GetKey('C').bReleased)
		{
			fFov = 80.0f;
			UpdateProjectionMatrix();
		}

		if (GetKey(GLFW_KEY_HOME).bPressed)
			camera.Init(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, -1.0f), ScreenWidth(), ScreenHeight());

		/* ------------------------------------------ - Mouse Control - ------------------------------------------ */
		camera.ProcessMouse(GetMousePosX(), GetMousePosY(), ScreenWidth(), ScreenHeight(), bFirstMouse.load(std::memory_order_relaxed));
	}

	// If both camera updates and pause are disabled, use the last mouse position to update the view matrix, so that the camera doesn't "jump" when resuming
	if (!shouldUpdateCamera && !bIsPaused)
		camera.ProcessMouse(camera.fLastX, camera.fLastY, ScreenWidth(), ScreenHeight(), bFirstMouse.load(std::memory_order_relaxed));

	UpdateViewMatrix();
}

void OpenGL_3D::UpdateProjectionMatrix()
{
	matProjection = glm::perspective(fFov * PI / 180.0f, (float)ScreenWidth() / (float)ScreenHeight(), 0.1f, 1000.0f);
	glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(matProjection));
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void OpenGL_3D::UpdateViewMatrix()
{
	glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
	glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(camera.getLookAt()));
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void OpenGL_3D::ConstructWindow(int width, int height, std::string windowName)
{
	m_sAppName = windowName;
	m_width = width;
	m_height = height;

	// Initialize GLFW and initialize OpenGL to version 3.3
	// Without this GLFW failures are silent; the reason (no display, no matching GL visual, ...) is only reported here
	glfwSetErrorCallback([](int code, const char* desc) { std::cerr << "GLFW error " << code << ": " << desc << '\n'; });
	if (!glfwInit())
		Error("glfwInit failed");

#ifdef __EMSCRIPTEN__
	// WebGL2 == GLES 3.0. A "3.3 core" request has no meaning in the browser.
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

	// Make the window non-resizable
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	glfwWindowHint(GLFW_SAMPLES, 4);			// MSAA
	//glfwWindowHint(GLFW_DEPTH_BITS, 24);		// Request a 32-bit depth buffer

	// Create a window
	window = glfwCreateWindow(m_width, m_height, m_sAppName.c_str(), NULL, NULL);
	if (window == NULL)
		Error("Failed to create window.");

	// Set window position on screen
	glfwSetWindowPos(window, 360, 75);

	// Make the window to in the current context
	glfwMakeContextCurrent(window);

	// Disable cursor
	glfwSetCursorPos(window, m_width / 2.0f, m_height / 2.0f);

	// Disable V-Sync (to achieve 60+ fps)
	// Comment this out to get 60 fps (max)
#ifndef __EMSCRIPTEN__
	// Web: frame pacing is requestAnimationFrame (set by emscripten_set_main_loop);
	// calling this before the main loop exists only logs a warning.
	glfwSwapInterval(0);

	// Load all function pointers using GLAD. Web links WebGL2 directly, nothing to load.
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		Error("Failed to initialize GLAD");
#endif

	// Set viewport and callback function when window gets resized 
	glViewport(0, 0, m_width, m_height);

	// Enable z-buffer
	glEnable(GL_DEPTH_TEST);          // Enable depth testing
	glDepthFunc(GL_LESS);

	// Enable multi-sampling (usually enabled, good to enable it ourselves anyways)
#ifndef __EMSCRIPTEN__
	// Not a GLES/WebGL2 enum (GL_INVALID_ENUM). On web MSAA is the canvas `antialias`
	// attribute, which GLFW_SAMPLES above already requests.
	glEnable(GL_MULTISAMPLE);
#endif

	// Make the image loading library flip textures on load by default since 
	// OpenGL's texture coordinate system has the y-axis going 
	// upwards, while images usually have it downwards
	stbi_set_flip_vertically_on_load(true);

	// Display GPU info
	DisplayGPU();
}

void OpenGL_3D::Start()
{
	m_bIsRunning = true;

	// Will be transferred to the renderer thread, so that the callbacks can access the OpenGL_3D instance
	// For now the main thread will handle the callbacks to initialize the mouse position and scroll wheel until the renderer thread is ready to take over
	glfwSetWindowUserPointer(window, this);

	// Set callbacks
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSetCharCallback(window, char_callback);
	glfwSetWindowFocusCallback(window, focus_callback);

	/* ImGui platform backend lives on the main thread : Init / Shutdown call GLFW functions that are
	 * main-thread-only (error callback, cursor creation). install_callbacks=false: our callbacks
	 * above queue events instead (see DrainUiEvents). Renderer thread still owns the GL backend.
	 */
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

#ifdef __EMSCRIPTEN__
	/* Web has no renderer thread. The browser main thread must return to the event loop
	 * every frame, so both desktop while-loops become one rAF callback (WebTick).
	 * The GL context stays current on this thread, where WebGL requires it to live.
	 * World's workers are still real pthreads (PTHREAD_POOL_SIZE pre-spawns them,
	 * which matters because Setup() creates them before the first yield). */

	// rAF callback is the function passed to window.requestAnimationFrame()
	// that runs just before the browser performs its next screen repaint.
	if (!Setup())
	{
		std::cerr << "Setup() failed\n";
		return;
	}
	UpdateProjectionMatrix();
	m_frameLast = std::chrono::system_clock::now();

	m_lastCursorVisible = m_cursorVisible.load(std::memory_order_relaxed);
	emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true, &OpenGL_3D::PointerLockChanged);

	/* A deferred pointer-lock request only runs inside an Emscripten html5 event handler
	 * (GLFW's own DOM listeners don't count). These no-op handlers exist so a click on the
	 * canvas or any key press flushes the queued request while the browser still treats it
	 * as a user gesture. EM_FALSE: don't swallow the event, GLFW still receives it. */
	emscripten_set_mousedown_callback("#canvas", nullptr, true,
		[](int, const EmscriptenMouseEvent*, void*) -> EM_BOOL { return EM_FALSE; });
	emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true,
		[](int, const EmscriptenKeyboardEvent*, void*) -> EM_BOOL { return EM_FALSE; });

	ApplyCursorMode(m_lastCursorVisible);	// queued: first click/key on the page captures the mouse

	// fps=0 -> requestAnimationFrame (vsync). simulate_infinite_loop=true -> this call never
	// returns, so main()'s stack (the Window object, World, ...) stays alive for the page's life.
	emscripten_set_main_loop_arg(&OpenGL_3D::WebTick, this, 0, true);
#else

	// Make the window context null before calling the renderer thread
	glfwMakeContextCurrent(nullptr);

	// Start the renderer
	std::thread rendererThread = std::thread(&OpenGL_3D::RendererThread, this);

	m_lastCursorVisible = m_cursorVisible.load(std::memory_order_relaxed);
	ApplyCursorMode(m_lastCursorVisible);

	// While the renderer thread is running, the main thread handles poll events
	while (m_bIsRunning)
		MainThreadTick();

	// Wait until the renderer thread exits
	if (rendererThread.joinable())
		rendererThread.join();

	// Cleanup functions
	// Renderer has shut down the GL backend (Destroy) before exiting
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
#endif
}

#ifdef __EMSCRIPTEN__
void OpenGL_3D::WebTick(void* p)
{
	auto* self = static_cast<OpenGL_3D*>(p);

	// Same order as desktop: main thread publishes input, renderer consumes it.
	// Here both run on the browser main thread, so the key mutex / atomics are
	// uncontended but still correct, and no new threading invariant is introduced.
	self->MainThreadTick();

	if (!self->Frame())
	{
		// Mirrors the tail of RendererThread(). Practically unreachable on web (no ESC/close
		// quit), kept so an Update() returning false still flushes saves and joins workers.
		emscripten_cancel_main_loop();
		self->Destroy();
		glDeleteBuffers(1, &self->uboMatrices);
		ImGui::DestroyContext();
	}
}
#endif

void OpenGL_3D::MainThreadTick()
{
	if (m_titleDirty.exchange(false, std::memory_order_acquire))
		glfwSetWindowTitle(window, m_titleBuf);

#ifndef __EMSCRIPTEN__
	// ESC = quit on desktop, web: ESC belongs to the browser (releases pointer lock)
	// and a tab has no "quit", so it must not stop the engine there.
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
	{
		m_bIsRunning = false;
		glfwSetWindowShouldClose(window, true);
	}
#endif

	// Only change when a state mutation occurs
	bool currentCursorVisible = m_cursorVisible.load(std::memory_order_relaxed);
	if (currentCursorVisible != m_lastCursorVisible)
	{
		ApplyCursorMode(currentCursorVisible);
		m_lastCursorVisible = currentCursorVisible;
	}

#ifndef __EMSCRIPTEN__
	// Initiate shutdown when window is closed
	if (glfwWindowShouldClose(window))
		m_bIsRunning = false;
#endif

	glfwPollEvents();	// web: no-op, callbacks already fired from DOM events before this tick
	PollKeys();
}

void OpenGL_3D::ApplyCursorMode(bool visible)
{
#ifdef __EMSCRIPTEN__
	/* Browsers grant pointer lock only inside a user-gesture handler (click/key),this runs 
	 * from the rAF tick, so a direct request is rejected. DeferUntilInEventHandler=1
     * queues it and Emscripten performs it inside the next click/keydown on the page. */
	if (visible)
		emscripten_exit_pointerlock();
	else
		emscripten_request_pointerlock("#canvas", 1);
#else
	glfwSetInputMode(window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
#endif
}

#ifdef __EMSCRIPTEN__
EM_BOOL OpenGL_3D::PointerLockChanged(int, const EmscriptenPointerlockChangeEvent* e, void* p)
{
	auto* self = static_cast<OpenGL_3D*>(p);

	// ESC (or alt-tab) makes the browser drop the lock without asking us. If the game still
	// wants a captured mouse, queue a new request: the next click on the canvas re-captures.
	if (!e->isActive && !self->m_lastCursorVisible)
		emscripten_request_pointerlock("#canvas", 1);
	return EM_TRUE;
}
#endif

void OpenGL_3D::ErrorLog(const std::string& str)
{
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR)
	{
		std::cerr << "OpenGL error - main: " << err << std::endl;
		std::cerr << "in: " << str << '\n';
	}
}

void OpenGL_3D::Error(const std::string& message)
{
	std::cerr << "Error: " << message << std::endl;

	// Destroy runs only if Setup() ran on renderer thread
	if(m_bIsRunning)
		Destroy();

	glfwTerminate();
	exit(EXIT_FAILURE);
}

void OpenGL_3D::PollKeys()
{
	std::lock_guard<std::mutex> lock(m_keyMutex);
	// Codes below GLFW_KEY_SPACE aren't keys; glfwGetKey rejects them with GLFW_INVALID_ENUM
	for (int i = GLFW_KEY_SPACE; i < MAX_KEYS; ++i)
		m_keyRaw[i] = (glfwGetKey(window, i) == GLFW_PRESS);
}

// Callback functions for GLFW
void OpenGL_3D::mouse_callback(GLFWwindow* window, double xPos, double yPos)
{
	OpenGL_3D* instance = static_cast<OpenGL_3D*>(glfwGetWindowUserPointer(window));

	instance->m_mousePosX = (float)xPos;
	instance->m_mousePosY = (float)yPos;
	instance->bFirstMouse = false;

	// Queued a click must be replayed at the position it happened:
	// i.e. after the move that preceded it, or ImGui hit-tests last frame's position
	// Same for other callbacks
	instance->PushUiEvent({ .type = UiEvent::Type::CursorPos, .x = xPos, .y = yPos });
}

void OpenGL_3D::scroll_callback(GLFWwindow* window, double xOffset, double yOffset)
{
	OpenGL_3D* instance = static_cast<OpenGL_3D*>(glfwGetWindowUserPointer(window));

	instance->PushUiEvent({ .type = UiEvent::Type::Scroll, .x = xOffset, .y = yOffset });

	if ((int)yOffset == 1)
		instance->m_mouseScroll.store((int)Mouse::SCROLL_UP, std::memory_order_relaxed);
	else if ((int)yOffset == -1)
		instance->m_mouseScroll.store((int)Mouse::SCROLL_DOWN, std::memory_order_relaxed);
}

void OpenGL_3D::mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	OpenGL_3D* instance = static_cast<OpenGL_3D*>(glfwGetWindowUserPointer(window));
	if (button < MAX_MOUSE_BUTTONS)
		instance->m_bMouseButtonHeld[button].store(action != GLFW_RELEASE, std::memory_order_relaxed);

	instance->PushUiEvent({ .type = UiEvent::Type::MouseButton, .a = button, .b = action });
}

void OpenGL_3D::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	// Game keys are still polled in PollKeys(): this path feeds ImGui only
	if (action == GLFW_REPEAT)
		return;
	static_cast<OpenGL_3D*>(glfwGetWindowUserPointer(window))->PushUiEvent({ .type = UiEvent::Type::Key, .a = key, .b = scancode, .c = action, .d = mods });
}

void OpenGL_3D::char_callback(GLFWwindow* window, unsigned int codepoint)
{
	static_cast<OpenGL_3D*>(glfwGetWindowUserPointer(window))->PushUiEvent({ .type = UiEvent::Type::Char, .a = (int)codepoint });
}

void OpenGL_3D::focus_callback(GLFWwindow* window, int focused)
{
	static_cast<OpenGL_3D*>(glfwGetWindowUserPointer(window))->PushUiEvent({ .type = UiEvent::Type::Focus, .a = focused });
}

void OpenGL_3D::PushUiEvent(const UiEvent& e)
{
	std::lock_guard lock(m_uiEventMutex);
	m_uiEvents.push_back(e);
}

void OpenGL_3D::DrainUiEvents()
{
	m_uiEventsDrain.clear();
	{
		std::lock_guard lock(m_uiEventMutex);
		m_uiEventsDrain.swap(m_uiEvents);
	}

	ImGuiIO& io = ImGui::GetIO();
	for (const UiEvent& e : m_uiEventsDrain)
	{
		switch (e.type)
		{
			case UiEvent::Type::Key:
			{
				// Modifiers from the event itself: the backend's version calls glfwGetKey -> 
				// reads GLFW's key array from this thread while the main thread writes it
				io.AddKeyEvent(ImGuiMod_Ctrl, (e.d & GLFW_MOD_CONTROL) != 0);
				io.AddKeyEvent(ImGuiMod_Shift, (e.d & GLFW_MOD_SHIFT) != 0);
				io.AddKeyEvent(ImGuiMod_Alt, (e.d & GLFW_MOD_ALT) != 0);
				io.AddKeyEvent(ImGuiMod_Super, (e.d & GLFW_MOD_SUPER) != 0);
				io.AddKeyEvent(ImGui_ImplGlfw_KeyToImGuiKey(e.a, e.b), e.c == GLFW_PRESS);
				break;
			}
			case UiEvent::Type::Char:		 { io.AddInputCharacter((unsigned int)e.a); break; }
			case UiEvent::Type::CursorPos:	 { io.AddMousePosEvent((float)e.x, (float)e.y); break; }
			case UiEvent::Type::MouseButton: { io.AddMouseButtonEvent(e.a, e.b == GLFW_PRESS); break; }
			case UiEvent::Type::Scroll:		 { io.AddMouseWheelEvent((float)e.x, (float)e.y); break; }
			case UiEvent::Type::Focus:		 { io.AddFocusEvent(e.a != 0); break; }
		}
	}
}

// GPU Debug and Information
void OpenGL_3D::DisplayGPU()
{
	const char* renderer = (const char*)glGetString(GL_RENDERER);
	const char* vendor = (const char*)glGetString(GL_VENDOR);
	const char* version = (const char*)glGetString(GL_VERSION);
	const char* glslVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);

	std::cout << "========== GPU INFORMATION ==========\n\n";

	// Basic Info
	std::cout << "[Core]\n";
	std::cout << "Renderer        : " << renderer << '\n';
	std::cout << "Vendor          : " << vendor << '\n';
	std::cout << "OpenGL Version  : " << version << '\n';
	std::cout << "GLSL Version    : " << glslVersion << "\n\n";

	// Check if OpenGL context exists
	if (!renderer)
	{
		std::cout << "[ERROR] OpenGL not initialized!\n";
		return;
	}

	// Limits
	GLint maxTextureSize;
	GLint maxVertexAttribs;
	GLint maxUniforms;
	GLint maxDrawBuffers;

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &maxUniforms);
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);

	std::cout << "[Limits]\n";
	std::cout << "Max Texture Size       : " << maxTextureSize << '\n';
	std::cout << "Max Vertex Attribs     : " << maxVertexAttribs << '\n';
	std::cout << "Max Vertex Uniforms    : " << maxUniforms << '\n';
	std::cout << "Max Draw Buffers       : " << maxDrawBuffers << "\n\n";

	// Compute Shader Support
	GLint major = 0, minor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);

	bool computeSupported = (major > 4 || (major == 4 && minor >= 3));

	std::cout << "[Features]\n";
	std::cout << "Compute Shader Support : " << (computeSupported ? "YES" : "NO") << '\n';

	// VRAM (NVIDIA)
#ifdef GL_NVX_gpu_memory_info
	GLint totalMemKB = 0;
	GLint availMemKB = 0;

	glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemKB);
	glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &availMemKB);

	std::cout << "[Memory - NVIDIA]\n";
	std::cout << "Total VRAM (MB)        : " << totalMemKB / 1024 << '\n';
	std::cout << "Available VRAM (MB)    : " << availMemKB / 1024 << "\n\n";
#endif

#ifdef GL_ATI_meminfo
	GLint memInfo[4];
	glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, memInfo);

	std::cout << "[Memory - AMD]\n";
	std::cout << "Free Texture Memory MB : " << memInfo[0] / 1024 << "\n\n";
#endif

	// Extensions count
	GLint numExtensions = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);

	std::cout << "[Extensions]\n";
	std::cout << "Total Extensions       : " << numExtensions << '\n';

	std::cout << "\n=====================================\n\n";
}

OpenGL_3D::~OpenGL_3D()
{
}
