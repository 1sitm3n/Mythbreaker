#pragma once

#include <string>
#include <vector>

// Forward-declare std::function to avoid pulling <functional> near namespace myth
#include <functional>

namespace myth {

enum class GameState {
    Loading, MainMenu, Playing, Paused, Inventory,
    Dialogue, Settings, Dead, Cutscene, COUNT
};

inline const char* gameStateName(GameState s) {
    switch (s) {
        case GameState::Loading:   return "Loading";
        case GameState::MainMenu:  return "MainMenu";
        case GameState::Playing:   return "Playing";
        case GameState::Paused:    return "Paused";
        case GameState::Inventory: return "Inventory";
        case GameState::Dialogue:  return "Dialogue";
        case GameState::Settings:  return "Settings";
        case GameState::Dead:      return "Dead";
        case GameState::Cutscene:  return "Cutscene";
        default: return "Unknown";
    }
}

class GameStateMachine {
public:
    using TransitionCallback = ::std::function<void(GameState, GameState)>;

    void init(GameState initialState = GameState::Loading) {
        m_currentState = initialState;
        m_previousState = initialState;
    }

    GameState current() const { return m_currentState; }
    GameState previous() const { return m_previousState; }

    bool transitionTo(GameState newState) {
        if (newState == m_currentState) return false;
        if (!isValidTransition(m_currentState, newState)) return false;
        m_previousState = m_currentState;
        m_currentState = newState;
        for (auto& cb : m_onTransition) cb(m_previousState, m_currentState);
        return true;
    }

    void onTransition(TransitionCallback callback) {
        m_onTransition.push_back(::std::move(callback));
    }

    bool returnToPrevious() { return transitionTo(m_previousState); }

    bool isPlaying() const { return m_currentState == GameState::Playing; }

    bool isGameplayActive() const {
        return m_currentState == GameState::Playing || m_currentState == GameState::Dialogue;
    }

    bool shouldRenderWorld() const {
        return m_currentState != GameState::Loading && m_currentState != GameState::MainMenu;
    }

    bool shouldCaptureMouse() const { return m_currentState == GameState::Playing; }

    bool shouldShowCursor() const {
        return m_currentState == GameState::MainMenu || m_currentState == GameState::Paused ||
               m_currentState == GameState::Inventory || m_currentState == GameState::Settings ||
               m_currentState == GameState::Dead;
    }

    float getTimeScale() const {
        switch (m_currentState) {
            case GameState::Playing:  return 1.0f;
            case GameState::Dialogue: return 0.1f;
            default: return 0.0f;
        }
    }

private:
    bool isValidTransition(GameState from, GameState to) const {
        switch (from) {
            case GameState::Loading:   return to == GameState::MainMenu || to == GameState::Playing;
            case GameState::MainMenu:  return to == GameState::Playing || to == GameState::Settings || to == GameState::Loading;
            case GameState::Playing:   return to == GameState::Paused || to == GameState::Inventory || to == GameState::Dialogue || to == GameState::Dead || to == GameState::Cutscene || to == GameState::MainMenu;
            case GameState::Paused:    return to == GameState::Playing || to == GameState::Settings || to == GameState::MainMenu;
            case GameState::Inventory: return to == GameState::Playing;
            case GameState::Dialogue:  return to == GameState::Playing;
            case GameState::Settings:  return to == GameState::MainMenu || to == GameState::Paused;
            case GameState::Dead:      return to == GameState::Playing || to == GameState::MainMenu;
            case GameState::Cutscene:  return to == GameState::Playing;
            default: return false;
        }
    }

    GameState m_currentState = GameState::Loading;
    GameState m_previousState = GameState::Loading;
    ::std::vector<TransitionCallback> m_onTransition;
};

} // namespace myth
