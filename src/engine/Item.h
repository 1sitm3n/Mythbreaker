#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>

namespace myth {

enum class ItemType : uint8_t {
    None = 0,
    Consumable,
    Weapon,
    Armor,
    Material,
    Quest,
    Key
};

enum class ItemId : uint16_t {
    None = 0,
    
    // Consumables (1-99)
    HealthPotion = 1,
    StaminaPotion = 2,
    ManaPotion = 3,
    Bread = 10,
    Apple = 11,
    
    // Materials (100-199)
    Stone = 100,
    Wood = 101,
    IronOre = 102,
    Crystal = 103,
    MythicShard = 110,
    
    // Weapons (200-299)
    WoodenSword = 200,
    IronSword = 201,
    MythicBlade = 210,
    
    // Armor (300-399)
    LeatherArmor = 300,
    IronArmor = 301,
    
    // Quest items (400-499)
    AncientKey = 400,
    MysteriousOrb = 401
};

struct ItemDef {
    ItemId id = ItemId::None;
    const char* name = "Unknown";
    const char* description = "";
    ItemType type = ItemType::None;
    uint8_t maxStack = 1;
    float value = 0.0f;
    
    bool isStackable() const { return maxStack > 1; }
};

class ItemDatabase {
public:
    static const ItemDef& get(ItemId id) {
        static const ItemDef none{ItemId::None, "Unknown", "???", ItemType::None, 1, 0.0f};
        static const std::unordered_map<uint16_t, ItemDef> items = {
            // Consumables
            {1, {ItemId::HealthPotion, "Health Potion", "Restores 50 HP", ItemType::Consumable, 10, 50.0f}},
            {2, {ItemId::StaminaPotion, "Stamina Potion", "Restores 50 Stamina", ItemType::Consumable, 10, 50.0f}},
            {3, {ItemId::ManaPotion, "Mana Potion", "Restores 50 Mana", ItemType::Consumable, 10, 50.0f}},
            {10, {ItemId::Bread, "Bread", "Restores 20 HP", ItemType::Consumable, 20, 20.0f}},
            {11, {ItemId::Apple, "Apple", "Restores 10 HP", ItemType::Consumable, 30, 10.0f}},
            
            // Materials
            {100, {ItemId::Stone, "Stone", "Basic building material", ItemType::Material, 99, 0.0f}},
            {101, {ItemId::Wood, "Wood", "Basic building material", ItemType::Material, 99, 0.0f}},
            {102, {ItemId::IronOre, "Iron Ore", "Can be smelted", ItemType::Material, 50, 0.0f}},
            {103, {ItemId::Crystal, "Crystal", "Magical material", ItemType::Material, 30, 0.0f}},
            {110, {ItemId::MythicShard, "Mythic Shard", "Fragment of broken reality", ItemType::Material, 10, 0.0f}},
            
            // Weapons
            {200, {ItemId::WoodenSword, "Wooden Sword", "A simple training weapon", ItemType::Weapon, 1, 5.0f}},
            {201, {ItemId::IronSword, "Iron Sword", "A sturdy blade", ItemType::Weapon, 1, 15.0f}},
            {210, {ItemId::MythicBlade, "Mythic Blade", "Forged from reality shards", ItemType::Weapon, 1, 50.0f}},
            
            // Armor
            {300, {ItemId::LeatherArmor, "Leather Armor", "Basic protection", ItemType::Armor, 1, 10.0f}},
            {301, {ItemId::IronArmor, "Iron Armor", "Solid protection", ItemType::Armor, 1, 25.0f}},
            
            // Quest
            {400, {ItemId::AncientKey, "Ancient Key", "Opens ancient doors", ItemType::Quest, 1, 0.0f}},
            {401, {ItemId::MysteriousOrb, "Mysterious Orb", "Pulses with strange energy", ItemType::Quest, 1, 0.0f}},
        };
        
        auto it = items.find(static_cast<uint16_t>(id));
        return (it != items.end()) ? it->second : none;
    }
    
    static const char* typeName(ItemType type) {
        switch (type) {
            case ItemType::Consumable: return "Consumable";
            case ItemType::Weapon: return "Weapon";
            case ItemType::Armor: return "Armor";
            case ItemType::Material: return "Material";
            case ItemType::Quest: return "Quest";
            case ItemType::Key: return "Key";
            default: return "Unknown";
        }
    }
};

} // namespace myth
