###############################################################################
# Mythbreaker — Wire In: Frustum Culling + Game State + PBR Prep
#
# USAGE:
#   cd C:\Projects\Mythbreaker
#   .\wire_all_systems.ps1
#
# Creates a backup of main.cpp, then applies all edits.
###############################################################################

param(
    [string]$ProjectRoot = "C:\Projects\Mythbreaker"
)

$ErrorActionPreference = "Stop"
Set-Location $ProjectRoot

Write-Host "`n=== Wiring Frustum Culling + Game State + PBR Prep ===" -ForegroundColor Cyan

$mainCpp = "src/app/main.cpp"

# Backup
$backup = "$mainCpp.backup_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
Copy-Item $mainCpp $backup
Write-Host "Backup saved: $backup" -ForegroundColor Gray

$content = [System.IO.File]::ReadAllText((Resolve-Path $mainCpp))

$editCount = 0

function Apply-Edit($Name, $Old, $New) {
    $script:editCount++
    if ($script:content.Contains($Old)) {
        $script:content = $script:content.Replace($Old, $New)
        Write-Host "  [$script:editCount] $Name" -ForegroundColor Green
    } else {
        Write-Host "  [$script:editCount] $Name -- SKIPPED (already applied or not found)" -ForegroundColor Yellow
    }
}

Write-Host "`nApplying edits..." -ForegroundColor Yellow

# ── 1. Add #includes ──
$old1 = '#include "engine/BiomeSystem.h"'
$new1 = @'
#include "engine/BiomeSystem.h"
#include "engine/FrustumCuller.h"
#include "engine/GameState.h"
#include "engine/PBRMaterial.h"
#include "engine/TangentCalculator.h"
'@
Apply-Edit "Add new includes" $old1 $new1

# ── 2. Add member variables ──
$old2 = 'RegionState m_lastLoggedState = RegionState::Stable;'
$new2 = @'
RegionState m_lastLoggedState = RegionState::Stable;
    FrustumCuller m_frustumCuller;
    GameStateMachine m_gameState;
'@
Apply-Edit "Add FrustumCuller + GameStateMachine members" $old2 $new2

# ── 3. Fix window title ──
$old3 = '"Mythbreaker - NPC & Dialogue"'
$new3 = '"Mythbreaker"'
Apply-Edit "Update window title" $old3 $new3

# ── 4. Initialize game state in initVulkan() ──
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

# ── 5. Update frustum culler each frame ──
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
Apply-Edit "Add frustum culler update in updateCameraUBO()" $old5 $new5

# ── 6. ESC toggles pause ──
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
Apply-Edit "ESC toggles pause instead of quitting" $old6 $new6

# ── 7. Add pause logic to main loop ──
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
Apply-Edit "Add pause logic to main loop" $old7 $new7

# ── 8. Frustum cull landmarks ──
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

# ── 9. Frustum cull enemies ──
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

# ── 10. Frustum cull NPCs ──
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

# ── 11. Add culling stats to periodic log ──
$old11 = '                    Logger::infof("FPS: {:.0f} | {:02d}:{:02d} {} | Pos: ({:.0f},{:.1f},{:.0f})",'
$new11 = '                    Logger::infof("FPS: {:.0f} | {:02d}:{:02d} {} | Pos: ({:.0f},{:.1f},{:.0f}) | Cull: {}/{} ({:.0f}%%)",'
Apply-Edit "Add culling stats format string" $old11 $new11

$old11b = '                        m_timer.fps(), dispH, dispM, timePeriod, pt.position.x, pt.position.y, pt.position.z);'
$new11b = '                        m_timer.fps(), dispH, dispM, timePeriod, pt.position.x, pt.position.y, pt.position.z, m_frustumCuller.totalVisible(), m_frustumCuller.totalTested(), m_frustumCuller.cullPercentage());'
Apply-Edit "Add culling stats arguments" $old11b $new11b

# ── 12. Add pause overlay to renderUI ──
$old12 = @'
        // Death overlay
        if (m_hud.isDead) {
'@
$new12 = @'
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
Apply-Edit "Add pause overlay to renderUI" $old12 $new12

# ── 13. Block gameplay input when paused ──
$old13 = @'
        // Combat - left click to attack
        if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
'@
$new13 = @'
        // Skip gameplay input when paused
        if (m_gameState.current() == GameState::Paused) return;
        
        // Combat - left click to attack
        if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
'@
Apply-Edit "Block gameplay input when paused" $old13 $new13

# ── Write ──
Write-Host "`nWriting modified main.cpp..." -ForegroundColor Yellow
[System.IO.File]::WriteAllText((Resolve-Path $mainCpp).Path, $content)
Write-Host "Done! Applied $editCount edits." -ForegroundColor Green

# ── Build ──
Write-Host "`nBuilding..." -ForegroundColor Yellow

$buildOutput = cmake --build build --config Release 2>&1
$buildErrors = $buildOutput | Where-Object { $_ -match "error C" }
$buildWarnings = $buildOutput | Where-Object { $_ -match "warning C" }

if ($buildErrors) {
    foreach ($line in $buildErrors) { Write-Host "  $line" -ForegroundColor Red }
}
if ($buildWarnings) {
    foreach ($line in $buildWarnings) { Write-Host "  $line" -ForegroundColor Yellow }
}

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== BUILD SUCCESSFUL ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "What changed:" -ForegroundColor Cyan
    Write-Host "  - ESC now pauses/unpauses (dark overlay + gold border)" -ForegroundColor White
    Write-Host "  - All gameplay stops when paused (combat, AI, movement, time)" -ForegroundColor White
    Write-Host "  - Off-screen landmarks, enemies, NPCs are frustum culled" -ForegroundColor White
    Write-Host "  - Culling stats show in the periodic log (Cull: visible/total)" -ForegroundColor White
    Write-Host "  - Window title cleaned up to 'Mythbreaker'" -ForegroundColor White
    Write-Host "  - PBRMaterial.h and TangentCalculator.h included, ready to use" -ForegroundColor White
    Write-Host ""
    Write-Host "Run: .\build\bin\Release\Mythbreaker.exe" -ForegroundColor Cyan
} else {
    Write-Host "`n=== BUILD FAILED ===" -ForegroundColor Red
    Write-Host "Restoring backup..." -ForegroundColor Yellow
    Copy-Item $backup $mainCpp
    Write-Host "Restored from: $backup" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Full build output:" -ForegroundColor Yellow
    foreach ($line in $buildOutput) {
        if ($line -match "error") { Write-Host "  $line" -ForegroundColor Red }
    }
}
