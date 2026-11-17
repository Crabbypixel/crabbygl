#pragma once

// IMPORTANT: GLAD must be included BEFORE GLFW
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// GLM (math library)
#include <glm/glm.hpp>

// Camera
#include "core/Camera.h"

// Image
#include "stb/stb_image.h"

#include <string>
#include <atomic>
#include <mutex>

// Constants
constexpr float PI = 3.14159f;

class OpenGL_3D
{
private:
	// Window width and height
	int m_width = 0;
	int m_height = 0;

	// Window title name
	std::string m_sAppName;

	// Atomic variable for running console
	std::atomic<bool> m_bIsRunning{ false };

	// Maximum number of keys supported in GLFW
	static constexpr int MAX_KEYS = GLFW_KEY_LAST;
	static constexpr int MAX_MOUSE_BUTTONS = 3;

	// Key state structure
	struct sKeyState
	{
		bool bPressed;
		bool bReleased;
		bool bHeld;
	} m_keys[MAX_KEYS] = {}, m_mouse[MAX_MOUSE_BUTTONS] = {};

	// Arrays to store key states
	short m_keyNewState[MAX_KEYS] = { 0 };
	short m_keyOldState[MAX_KEYS] = { 0 };

	// True when main thread finishes writing into the buffer and ready to swap
	std::mutex m_keyMutex;				// Locks access to m_keyRaw
	bool m_keyRaw[MAX_KEYS] = {};		// Main thread writes, renderer thread swap to local buffer using mutex

	// Mouse variables
	// Mouse button state
	short m_mouseOldState[MAX_MOUSE_BUTTONS] = { 0 };
	short m_mouseNewState[MAX_MOUSE_BUTTONS] = { 0 };

	// Mouse position - varies all the time
	std::atomic<float> m_mousePosX = 0.0f;
	std::atomic<float> m_mousePosY = 0.0f;

	// Per frame mouse positions as m_mousePos may vary per frame (as it is updated from main thread)
	float m_mousePosXFrame = 0.0f;
	float m_mousePosYFrame = 0.0f;

	// Mouse scroll wheel
	// Written by main-thread GLFW callbacks, read by renderer thread
	// Must be atomic to avoid undefined behavior and compiler register-caching
	std::atomic<int>  m_mouseScroll{ 0 };
	std::atomic<bool> m_bMouseButtonHeld[MAX_MOUSE_BUTTONS]{ false };  // per-button

	// Renderer-thread-only snapshot for m_mouseScroll, updated once per frame 
	// by exchange(0) to drain the atomic into a stable value for the frame
	// This can be read only once as reading this will cause it to reset to 0,
	// as the main thread can update the scroll at any time, we want to flush out asap
	int m_mouseScrollFrame = 0;

	// Atomic variable for cursor visibility - renderer writes, main reads
	std::atomic<bool> m_cursorVisible{ false };

	// For title string
	std::atomic<bool> m_titleDirty{ false };
	char m_titleBuf[64] = {};

protected:
	// GLFW window pointer
	GLFWwindow* window;

	// Mouse state structure
	enum class Mouse
	{
		LEFT = 0,
		RIGHT = 1,
		MIDDLE = 2,
		SCROLL_UP = 3,
		SCROLL_DOWN = 4
	};

	bool bIsPaused = false;				// Engine pause flag, disables camera and stops view/projection matrix updates
	bool shouldUpdateCamera = true;		// Set to false to disable camera controls and view/projection updates

	// Call from Update(), the atomic m_cursorVisible flag toggles cursor visibility
	// GLFW only allows cursor visibility to be changed from the main thread
	void RequestCursor(bool visible) noexcept { m_cursorVisible.store(visible, std::memory_order_relaxed); }

private:
	// Main renderer thread which constantly renders to the screen
	void RendererThread();

	// Update key and mouse states and update camera parameters
	void UpdateCameraControls(float fElapsedTime);

	// Update projection matrix UBOs
	void UpdateProjectionMatrix();

	// Update view matrix using UBOs
	void UpdateViewMatrix();

public:
	// No copying or moving - the window and OpenGL context should be unique and not duplicated
	OpenGL_3D(const OpenGL_3D&) = delete;
	OpenGL_3D(OpenGL_3D&&) = delete;
	OpenGL_3D& operator=(const OpenGL_3D&) = delete;
	OpenGL_3D& operator=(OpenGL_3D&&) = delete;

	float fTimeSinceStart = 0.0f;
	std::atomic<bool> bFirstMouse = true;

	Camera camera;
	glm::mat4 matProjection;
	float fFov = 80.0f;

	// Using a Uniform Buffer Object (UBO) to store the projection & view matrices 
	// in VRAM allows multiple shaders to access this matrix directly, 
	// eliminating the need for repeated CPU - GPU calls each time
	// The actual definition of uboMatrices is defined in Main.cpp
	unsigned int uboMatrices;

	[[nodiscard]] int ScreenWidth() const noexcept { return m_width; }
	[[nodiscard]] int ScreenHeight() const noexcept { return m_height; }
	[[nodiscard]] float GetMousePosX() const noexcept { return m_mousePosXFrame; }
	[[nodiscard]] float GetMousePosY() const noexcept { return m_mousePosYFrame; }
	[[nodiscard]] Mouse GetMouseScroll() const noexcept { return (Mouse)m_mouseScrollFrame; }
	[[nodiscard]] sKeyState GetMouseButton(Mouse button) const { return m_mouse[(int)button]; }
	[[nodiscard]] sKeyState GetKey(int nKeyID) const { return m_keys[nKeyID]; }

	OpenGL_3D() : window(nullptr), m_width(0), m_height(0) {}

	~OpenGL_3D();

	void ConstructWindow(int width, int height, std::string windowName);

	void Start();

	void ErrorLog(const std::string& str = "");

protected:
	// Virtual functions
	// Has to be overridden by subclasses
	virtual bool Setup() = 0;
	virtual bool Update(float fElapsedTime) = 0;

	// Optional to override
	virtual void Destroy() {}

	// Private functions
private:
	void Error(const std::string& message);
	void DisplayGPU();

	// Called from main thread - captures key strokes
	void PollKeys();

	// Callback functions used by GLFW
	static void mouse_callback(GLFWwindow* window, double xPos, double yPos);
	static void scroll_callback(GLFWwindow* window, double xOffset, double yOffset);
	static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
};