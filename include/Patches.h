#pragma once

#include <cstdint>

namespace Patches
{
	inline constexpr std::uint32_t PLUGIN_VERSION{ 21 };

	// Relocation::AddressLibrary::Header::Read and Relocation::AddressLibrary::Read.
	inline constexpr std::uint32_t HEADER_READ{ 0x0404A0 };
	inline constexpr std::uint32_t ADDRESS_LIBRARY_READ{ 0x041750 };

	// 1.7.99 added a base class to PlayerCharacter, shifting members above the
	// insertion by 8. Of the 129 offsets ScrambledBugs compiles in, these moved.
	struct Displacement
	{
		std::uint32_t rva;             // start of the instruction
		std::uint8_t  displacementAt;  // where the disp32 sits inside it
		std::uint32_t corrected;
	};

	inline constexpr Displacement DISPLACEMENTS[]{
		// Fixes::WeaponCharge::UpdateEquippedEnchantmentCharge
		{ 0x0060ED, 3, 0xBEB },  // movzx eax, byte ptr [rcx + 0xBE3]
		{ 0x0060FF, 2, 0xBEB },  // mov byte ptr [rcx + 0xBE3], al  <- the write
		// Patches::DifficultyMultipliers::AdjustHealthDamageToDifficulty
		{ 0x00690E, 2, 0xB08 },  // mov ecx, dword ptr [rsi + 0xB00]
	};
}
