#pragma once

#include "RegionState.h"
#include <glm/glm.hpp>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>

namespace myth {

struct SaveData {
    // Player state
    glm::vec3 playerPosition{0, 0, 0};
    float playerYaw = 0.0f;
    
    // Camera state
    float cameraYaw = 0.0f;
    float cameraPitch = 25.0f;
    float cameraDistance = 8.0f;
    
    // Region states
    struct RegionSave {
        int x, z;
        int state;  // RegionState enum as int
        float pressure;
    };
    std::vector<RegionSave> regions;
    
    // Metadata
    float playTime = 0.0f;
};

class SaveManager {
public:
    static constexpr const char* SAVE_DIRECTORY = "saves";
    static constexpr const char* DEFAULT_SAVE = "saves/quicksave.json";
    
    static bool save(const SaveData& data, const std::string& filename = DEFAULT_SAVE) {
        // Ensure saves directory exists
        std::filesystem::create_directories(SAVE_DIRECTORY);
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        // Write JSON manually (simple format)
        file << "{\n";
        file << "  \"version\": 1,\n";
        file << "  \"playTime\": " << data.playTime << ",\n";
        
        // Player
        file << "  \"player\": {\n";
        file << "    \"position\": [" << data.playerPosition.x << ", " 
             << data.playerPosition.y << ", " << data.playerPosition.z << "],\n";
        file << "    \"yaw\": " << data.playerYaw << "\n";
        file << "  },\n";
        
        // Camera
        file << "  \"camera\": {\n";
        file << "    \"yaw\": " << data.cameraYaw << ",\n";
        file << "    \"pitch\": " << data.cameraPitch << ",\n";
        file << "    \"distance\": " << data.cameraDistance << "\n";
        file << "  },\n";
        
        // Regions
        file << "  \"regions\": [\n";
        for (size_t i = 0; i < data.regions.size(); i++) {
            const auto& r = data.regions[i];
            file << "    {\"x\": " << r.x << ", \"z\": " << r.z 
                 << ", \"state\": " << r.state << ", \"pressure\": " << r.pressure << "}";
            if (i < data.regions.size() - 1) file << ",";
            file << "\n";
        }
        file << "  ]\n";
        
        file << "}\n";
        file.close();
        return true;
    }
    
    static bool load(SaveData& data, const std::string& filename = DEFAULT_SAVE) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        // Read entire file
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();
        
        // Simple JSON parsing (not robust, but works for our format)
        data = SaveData{}; // Reset
        
        // Parse playTime
        data.playTime = parseFloat(content, "\"playTime\":");
        
        // Parse player position
        size_t posStart = content.find("\"position\": [");
        if (posStart != std::string::npos) {
            posStart += 13;
            size_t posEnd = content.find("]", posStart);
            std::string posStr = content.substr(posStart, posEnd - posStart);
            sscanf(posStr.c_str(), "%f, %f, %f", 
                   &data.playerPosition.x, &data.playerPosition.y, &data.playerPosition.z);
        }
        
        // Parse player yaw (find it in player section)
        size_t playerSection = content.find("\"player\":");
        if (playerSection != std::string::npos) {
            size_t playerEnd = content.find("}", playerSection);
            std::string playerStr = content.substr(playerSection, playerEnd - playerSection);
            data.playerYaw = parseFloat(playerStr, "\"yaw\":");
        }
        
        // Parse camera
        size_t cameraSection = content.find("\"camera\":");
        if (cameraSection != std::string::npos) {
            size_t cameraEnd = content.find("}", cameraSection);
            std::string cameraStr = content.substr(cameraSection, cameraEnd - cameraSection);
            data.cameraYaw = parseFloat(cameraStr, "\"yaw\":");
            data.cameraPitch = parseFloat(cameraStr, "\"pitch\":");
            data.cameraDistance = parseFloat(cameraStr, "\"distance\":");
        }
        
        // Parse regions array
        size_t regionsStart = content.find("\"regions\": [");
        if (regionsStart != std::string::npos) {
            size_t regionsEnd = content.find("]", regionsStart);
            std::string regionsStr = content.substr(regionsStart, regionsEnd - regionsStart);
            
            size_t pos = 0;
            while ((pos = regionsStr.find("{", pos)) != std::string::npos) {
                size_t end = regionsStr.find("}", pos);
                std::string regionStr = regionsStr.substr(pos, end - pos);
                
                SaveData::RegionSave r;
                r.x = static_cast<int>(parseFloat(regionStr, "\"x\":"));
                r.z = static_cast<int>(parseFloat(regionStr, "\"z\":"));
                r.state = static_cast<int>(parseFloat(regionStr, "\"state\":"));
                r.pressure = parseFloat(regionStr, "\"pressure\":");
                data.regions.push_back(r);
                
                pos = end + 1;
            }
        }
        
        return true;
    }
    
    static bool saveExists(const std::string& filename = DEFAULT_SAVE) {
        return std::filesystem::exists(filename);
    }

private:
    static float parseFloat(const std::string& str, const std::string& key) {
        size_t pos = str.find(key);
        if (pos == std::string::npos) return 0.0f;
        pos += key.length();
        // Skip whitespace
        while (pos < str.length() && (str[pos] == ' ' || str[pos] == '\t')) pos++;
        return std::stof(str.substr(pos));
    }
};

} // namespace myth
