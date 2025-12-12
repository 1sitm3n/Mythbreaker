#include "Input.h"

namespace myth {

Input& Input::instance() {
    static Input inst;
    return inst;
}

void Input::init(GLFWwindow* window) {
    m_window = window;
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetScrollCallback(window, scrollCallback);
    
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    m_mouseX = m_lastMouseX = x;
    m_mouseY = m_lastMouseY = y;
}

void Input::update() {
    for (auto& state : m_keys) {
        if (state == KeyState::Pressed) state = KeyState::Held;
        else if (state == KeyState::JustReleased) state = KeyState::Released;
    }
    for (auto& state : m_mouseButtons) {
        if (state == KeyState::Pressed) state = KeyState::Held;
        else if (state == KeyState::JustReleased) state = KeyState::Released;
    }
    
    m_mouseDeltaX = m_mouseX - m_lastMouseX;
    m_mouseDeltaY = m_mouseY - m_lastMouseY;
    m_lastMouseX = m_mouseX;
    m_lastMouseY = m_mouseY;
    m_scrollDelta = 0;
}

bool Input::isKeyDown(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return m_keys[key] == KeyState::Pressed || m_keys[key] == KeyState::Held;
}

bool Input::isKeyPressed(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return m_keys[key] == KeyState::Pressed;
}

bool Input::isKeyReleased(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return m_keys[key] == KeyState::JustReleased;
}

bool Input::isMouseButtonDown(int button) const {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return m_mouseButtons[button] == KeyState::Pressed || m_mouseButtons[button] == KeyState::Held;
}

void Input::setMouseCapture(bool capture) {
    m_captured = capture;
    glfwSetInputMode(m_window, GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (capture) m_firstMouse = true;
}

void Input::keyCallback(GLFWwindow*, int key, int, int action, int) {
    if (key < 0 || key > GLFW_KEY_LAST) return;
    auto& inst = instance();
    if (action == GLFW_PRESS) inst.m_keys[key] = KeyState::Pressed;
    else if (action == GLFW_RELEASE) inst.m_keys[key] = KeyState::JustReleased;
}

void Input::mouseCallback(GLFWwindow*, double x, double y) {
    auto& inst = instance();
    if (inst.m_firstMouse && inst.m_captured) {
        inst.m_lastMouseX = x;
        inst.m_lastMouseY = y;
        inst.m_firstMouse = false;
    }
    inst.m_mouseX = x;
    inst.m_mouseY = y;
}

void Input::mouseButtonCallback(GLFWwindow*, int button, int action, int) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;
    auto& inst = instance();
    if (action == GLFW_PRESS) inst.m_mouseButtons[button] = KeyState::Pressed;
    else if (action == GLFW_RELEASE) inst.m_mouseButtons[button] = KeyState::JustReleased;
}

void Input::scrollCallback(GLFWwindow*, double, double y) {
    instance().m_scrollDelta = y;
}

} // namespace myth
