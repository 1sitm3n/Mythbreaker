#include "BiomeSystem.h"
#include "Logger.h"
#include <algorithm>

namespace myth {

void BiomeSystem::init(int seed) {
    m_seed = seed;
    worldSeed = seed;
    m_rng.seed(seed);
    
    // Initialize permutation table for noise
    m_perm.resize(512);
    for (int i = 0; i < 256; i++) m_perm[i] = i;
    std::shuffle(m_perm.begin(), m_perm.begin() + 256, m_rng);
    for (int i = 0; i < 256; i++) m_perm[256 + i] = m_perm[i];
    
    initBiomes();
    generateWaterBodies(500);
    
    Logger::info("Biome system initialized with diverse terrain");
}

void BiomeSystem::initBiomes() {
    m_biomes.resize(static_cast<size_t>(BiomeType::Lake) + 1);
    
    // Ocean - deep water
    m_biomes[0] = {BiomeType::Ocean, 
        glm::vec3(0.1f, 0.2f, 0.4f), glm::vec3(0.0f), 
        0, 0, 0, 0.3f, 0.9f, 1.0f, -1.0f, 1.0f};
    
    // Beach - sandy shores
    m_biomes[1] = {BiomeType::Beach,
        glm::vec3(0.76f, 0.70f, 0.50f), glm::vec3(0.6f, 0.7f, 0.3f),
        0.5f, 0.1f, 2.0f, 0.2f, 0.3f, 0.7f, 0.2f, 0.8f};
    
    // Plains - open grassland
    m_biomes[2] = {BiomeType::Plains,
        glm::vec3(0.45f, 0.55f, 0.25f), glm::vec3(0.3f, 0.6f, 0.2f),
        2.0f, 0.8f, 1.0f, 0.6f, 0.3f, 0.5f, 0.3f, 0.7f};
    
    // Forest - scattered trees
    m_biomes[3] = {BiomeType::Forest,
        glm::vec3(0.3f, 0.45f, 0.2f), glm::vec3(0.2f, 0.5f, 0.15f),
        8.0f, 0.6f, 2.0f, 0.8f, 0.5f, 0.7f, 0.2f, 0.6f};
    
    // Dense Forest - thick woods
    m_biomes[4] = {BiomeType::DenseForest,
        glm::vec3(0.2f, 0.35f, 0.15f), glm::vec3(0.15f, 0.4f, 0.1f),
        15.0f, 0.4f, 3.0f, 1.0f, 0.7f, 0.9f, 0.1f, 0.5f};
    
    // Swamp - murky wetlands
    m_biomes[5] = {BiomeType::Swamp,
        glm::vec3(0.25f, 0.3f, 0.2f), glm::vec3(0.3f, 0.35f, 0.2f),
        5.0f, 0.3f, 1.0f, 0.3f, 0.8f, 1.0f, 0.3f, 0.6f};
    
    // Desert - arid dunes
    m_biomes[6] = {BiomeType::Desert,
        glm::vec3(0.82f, 0.75f, 0.55f), glm::vec3(0.7f, 0.6f, 0.3f),
        0.2f, 0.05f, 4.0f, 0.7f, 0.0f, 0.2f, 0.6f, 1.0f};
    
    // Savanna - dry grassland
    m_biomes[7] = {BiomeType::Savanna,
        glm::vec3(0.6f, 0.55f, 0.35f), glm::vec3(0.5f, 0.5f, 0.25f),
        1.5f, 0.4f, 2.0f, 0.5f, 0.2f, 0.4f, 0.5f, 0.9f};
    
    // Tundra - cold sparse
    m_biomes[8] = {BiomeType::Tundra,
        glm::vec3(0.5f, 0.55f, 0.5f), glm::vec3(0.4f, 0.5f, 0.35f),
        0.5f, 0.2f, 5.0f, 0.4f, 0.2f, 0.5f, -1.0f, 0.0f};
    
    // Mountains - rocky heights
    m_biomes[9] = {BiomeType::Mountains,
        glm::vec3(0.45f, 0.42f, 0.4f), glm::vec3(0.3f, 0.4f, 0.25f),
        3.0f, 0.2f, 8.0f, 2.5f, 0.3f, 0.6f, -0.5f, 0.3f};
    
    // Snow Peaks - frozen summits
    m_biomes[10] = {BiomeType::SnowPeaks,
        glm::vec3(0.9f, 0.92f, 0.95f), glm::vec3(0.85f, 0.9f, 0.85f),
        0.5f, 0.0f, 4.0f, 3.0f, 0.4f, 0.8f, -1.0f, -0.3f};
    
    // Lake - inland water
    m_biomes[11] = {BiomeType::Lake,
        glm::vec3(0.2f, 0.35f, 0.5f), glm::vec3(0.0f),
        0, 0, 0, 0.1f, 0.9f, 1.0f, -0.5f, 0.8f};
}

float BiomeSystem::hash(float n) const {
    return glm::fract(sin(n) * 43758.5453123f);
}

glm::vec2 BiomeSystem::hash2(glm::vec2 p) const {
    p = glm::vec2(glm::dot(p, glm::vec2(127.1f, 311.7f)),
                  glm::dot(p, glm::vec2(269.5f, 183.3f)));
    return glm::fract(glm::sin(p) * 43758.5453f);
}

float BiomeSystem::noise2D(float x, float z, float scale) const {
    x *= scale;
    z *= scale;
    
    int ix = (int)floor(x);
    int iz = (int)floor(z);
    float fx = x - ix;
    float fz = z - iz;
    
    // Smoothstep
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uz = fz * fz * (3.0f - 2.0f * fz);
    
    if (m_perm.empty()) return 0.5f; // Not initialized yet
    int a = m_perm[((ix & 255) + m_perm[iz & 255]) & 511];
    int b = m_perm[((ix & 255) + m_perm[(iz + 1) & 255]) & 511];
    int c = m_perm[(((ix + 1) & 255) + m_perm[iz & 255]) & 511];
    int d = m_perm[(((ix + 1) & 255) + m_perm[(iz + 1) & 255]) & 511];
    
    float va = hash(float(a + m_seed));
    float vb = hash(float(b + m_seed));
    float vc = hash(float(c + m_seed));
    float vd = hash(float(d + m_seed));
    
    float k0 = va;
    float k1 = vc - va;
    float k2 = vb - va;
    float k3 = va - vb - vc + vd;
    
    return k0 + k1 * ux + k2 * uz + k3 * ux * uz;
}

float BiomeSystem::fbm(float x, float z, int octaves, float persistence, float scale) const {
    float total = 0.0f;
    float amplitude = 1.0f;
    float maxValue = 0.0f;
    float freq = scale;
    
    for (int i = 0; i < octaves; i++) {
        total += noise2D(x, z, freq) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        freq *= 2.0f;
    }
    
    return total / maxValue;
}

float BiomeSystem::ridgedNoise(float x, float z, float scale) const {
    float n = noise2D(x, z, scale);
    n = 1.0f - abs(n * 2.0f - 1.0f);
    return n * n;
}

float BiomeSystem::getMoisture(float x, float z) const {
    // Large-scale moisture patterns
    float moisture = fbm(x + 1000.0f, z + 1000.0f, 4, 0.5f, 0.003f);
    
    // Add some variation
    moisture += noise2D(x, z, 0.01f) * 0.3f;
    
    // Clamp
    return glm::clamp(moisture, 0.0f, 1.0f);
}

float BiomeSystem::getTemperature(float x, float z) const {
    // Base temperature (latitude-like gradient)
    float baseTemp = 0.5f + noise2D(x, z, 0.001f) * 0.3f;
    
    // Local variation
    baseTemp += noise2D(x + 5000.0f, z + 5000.0f, 0.008f) * 0.4f;
    
    // Height affects temperature (colder at altitude)
    float height = getHeight(x, z);
    float heightPenalty = glm::max(0.0f, (height - 10.0f) * 0.03f);
    baseTemp -= heightPenalty;
    
    return glm::clamp(baseTemp, -1.0f, 1.0f);
}

float BiomeSystem::getHeight(float x, float z) const {
    // Continental shape
    float continental = fbm(x, z, 4, 0.5f, 0.002f);
    
    // Mountain ridges
    float mountains = ridgedNoise(x, z, 0.008f) * ridgedNoise(x * 1.5f, z * 1.3f, 0.006f);
    mountains = pow(mountains, 1.5f);
    
    // Hills
    float hills = fbm(x, z, 6, 0.55f, 0.015f);
    
    // Detail
    float detail = fbm(x, z, 3, 0.5f, 0.05f) * 0.3f;
    
    // Combine - scale down for playable heights
    float height = continental * 3.0f;
    height += mountains * 12.0f;
    height += hills * 3.0f;
    height += detail * 0.5f;
    
    // Water bodies depression
    for (const auto& water : m_waterBodies) {
        float dist = glm::length(glm::vec2(x, z) - glm::vec2(water.center.x, water.center.z));
        if (dist < water.radius * 1.5f) {
            float t = glm::clamp(dist / water.radius, 0.0f, 1.0f);
            float depression = (1.0f - t * t) * water.depth;
            height -= depression;
        }
    }
    
    return height;
}

BiomeType BiomeSystem::getBiome(float x, float z) const {
    float height = getHeight(x, z);
    float moisture = getMoisture(x, z);
    float temperature = getTemperature(x, z);
    
    // Check water first
    if (height < seaLevel - 2.0f) return BiomeType::Ocean;
    if (height < seaLevel) return BiomeType::Beach;
    if (isWater(x, z)) return BiomeType::Lake;
    
    // Snow peaks at high altitude
    if (height > 25.0f && temperature < 0.2f) return BiomeType::SnowPeaks;
    
    // Mountains at high altitude
    if (height > 18.0f) return BiomeType::Mountains;
    
    // Temperature-based biomes
    if (temperature < 0.0f) return BiomeType::Tundra;
    
    if (temperature > 0.7f) {
        // Hot biomes
        if (moisture < 0.3f) return BiomeType::Desert;
        if (moisture < 0.5f) return BiomeType::Savanna;
    }
    
    // Temperate biomes based on moisture
    if (moisture > 0.8f) {
        if (temperature < 0.4f) return BiomeType::Swamp;
        return BiomeType::DenseForest;
    }
    
    if (moisture > 0.5f) return BiomeType::Forest;
    
    return BiomeType::Plains;
}

const BiomeInfo& BiomeSystem::getBiomeInfo(BiomeType type) const {
    return m_biomes[static_cast<size_t>(type)];
}

glm::vec3 BiomeSystem::getGroundColor(float x, float z) const {
    BiomeType biome = getBiome(x, z);
    const BiomeInfo& info = getBiomeInfo(biome);
    
    // Add noise variation to color
    float colorNoise = noise2D(x * 3.0f, z * 3.0f, 0.1f) * 0.15f;
    glm::vec3 color = info.groundColor + glm::vec3(colorNoise);
    
    // Blend with neighbors for smooth transitions
    float blendDist = 8.0f;
    glm::vec3 avgColor = color;
    int samples = 1;
    
    for (float ox = -blendDist; ox <= blendDist; ox += blendDist) {
        for (float oz = -blendDist; oz <= blendDist; oz += blendDist) {
            if (ox == 0 && oz == 0) continue;
            BiomeType neighbor = getBiome(x + ox, z + oz);
            if (neighbor != biome) {
                avgColor += getBiomeInfo(neighbor).groundColor;
                samples++;
            }
        }
    }
    
    return avgColor / float(samples);
}

bool BiomeSystem::isWater(float x, float z) const {
    for (const auto& water : m_waterBodies) {
        float dist = glm::length(glm::vec2(x, z) - glm::vec2(water.center.x, water.center.z));
        if (dist < water.radius) return true;
    }
    return getHeight(x, z) < seaLevel;
}

float BiomeSystem::getWaterLevel(float x, float z) const {
    for (const auto& water : m_waterBodies) {
        float dist = glm::length(glm::vec2(x, z) - glm::vec2(water.center.x, water.center.z));
        if (dist < water.radius) return water.center.y;
    }
    return seaLevel;
}

void BiomeSystem::generateWaterBodies(int worldRadius) {
    m_waterBodies.clear();
    m_rng.seed(m_seed + 777);
    
    std::uniform_real_distribution<float> posDist(-float(worldRadius), float(worldRadius));
    std::uniform_real_distribution<float> sizeDist(15.0f, 60.0f);
    std::uniform_real_distribution<float> depthDist(3.0f, 8.0f);
    
    int numLakes = worldRadius / 40;
    
    for (int i = 0; i < numLakes; i++) {
        WaterBody lake;
        lake.center = glm::vec3(posDist(m_rng), seaLevel - 0.5f, posDist(m_rng));
        lake.radius = sizeDist(m_rng);
        lake.depth = depthDist(m_rng);
        lake.isRiver = false;
        
        // Don't place lakes on mountains
        float baseHeight = fbm(lake.center.x, lake.center.z, 4, 0.5f, 0.002f) * 8.0f;
        if (baseHeight > 12.0f) continue;
        
        m_waterBodies.push_back(lake);
    }
    
    // Add a lake near spawn
    WaterBody spawnLake;
    spawnLake.center = glm::vec3(40.0f, 2.0f, -30.0f);
    spawnLake.radius = 25.0f;
    spawnLake.depth = 6.0f; // Deep depression
    spawnLake.isRiver = false;
    m_waterBodies.push_back(spawnLake);
    
    Logger::infof("Generated {} lakes/ponds", m_waterBodies.size());
    
    // Debug: check biome at origin
    Logger::infof("Biome at (0,0): {}, height: {:.1f}", static_cast<int>(getBiome(0, 0)), getHeight(0, 0));
    Logger::infof("Biome at (50,50): {}, height: {:.1f}", static_cast<int>(getBiome(50, 50)), getHeight(50, 50));
}

std::vector<TreeInstance> BiomeSystem::getTreesInChunk(int chunkX, int chunkZ) const {
    std::vector<TreeInstance> trees;
    
    float cx = chunkX * chunkSize;
    float cz = chunkZ * chunkSize;
    
    // Get biome at chunk center
    BiomeType biome = getBiome(cx + chunkSize * 0.5f, cz + chunkSize * 0.5f);
    const BiomeInfo& info = getBiomeInfo(biome);
    
    if (info.treeDensity < 0.1f) return trees;
    
    // Deterministic RNG for this chunk
    std::mt19937 rng(m_seed + chunkX * 73856093 + chunkZ * 19349663);
    std::uniform_real_distribution<float> posDist(0.0f, chunkSize);
    std::uniform_real_distribution<float> scaleDist(0.7f, 1.3f);
    std::uniform_real_distribution<float> rotDist(0.0f, 6.28f);
    
    int numTrees = static_cast<int>(info.treeDensity);
    float frac = info.treeDensity - numTrees;
    if (std::uniform_real_distribution<float>(0, 1)(rng) < frac) numTrees++;
    
    for (int i = 0; i < numTrees; i++) {
        float lx = posDist(rng);
        float lz = posDist(rng);
        float wx = cx + lx;
        float wz = cz + lz;
        
        // Skip if in water
        if (isWater(wx, wz)) continue;
        
        // Skip steep slopes
        float h0 = getHeight(wx, wz);
        float h1 = getHeight(wx + 1, wz);
        float h2 = getHeight(wx, wz + 1);
        float slope = abs(h1 - h0) + abs(h2 - h0);
        if (slope > 3.0f) continue;
        
        TreeInstance tree;
        tree.position = glm::vec3(wx, h0, wz);
        tree.scale = scaleDist(rng);
        tree.rotation = rotDist(rng);
        
        // Tree type based on biome
        switch (biome) {
            case BiomeType::DenseForest:
            case BiomeType::Forest:
                tree.type = (rng() % 3 == 0) ? 0 : 1; // Pine or Oak
                break;
            case BiomeType::Savanna:
            case BiomeType::Desert:
                tree.type = 2; // Palm/Acacia style
                break;
            case BiomeType::Swamp:
                tree.type = 3; // Dead/willow
                break;
            case BiomeType::Tundra:
            case BiomeType::Mountains:
                tree.type = 0; // Pine
                tree.scale *= 0.7f;
                break;
            default:
                tree.type = rng() % 2; // Random
        }
        
        trees.push_back(tree);
    }
    
    return trees;
}

std::vector<RockInstance> BiomeSystem::getRocksInChunk(int chunkX, int chunkZ) const {
    std::vector<RockInstance> rocks;
    
    float cx = chunkX * chunkSize;
    float cz = chunkZ * chunkSize;
    
    BiomeType biome = getBiome(cx + chunkSize * 0.5f, cz + chunkSize * 0.5f);
    const BiomeInfo& info = getBiomeInfo(biome);
    
    if (info.rockDensity < 0.1f) return rocks;
    
    std::mt19937 rng(m_seed + chunkX * 83492791 + chunkZ * 29384756);
    std::uniform_real_distribution<float> posDist(0.0f, chunkSize);
    std::uniform_real_distribution<float> scaleDist(0.3f, 1.5f);
    std::uniform_real_distribution<float> rotDist(0.0f, 6.28f);
    
    int numRocks = static_cast<int>(info.rockDensity);
    
    for (int i = 0; i < numRocks; i++) {
        float lx = posDist(rng);
        float lz = posDist(rng);
        float wx = cx + lx;
        float wz = cz + lz;
        
        if (isWater(wx, wz)) continue;
        
        RockInstance rock;
        rock.position = glm::vec3(wx, getHeight(wx, wz), wz);
        rock.scale = scaleDist(rng);
        rock.rotation = rotDist(rng);
        rock.type = rng() % 4;
        
        // Larger rocks in mountains
        if (biome == BiomeType::Mountains || biome == BiomeType::SnowPeaks) {
            rock.scale *= 1.5f;
        }
        
        rocks.push_back(rock);
    }
    
    return rocks;
}

} // namespace myth



