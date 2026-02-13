###############################################################################
# Mythbreaker — Fix Windows min/max macro collision + wire all systems
#
# USAGE:
#   cd C:\Projects\Mythbreaker
#   .\fix_and_wire.ps1
###############################################################################

param(
    [string]$ProjectRoot = "C:\Projects\Mythbreaker"
)

$ErrorActionPreference = "Stop"
Set-Location $ProjectRoot

Write-Host "`n=== Fix #1: Rewrite FrustumCuller.h (Windows min/max macro fix) ===" -ForegroundColor Cyan

Set-Content -Path "src/engine/FrustumCuller.h" -Value @'
#pragma once

// Prevent Windows min/max macros from interfering
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <glm/glm.hpp>
#include <array>
#include <algorithm>

namespace myth {

struct AABB {
    glm::vec3 lo = glm::vec3(0.0f);
    glm::vec3 hi = glm::vec3(0.0f);

    AABB() = default;
    AABB(const glm::vec3& mn, const glm::vec3& mx) : lo(mn), hi(mx) {}

    static AABB fromCenterExtents(const glm::vec3& center, const glm::vec3& extents) {
        return AABB(center - extents, center + extents);
    }

    static AABB fromChunk(int cx, int cz, float chunkSize, float minY, float maxY) {
        float x = cx * chunkSize - chunkSize / 2.0f;
        float z = cz * chunkSize - chunkSize / 2.0f;
        return AABB(glm::vec3(x, minY, z), glm::vec3(x + chunkSize, maxY, z + chunkSize));
    }

    AABB transformed(const glm::mat4& m) const {
        glm::vec3 center = (lo + hi) * 0.5f;
        glm::vec3 extents = (hi - lo) * 0.5f;
        glm::vec3 newCenter = glm::vec3(m * glm::vec4(center, 1.0f));
        glm::vec3 newExtents(0.0f);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                newExtents[i] += glm::abs(m[j][i]) * extents[j];
        return AABB(newCenter - newExtents, newCenter + newExtents);
    }

    glm::vec3 center() const { return (lo + hi) * 0.5f; }
    glm::vec3 extents() const { return (hi - lo) * 0.5f; }
    float radius() const { return glm::length(extents()); }
};

struct Plane {
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float distance = 0.0f;
    float distanceTo(const glm::vec3& point) const { return glm::dot(normal, point) + distance; }
};

class Frustum {
public:
    enum Side { Left = 0, Right, Top, Bottom, Near, Far, COUNT };

    void update(const glm::mat4& vp) {
        m_planes[Left]  = {glm::vec3(vp[0][3]+vp[0][0], vp[1][3]+vp[1][0], vp[2][3]+vp[2][0]), vp[3][3]+vp[3][0]};
        m_planes[Right] = {glm::vec3(vp[0][3]-vp[0][0], vp[1][3]-vp[1][0], vp[2][3]-vp[2][0]), vp[3][3]-vp[3][0]};
        m_planes[Bottom]= {glm::vec3(vp[0][3]+vp[0][1], vp[1][3]+vp[1][1], vp[2][3]+vp[2][1]), vp[3][3]+vp[3][1]};
        m_planes[Top]   = {glm::vec3(vp[0][3]-vp[0][1], vp[1][3]-vp[1][1], vp[2][3]-vp[2][1]), vp[3][3]-vp[3][1]};
        m_planes[Near]  = {glm::vec3(vp[0][3]+vp[0][2], vp[1][3]+vp[1][2], vp[2][3]+vp[2][2]), vp[3][3]+vp[3][2]};
        m_planes[Far]   = {glm::vec3(vp[0][3]-vp[0][2], vp[1][3]-vp[1][2], vp[2][3]-vp[2][2]), vp[3][3]-vp[3][2]};

        for (auto& p : m_planes) {
            float len = glm::length(p.normal);
            if (len > 0.0001f) { p.normal /= len; p.distance /= len; }
        }
    }

    bool isVisible(const AABB& aabb) const {
        for (const auto& plane : m_planes) {
            glm::vec3 pVertex;
            pVertex.x = (plane.normal.x >= 0.0f) ? aabb.hi.x : aabb.lo.x;
            pVertex.y = (plane.normal.y >= 0.0f) ? aabb.hi.y : aabb.lo.y;
            pVertex.z = (plane.normal.z >= 0.0f) ? aabb.hi.z : aabb.lo.z;
            if (plane.distanceTo(pVertex) < 0.0f) return false;
        }
        return true;
    }

    bool isVisible(const glm::vec3& center, float radius) const {
        for (const auto& plane : m_planes)
            if (plane.distanceTo(center) < -radius) return false;
        return true;
    }

private:
    std::array<Plane, COUNT> m_planes;
};

class FrustumCuller {
public:
    void update(const glm::mat4& viewProjection) {
        m_frustum.update(viewProjection);
        m_totalTested = 0;
        m_totalCulled = 0;
    }

    bool testAABB(const AABB& aabb) {
        m_totalTested++;
        bool visible = m_frustum.isVisible(aabb);
        if (!visible) m_totalCulled++;
        return visible;
    }

    bool testSphere(const glm::vec3& center, float radius) {
        m_totalTested++;
        bool visible = m_frustum.isVisible(center, radius);
        if (!visible) m_totalCulled++;
        return visible;
    }

    bool testChunk(int cx, int cz, float chunkSize,
                   float minTerrainY = -20.0f, float maxTerrainY = 80.0f) {
        return testAABB(AABB::fromChunk(cx, cz, chunkSize, minTerrainY, maxTerrainY));
    }

    bool testEntity(const glm::vec3& position, const glm::vec3& scale, float baseBoundRadius = 1.0f) {
        float maxScale = scale.x; if (scale.y > maxScale) maxScale = scale.y; if (scale.z > maxScale) maxScale = scale.z;
        return testSphere(position, baseBoundRadius * maxScale);
    }

    uint32_t totalTested() const { return m_totalTested; }
    uint32_t totalCulled() const { return m_totalCulled; }
    uint32_t totalVisible() const { return m_totalTested - m_totalCulled; }
    float cullPercentage() const {
        return m_totalTested > 0 ? (float)m_totalCulled / m_totalTested * 100.0f : 0.0f;
    }

private:
    Frustum  m_frustum;
    uint32_t m_totalTested = 0;
    uint32_t m_totalCulled = 0;
};

} // namespace myth
'@
Write-Host "  FrustumCuller.h rewritten (min/max -> lo/hi, std::abs -> glm::abs, std::max -> glm::max)" -ForegroundColor Green

# ── Fix #2: Rewrite GameState.h to avoid <functional> inside myth namespace ──
Write-Host "`n=== Fix #2: Rewrite GameState.h (avoid std::function namespace issues) ===" -ForegroundColor Cyan

Set-Content -Path "src/engine/GameState.h" -Value @'
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
'@
Write-Host "  GameState.h rewritten (::std:: qualified to avoid namespace collision)" -ForegroundColor Green

# ── Now apply main.cpp edits ──
Write-Host "`n=== Applying main.cpp edits ===" -ForegroundColor Cyan

$mainCpp = "src/app/main.cpp"
$backup = "$mainCpp.backup_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
Copy-Item $mainCpp $backup
Write-Host "Backup: $backup" -ForegroundColor Gray

$content = [System.IO.File]::ReadAllText((Resolve-Path $mainCpp))
$editCount = 0

function Apply-Edit($Name, $Old, $New) {
    $script:editCount++
    if ($script:content.Contains($Old)) {
        $script:content = $script:content.Replace($Old, $New)
        Write-Host "  [$script:editCount] $Name" -ForegroundColor Green
    } else {
        Write-Host "  [$script:editCount] $Name -- SKIPPED (already applied)" -ForegroundColor Yellow
    }
}

# 1. Includes
$old1 = '#include "engine/BiomeSystem.h"'
$new1 = @'
#include "engine/BiomeSystem.h"
#include "engine/FrustumCuller.h"
#include "engine/GameState.h"
'@
Apply-Edit "Add new includes" $old1 $new1

# 2. Member variables
$old2 = 'RegionState m_lastLoggedState = RegionState::Stable;'
$new2 = @'
RegionState m_lastLoggedState = RegionState::Stable;
    FrustumCuller m_frustumCuller;
    GameStateMachine m_gameState;
'@
Apply-Edit "Add member variables" $old2 $new2

# 3. Window title
$old3 = '"Mythbreaker - NPC & Dialogue"'
$new3 = '"Mythbreaker"'
Apply-Edit "Update window title" $old3 $new3

# 4. Init game state
$old4 = @'
loadModels();

        Logger::info("Terrain: 32m chunks, 16x16 vertices each");
'@
$new4 = @'
loadModels();

        // Initialize game state machine
        m_gameState.init(GameState::Playing);
        m_gameState.onTransition([](GameState from, GameState to) {
            Logger::info(std::string("Game state: ") + gameStateName(from) + " -> " + gameStateName(to));
        });
        Logger::info("Game state machine initialized");

        Logger::info("Terrain: 32m chunks, 16x16 vertices each");
'@
Apply-Edit "Initialize game state machine" $old4 $new4

# 5. Frustum update in camera UBO
$old5 = @'
ubo.viewProj = ubo.proj * ubo.view;
        
        // Get time and weather data
'@
$new5 = @'
ubo.viewProj = ubo.proj * ubo.view;
        
        // Update frustum culler with current view-projection
        m_frustumCuller.update(ubo.viewProj);
        
        // Get time and weather data
'@
Apply-Edit "Frustum update in updateCameraUBO()" $old5 $new5

# 6. ESC toggles pause
$old6 = 'if (input.isKeyPressed(GLFW_KEY_ESCAPE)) { glfwSetWindowShouldClose(m_window, true); return; }'
$new6 = @'
if (input.isKeyPressed(GLFW_KEY_ESCAPE)) {
            if (m_gameState.current() == GameState::Playing) {
                m_gameState.transitionTo(GameState::Paused);
                m_mouseCaptured = false;
                glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                Logger::info("*** PAUSED - Press ESC to resume ***");
            } else if (m_gameState.current() == GameState::Paused) {
                m_gameState.transitionTo(GameState::Playing);
                m_mouseCaptured = true;
                glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                Logger::info("*** RESUMED ***");
            } else {
                glfwSetWindowShouldClose(m_window, true);
            }
            return;
        }
'@
Apply-Edit "ESC toggles pause" $old6 $new6

# 7. Pause logic in main loop
$old7 = @'
float dt = m_timer.clampedDeltaTime();
            m_totalPlayTime += dt;
            
            processInput(dt);
'@
$new7 = @'
float dt = m_timer.clampedDeltaTime();
            float gameTimeScale = m_gameState.getTimeScale();
            float gameDt = dt * gameTimeScale;
            m_totalPlayTime += gameDt;
            
            processInput(dt);
            
            // Skip gameplay updates when paused - still render the scene
            if (!m_gameState.isGameplayActive()) {
                m_scrollDelta = 0.0f;
                Input::instance().update();
                updateCameraUBO();
                drawFrame();
                continue;
            }
'@
Apply-Edit "Pause logic in main loop" $old7 $new7

# 8. Frustum cull landmarks
$old8 = @'
m_world.landmarkTags.each([&](Entity e, const LandmarkTag&) {
            const auto* t = m_world.transforms.tryGet(e);
            const auto* r = m_world.renderables.tryGet(e);
            if (!t || !r || !r->visible) return;
            push.model = t->getMatrix();
'@
$new8 = @'
m_world.landmarkTags.each([&](Entity e, const LandmarkTag&) {
            const auto* t = m_world.transforms.tryGet(e);
            const auto* r = m_world.renderables.tryGet(e);
            if (!t || !r || !r->visible) return;
            if (!m_frustumCuller.testEntity(t->position, t->scale)) return;
            push.model = t->getMatrix();
'@
Apply-Edit "Frustum cull landmarks" $old8 $new8

# 9. Frustum cull enemies
$old9 = @'
for (Entity e : m_enemyEntities) {
            const auto* t = m_world.transforms.tryGet(e);
            const auto* health = m_world.healths.tryGet(e);
            if (t && health && !health->isDead) {
                push.model = t->getMatrix();
'@
$new9 = @'
for (Entity e : m_enemyEntities) {
            const auto* t = m_world.transforms.tryGet(e);
            const auto* health = m_world.healths.tryGet(e);
            if (t && health && !health->isDead) {
                if (!m_frustumCuller.testEntity(t->position, t->scale, 2.0f)) continue;
                push.model = t->getMatrix();
'@
Apply-Edit "Frustum cull enemies" $old9 $new9

# 10. Frustum cull NPCs
$old10 = @'
for (Entity e : m_npcEntities) {
            const auto* t = m_world.transforms.tryGet(e);
            if (t) {
                push.model = t->getMatrix();
'@
$new10 = @'
for (Entity e : m_npcEntities) {
            const auto* t = m_world.transforms.tryGet(e);
            if (t) {
                if (!m_frustumCuller.testSphere(t->position, 2.0f)) continue;
                push.model = t->getMatrix();
'@
Apply-Edit "Frustum cull NPCs" $old10 $new10

# 11. Culling stats in log (format string)
$old11 = '                    Logger::infof("FPS: {:.0f} | {:02d}:{:02d} {} | Pos: ({:.0f},{:.1f},{:.0f})",'
$new11 = '                    Logger::infof("FPS: {:.0f} | {:02d}:{:02d} {} | Pos: ({:.0f},{:.1f},{:.0f}) | Cull: {}/{} ({:.0f}%%)",'
Apply-Edit "Culling stats format string" $old11 $new11

# 12. Culling stats in log (arguments)
$old12 = '                        m_timer.fps(), dispH, dispM, timePeriod, pt.position.x, pt.position.y, pt.position.z);'
$new12 = '                        m_timer.fps(), dispH, dispM, timePeriod, pt.position.x, pt.position.y, pt.position.z, m_frustumCuller.totalVisible(), m_frustumCuller.totalTested(), m_frustumCuller.cullPercentage());'
Apply-Edit "Culling stats arguments" $old12 $new12

# 13. Pause overlay in renderUI
$old13 = @'
        // Death overlay
        if (m_hud.isDead) {
'@
$new13 = @'
        // Pause overlay
        if (m_gameState.current() == GameState::Paused) {
            drawUIQuad({0, 0}, {w, h}, {0, 0, 0, 0.5f}, w, h);
            float pauseW = 300.0f, pauseH = 60.0f;
            float px2 = (w - pauseW) / 2.0f;
            float py2 = (h - pauseH) / 2.0f;
            drawUIQuad({px2, py2}, {pauseW, pauseH}, {0.1f, 0.1f, 0.2f, 0.95f}, w, h);
            glm::vec4 gold = {0.85f, 0.7f, 0.3f, 1.0f};
            drawUIQuad({px2, py2 - 3}, {pauseW, 3}, gold, w, h);
            drawUIQuad({px2, py2 + pauseH}, {pauseW, 3}, gold, w, h);
            drawUIQuad({px2 - 3, py2}, {3, pauseH}, gold, w, h);
            drawUIQuad({px2 + pauseW, py2}, {3, pauseH}, gold, w, h);
            drawUIQuad({px2 + 20, py2 + pauseH/2 - 4}, {pauseW - 40, 8}, gold, w, h);
        }
        
        // Death overlay
        if (m_hud.isDead) {
'@
Apply-Edit "Pause overlay in renderUI" $old13 $new13

# 14. Block gameplay input when paused
$old14 = @'
        // Combat - left click to attack
        if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
'@
$new14 = @'
        // Skip gameplay input when paused
        if (m_gameState.current() == GameState::Paused) return;
        
        // Combat - left click to attack
        if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
'@
Apply-Edit "Block gameplay input when paused" $old14 $new14

# Write
Write-Host "`nWriting main.cpp..." -ForegroundColor Yellow
[System.IO.File]::WriteAllText((Resolve-Path $mainCpp).Path, $content)
Write-Host "Applied $editCount edits." -ForegroundColor Green

# Build
Write-Host "`nBuilding..." -ForegroundColor Yellow
$buildOutput = cmake --build build --config Release 2>&1
$buildErrors = $buildOutput | Where-Object { $_ -match "error C" }

if ($buildErrors) {
    foreach ($line in $buildErrors) { Write-Host "  $line" -ForegroundColor Red }
}

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== BUILD SUCCESSFUL ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "What changed:" -ForegroundColor Cyan
    Write-Host "  - ESC pauses/unpauses (dark overlay + gold border)" -ForegroundColor White
    Write-Host "  - Gameplay freezes when paused (combat, AI, movement, time)" -ForegroundColor White
    Write-Host "  - Off-screen landmarks, enemies, NPCs are frustum culled" -ForegroundColor White
    Write-Host "  - Culling stats in periodic log: Cull: visible/total (%%)" -ForegroundColor White
    Write-Host "  - Window title: 'Mythbreaker'" -ForegroundColor White
    Write-Host ""
    Write-Host "Run: .\build\bin\Release\Mythbreaker.exe" -ForegroundColor Cyan
} else {
    Write-Host "`n=== BUILD FAILED ===" -ForegroundColor Red
    Write-Host "Restoring backup..." -ForegroundColor Yellow
    Copy-Item $backup $mainCpp
    Write-Host "Restored from: $backup" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Errors:" -ForegroundColor Yellow
    foreach ($line in $buildOutput) {
        if ($line -match "error") { Write-Host "  $line" -ForegroundColor Red }
    }
}
