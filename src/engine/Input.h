#pragma once

#include <GLFW/glfw3.h>
#include <array>

namespace myth {

enum class KeyState { Released, Pressed, Held, JustReleased };

class Input {
public:
    static Input& instance();
    
    void init(GLFWwindow* window);
    void update();
    
    bool isKeyDown(int key) const;
    bool isKeyPressed(int key) const;
    bool isKeyReleased(int key) const;
    
    double mouseX() const { return m_mouseX; }
    double mouseY() const { return m_mouseY; }
    double mouseDeltaX() const { return m_mouseDeltaX; }
    double mouseDeltaY() const { return m_mouseDeltaY; }
    
    bool isMouseButtonDown(int button) const;
    void setMouseCapture(bool capture);
    
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

private:
    Input() = default;
    
    GLFWwindow* m_window = nullptr;
    std::array<KeyState, GLFW_KEY_LAST + 1> m_keys{};
    std::array<KeyState, GLFW_MOUSE_BUTTON_LAST + 1> m_mouseButtons{};
    
    double m_mouseX = 0, m_mouseY = 0;
    double m_lastMouseX = 0, m_lastMouseY = 0;
    double m_mouseDeltaX = 0, m_mouseDeltaY = 0;
    double m_scrollDelta = 0;
    bool m_firstMouse = true;
    bool m_captured = false;
};

} // namespace myth
