#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <random>
#include <cmath>

namespace myth {

enum class BiomeType {
    Ocean,
    Beach,
    Plains,
    Forest,
    DenseForest,
    Swamp,
    Desert,
    Savanna,
    Tundra,
    Mountains,
    SnowPeaks,
    Lake
};

struct BiomeInfo {
    BiomeType type;
    glm::vec3 groundColor;
    glm::vec3 foliageColor;
    float treeDensity;      // Trees per chunk
    float grassDensity;     // Grass coverage 0-1
    float rockDensity;      // Rocks per chunk
    float heightScale;      // Terrain height multiplier
    float moistureMin;
    float moistureMax;
    float temperatureMin;
    float temperatureMax;
};

struct TreeInstance {
    glm::vec3 position;
    float scale;
    float rotation;
    int type; // 0=pine, 1=oak, 2=palm, 3=dead
};

struct RockInstance {
    glm::vec3 position;
    float scale;
    float rotation;
    int type; // 0=small, 1=medium, 2=large, 3=boulder
};

struct WaterBody {
    glm::vec3 center;
    float radius;
    float depth;
    bool isRiver;
};

struct TerrainVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 color;      // Biome-tinted color
    float moisture;       // For blending textures
    float temperature;
};

class BiomeSystem {
public:
    static BiomeSystem& instance() {
        static BiomeSystem s;
        return s;
    }
    
    void init(int seed = 12345);
    
    // Core queries
    BiomeType getBiome(float x, float z) const;
    const BiomeInfo& getBiomeInfo(BiomeType type) const;
    float getHeight(float x, float z) const;
    float getMoisture(float x, float z) const;
    float getTemperature(float x, float z) const;
    glm::vec3 getGroundColor(float x, float z) const;
    bool isWater(float x, float z) const;
    float getWaterLevel(float x, float z) const;
    
    // Noise functions
    float noise2D(float x, float z, float scale = 1.0f) const;
    float fbm(float x, float z, int octaves, float persistence, float scale) const;
    float ridgedNoise(float x, float z, float scale) const;
    
    // Vegetation queries
    std::vector<TreeInstance> getTreesInChunk(int chunkX, int chunkZ) const;
    std::vector<RockInstance> getRocksInChunk(int chunkX, int chunkZ) const;
    
    // Water
    const std::vector<WaterBody>& getWaterBodies() const { return m_waterBodies; }
    void generateWaterBodies(int worldRadius);
    
    // Settings
    float seaLevel = 2.0f;
    float chunkSize = 32.0f;
    int worldSeed = 12345;
    
private:
    BiomeSystem() = default;
    void initBiomes();
    float hash(float n) const;
    glm::vec2 hash2(glm::vec2 p) const;
    
    std::vector<BiomeInfo> m_biomes;
    std::vector<WaterBody> m_waterBodies;
    mutable std::mt19937 m_rng;
    int m_seed = 12345;
    
    // Noise permutation table
    std::vector<int> m_perm;
};

} // namespace myth