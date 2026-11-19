#include "player/Inventory.h"

#include <iostream>
#include <algorithm>
#include <filesystem>
#include <fstream>

// Updates hotbar cursor
void Inventory::Scroll(int delta) noexcept
{
	m_hotbarIndex = (m_hotbarIndex + delta + HOTBAR_SIZE) % HOTBAR_SIZE;
}

bool Inventory::AddBlock(BlockType type)
{
	// Try adding to the current hotbar index
	if (m_slots[m_hotbarIndex].IsEmpty())
	{
		m_slots[m_hotbarIndex] = { type, 1 };
		return true;
	}

	// If already present
	for (int i = 0; i < INVENTORY_SIZE; ++i)
	{
		auto& slot = m_slots[i];

		if (slot.type == type)
		{
			if (i < 9)
				m_hotbarIndex = i;
			else
				std::swap(slot, m_slots[m_hotbarIndex]);

			return true;
		}
	}

	// If not present, add new
	for (int i = 0; i < INVENTORY_SIZE; ++i)
	{
		auto& slot = m_slots[i];

		if (slot.IsEmpty())
		{
			slot.type = type;
			slot.count = 1;

			if (i < 9)
				m_hotbarIndex = i;
			else
				std::swap(m_slots[i], m_slots[m_hotbarIndex]);

			return true;
		}
	}

	return false;	// full
}

void Inventory::RemoveFromSlot(int index)
{
	if (index < 0 || index >= INVENTORY_SIZE)
		return;

	m_slots[index] = {};
}

void Inventory::ClickSlot(int index) noexcept
{
	if (index < 0 || index >= INVENTORY_SIZE)
		return;

	std::swap(m_dragItem, m_slots[index]);
}

void Inventory::Dump() noexcept
{
	// Dump held item
	if (!m_dragItem.IsEmpty())
	{
		AddBlock(m_dragItem.type);
		m_dragItem = {};
	}
}

void Inventory::ClearHeld() noexcept
{
	m_dragItem = {};
}

bool Inventory::Load(const std::string& filepath)
{
	inventorySaveFilePath = filepath;

	std::ifstream f(filepath, std::ios::binary);

	if (!f)
		return false;

	f.read(reinterpret_cast<char*>(m_slots.data()), INVENTORY_SIZE * sizeof(ItemStack));
	return f.good();
}

bool Inventory::Save()
{
	std::filesystem::create_directories("saves");
	std::ofstream f(inventorySaveFilePath, std::ios::binary);

	if (!f)
		return false;

	f.write(reinterpret_cast<char*>(m_slots.data()), INVENTORY_SIZE * sizeof(ItemStack));
	return f.good();
}