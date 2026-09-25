#pragma once

#include "CoreMinimal.h"
#include "ArenaTypes.generated.h"

// One Quake/GoldSrc unit expressed in Unreal centimetres (1 unit ~= 1 inch).
constexpr float ArenaUnit = 2.54f;

// Converts a GoldSrc speed (units/s) to Unreal (cm/s), and back.
constexpr float QU(float Units) { return Units * ArenaUnit; }
constexpr float ToQU(float Cm) { return Cm / ArenaUnit; }

UENUM(BlueprintType)
enum class EArenaWeapon : uint8
{
	MachineGun,
	RocketLauncher,
	Railgun,
	Count UMETA(Hidden)
};

struct FArenaWeaponInfo
{
	const TCHAR* Name;
	float RefireTime;
	float Damage;
	int32 StartAmmo;
	int32 PickupAmmo;
	int32 MaxAmmo;
	FLinearColor Color;
};

inline const FArenaWeaponInfo& GetWeaponInfo(EArenaWeapon Weapon)
{
	static const FArenaWeaponInfo Infos[] =
	{
		{ TEXT("Machinegun"),      0.10f,   7.f, 100, 50, 200, FLinearColor(0.95f, 0.80f, 0.20f) },
		{ TEXT("Rocket Launcher"), 0.80f, 100.f,  10,  5,  50, FLinearColor(0.95f, 0.25f, 0.10f) },
		{ TEXT("Railgun"),         1.50f, 100.f,  10,  5,  50, FLinearColor(0.20f, 0.95f, 0.35f) },
	};
	return Infos[FMath::Clamp(static_cast<int32>(Weapon), 0, static_cast<int32>(UE_ARRAY_COUNT(Infos)) - 1)];
}

constexpr int32 ArenaWeaponCount = static_cast<int32>(EArenaWeapon::Count);

/** Quake knockback: velocity += dir * min(damage, 200) * 1000 / mass(200). */
inline FVector ArenaKnockback(const FVector& Dir, float Damage)
{
	return Dir.GetSafeNormal() * QU(5.f * FMath::Min(Damage, 200.f));
}

/** Sound effects, loaded from /Game/Audio/<Name>. Order matches Tools/generate_sounds.py. */
UENUM(BlueprintType)
enum class EArenaSound : uint8
{
	None,
	Hit,
	Kill,
	MachineGunFire,
	RocketFire,
	RocketExplode,
	RailFire,
	Jump,
	Land,
	Footstep,
	Pain,
	Pickup,
	WeaponPickup,
	JumpPad,
	Death,
	Spawn,
	NoAmmo,
	Count UMETA(Hidden)
};

inline EArenaSound GetFireSound(EArenaWeapon Weapon)
{
	switch (Weapon)
	{
	case EArenaWeapon::RocketLauncher: return EArenaSound::RocketFire;
	case EArenaWeapon::Railgun:        return EArenaSound::RailFire;
	default:                           return EArenaSound::MachineGunFire;
	}
}

UENUM(BlueprintType)
enum class EArenaPickupType : uint8
{
	Health,     // +25, up to 100
	MegaHealth, // +100, up to 200
	Armor,      // +50
	HeavyArmor, // +100
	RocketLauncher,
	Railgun,
	Ammo        // Ammo for every weapon you own
};
