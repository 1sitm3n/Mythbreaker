#pragma once

#include "Item.h"
#include <array>
#include <vector>

namespace myth {

struct ItemStack {
    ItemId id = ItemId::None;
    uint8_t count = 0;
    
    bool isEmpty() const { return id == ItemId::None || count == 0; }
    void clear() { id = ItemId::None; count = 0; }
};

class Inventory {
public:
    static constexpr size_t MAX_SLOTS = 20;
    
    Inventory() {
        for (auto& slot : m_slots) slot.clear();
    }
    
    // Add item to inventory, returns amount that couldn't be added
    uint8_t addItem(ItemId id, uint8_t amount = 1) {
        if (id == ItemId::None || amount == 0) return amount;
        
        const auto& def = ItemDatabase::get(id);
        uint8_t remaining = amount;
        
        // First try to stack with existing items
        if (def.isStackable()) {
            for (auto& slot : m_slots) {
                if (slot.id == id && slot.count < def.maxStack) {
                    uint8_t canAdd = def.maxStack - slot.count;
                    uint8_t toAdd = std::min(canAdd, remaining);
                    slot.count += toAdd;
                    remaining -= toAdd;
                    if (remaining == 0) return 0;
                }
            }
        }
        
        // Then try empty slots
        for (auto& slot : m_slots) {
            if (slot.isEmpty()) {
                uint8_t toAdd = std::min(def.maxStack, remaining);
                slot.id = id;
                slot.count = toAdd;
                remaining -= toAdd;
                if (remaining == 0) return 0;
            }
        }
        
        return remaining; // Couldn't fit everything
    }
    
    // Remove item from inventory, returns true if successful
    bool removeItem(ItemId id, uint8_t amount = 1) {
        if (id == ItemId::None || amount == 0) return true;
        
        uint8_t toRemove = amount;
        
        // Find slots with this item (remove from last to first)
        for (int i = static_cast<int>(MAX_SLOTS) - 1; i >= 0; --i) {
            if (m_slots[i].id == id) {
                if (m_slots[i].count <= toRemove) {
                    toRemove -= m_slots[i].count;
                    m_slots[i].clear();
                } else {
                    m_slots[i].count -= toRemove;
                    toRemove = 0;
                }
                if (toRemove == 0) return true;
            }
        }
        
        return toRemove == 0;
    }
    
    // Check if has item
    bool hasItem(ItemId id, uint8_t amount = 1) const {
        uint8_t total = countItem(id);
        return total >= amount;
    }
    
    // Count total of item
    uint8_t countItem(ItemId id) const {
        uint8_t total = 0;
        for (const auto& slot : m_slots) {
            if (slot.id == id) total += slot.count;
        }
        return total;
    }
    
    // Get slot
    const ItemStack& getSlot(size_t index) const {
        return m_slots[index];
    }
    
    ItemStack& getSlot(size_t index) {
        return m_slots[index];
    }
    
    // Selected slot
    size_t selectedSlot() const { return m_selectedSlot; }
    void selectSlot(size_t index) { if (index < MAX_SLOTS) m_selectedSlot = index; }
    void selectNext() { m_selectedSlot = (m_selectedSlot + 1) % MAX_SLOTS; }
    void selectPrev() { m_selectedSlot = (m_selectedSlot + MAX_SLOTS - 1) % MAX_SLOTS; }
    
    const ItemStack& selectedItem() const { return m_slots[m_selectedSlot]; }
    ItemStack& selectedItem() { return m_slots[m_selectedSlot]; }
    
    // Get non-empty items for display
    std::vector<std::pair<size_t, const ItemStack*>> getNonEmptySlots() const {
        std::vector<std::pair<size_t, const ItemStack*>> result;
        for (size_t i = 0; i < MAX_SLOTS; ++i) {
            if (!m_slots[i].isEmpty()) {
                result.push_back({i, &m_slots[i]});
            }
        }
        return result;
    }
    
    bool isEmpty() const {
        for (const auto& slot : m_slots) {
            if (!slot.isEmpty()) return false;
        }
        return true;
    }
    
    size_t usedSlots() const {
        size_t count = 0;
        for (const auto& slot : m_slots) {
            if (!slot.isEmpty()) count++;
        }
        return count;
    }

private:
    std::array<ItemStack, MAX_SLOTS> m_slots;
    size_t m_selectedSlot = 0;
};

// Component for world pickups
struct Pickup {
    ItemId itemId = ItemId::None;
    uint8_t amount = 1;
    float pickupRadius = 1.5f;
    bool collected = false;
};

} // namespace myth
