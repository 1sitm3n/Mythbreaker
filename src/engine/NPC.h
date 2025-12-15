#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <cstdint>

namespace myth {

// Forward declare Entity type
using Entity = uint32_t;
constexpr Entity NPC_NULL_ENTITY = UINT32_MAX;

// Dialogue line with speaker and text
struct DialogueLine {
    std::string speaker;
    std::string text;
};

// NPC component for friendly characters
struct NPC {
    std::string name = "Stranger";
    std::vector<DialogueLine> dialogue;
    int currentLine = 0;
    float interactRadius = 3.0f;
    bool hasSpoken = false;
    
    // Visual
    float idleTimer = 0.0f;
    float idleBobOffset = 0.0f;
    
    void update(float dt) {
        idleTimer += dt;
        idleBobOffset = sin(idleTimer * 2.0f) * 0.05f;
    }
    
    // Get next dialogue line, cycle through
    const DialogueLine* getNextLine() {
        if (dialogue.empty()) return nullptr;
        const DialogueLine* result = &dialogue[currentLine];
        currentLine = (currentLine + 1) % static_cast<int>(dialogue.size());
        hasSpoken = true;
        return result;
    }
    
    void resetDialogue() {
        currentLine = 0;
    }
};

// Tag for NPC entities
struct NPCTag {};

// Dialogue state manager
class DialogueSystem {
public:
    bool isInDialogue = false;
    Entity talkingTo = NPC_NULL_ENTITY;
    std::string currentSpeaker;
    std::string currentText;
    float displayTimer = 0.0f;
    static constexpr float DISPLAY_DURATION = 4.0f;
    
    void startDialogue(Entity npc, const DialogueLine* dialogueLine) {
        if (!dialogueLine) return;
        isInDialogue = true;
        talkingTo = npc;
        currentSpeaker = dialogueLine->speaker;
        currentText = dialogueLine->text;
        displayTimer = DISPLAY_DURATION;
    }
    
    void update(float dt) {
        if (isInDialogue) {
            displayTimer -= dt;
            if (displayTimer <= 0) {
                endDialogue();
            }
        }
    }
    
    void endDialogue() {
        isInDialogue = false;
        talkingTo = NPC_NULL_ENTITY;
        currentSpeaker.clear();
        currentText.clear();
    }
    
    void skipOrAdvance() {
        if (isInDialogue) {
            displayTimer = 0.1f;
        }
    }
};

// Pre-defined NPC templates
namespace NPCTemplates {
    inline NPC createWanderer() {
        NPC npc;
        npc.name = "Wandering Spirit";
        npc.dialogue = {
            {"Spirit", "The veil between worlds grows thin here..."},
            {"Spirit", "You carry the mark of a Mythbreaker."},
            {"Spirit", "Be wary - not all who wander are lost, but some are."},
            {"Spirit", "The fractured lands remember what was."}
        };
        return npc;
    }
    
    inline NPC createMerchant() {
        NPC npc;
        npc.name = "Ethereal Merchant";
        npc.dialogue = {
            {"Merchant", "Greetings, traveler. Care to see my wares?"},
            {"Merchant", "I deal in fragments of forgotten dreams."},
            {"Merchant", "The crystals you find... I could use those."},
            {"Merchant", "Return when you have something to trade."}
        };
        return npc;
    }
    
    inline NPC createSage() {
        NPC npc;
        npc.name = "Ancient Sage";
        npc.dialogue = {
            {"Sage", "Ah, another soul touched by the fracture."},
            {"Sage", "Long ago, the world was whole. Then came the Breaking."},
            {"Sage", "The Mythic energy you disrupt... it sustains this realm."},
            {"Sage", "Tread carefully, Mythbreaker. Your presence changes things."},
            {"Sage", "Seek the shards. They hold memories of what was lost."}
        };
        return npc;
    }
    
    inline NPC createGuard() {
        NPC npc;
        npc.name = "Realm Guardian";
        npc.dialogue = {
            {"Guardian", "Halt! State your purpose."},
            {"Guardian", "...A Mythbreaker? It has been ages."},
            {"Guardian", "The path ahead is dangerous. Enemies lurk."},
            {"Guardian", "I cannot leave my post, but I wish you well."}
        };
        return npc;
    }
    
    inline NPC createMystic() {
        NPC npc;
        npc.name = "Veiled Mystic";
        npc.dialogue = {
            {"Mystic", "*The figure turns slowly*"},
            {"Mystic", "Your aura... it ripples with potential."},
            {"Mystic", "The mythic regions respond to your presence."},
            {"Mystic", "When reality fractures, look for patterns."},
            {"Mystic", "We shall meet again, in another layer perhaps."}
        };
        return npc;
    }
}

} // namespace myth
