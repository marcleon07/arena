#pragma once

#include "CoreMinimal.h"
#include "ArenaTypes.generated.h"

// One Quake/GoldSrc unit expressed in Unreal centimetres (1 unit ~= 1 inch).
constexpr float ArenaUnit = 2.54f;

// Converts a GoldSrc speed (units/s) to Unreal (cm/s), and back.
constexpr float QU(float Units) { return Units * ArenaUnit; }
constexpr float ToQU(float Cm) { return Cm / ArenaUnit; }

/** Sound effects, loaded by name from /Game/Audio/<Name> (see Tools/generate_sounds.py). */
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
	ShotgunFire,
	GrenadeFire,
	GrenadeBounce,
	LightningFire,
	PlasmaFire,
	PlasmaExplode,
	GauntletFire,
	Count UMETA(Hidden)
};

/** Quake 3 order; number keys 1-8 select them in this order. */
UENUM(BlueprintType)
enum class EArenaWeapon : uint8
{
	Gauntlet,
	MachineGun,
	Shotgun,
	GrenadeLauncher,
	RocketLauncher,
	LightningGun,
	Railgun,
	PlasmaGun,
	Count UMETA(Hidden)
};

constexpr int32 ArenaWeaponCount = static_cast<int32>(EArenaWeapon::Count);

struct FArenaWeaponInfo
{
	const TCHAR* Name;
	const TCHAR* ShortName;
	float RefireTime;
	/** Per pellet / per hit / direct hit. */
	float Damage;
	int32 StartAmmo;
	int32 PickupAmmo;
	/** Negative: needs no ammo. */
	int32 MaxAmmo;
	FLinearColor Color;
	EArenaSound FireSound;
	/** Projectile weapons spawn an AArenaProjectile subclass; the rest are hitscan. */
	bool bProjectile;

	// Hitscan only.
	int32 Pellets;
	float SpreadDegrees;
	float Range;
	/** How many players one trace can pass through (railgun). */
	int32 MaxPierce;
	/** Tracer drawn from the gun; 0 width draws none. */
	float TracerWidth;
	float TracerLife;

	bool UsesAmmo() const { return MaxAmmo >= 0; }
};

inline const FArenaWeaponInfo& GetWeaponInfo(EArenaWeapon Weapon)
{
	// Quake 3 values; ranges and speeds are in Quake units via QU().
	static const FArenaWeaponInfo Infos[] =
	{
		//  Name                ShortName    Refire  Damage Start Pickup Max   Color                               FireSound                     Proj.  Pellets Spread Range           Pierce Tracer Life
		{ TEXT("Gauntlet"),         TEXT("GA"), 0.40f,  50.f,   0,   0,  -1, FLinearColor(0.30f, 0.60f, 1.00f), EArenaSound::GauntletFire,   false, 1, 0.f,  QU(64.f),  1, 0.f, 0.f },
		{ TEXT("Machinegun"),       TEXT("MG"), 0.10f,   7.f, 100,  50, 200, FLinearColor(0.95f, 0.80f, 0.20f), EArenaSound::MachineGunFire, false, 1, 1.2f, 50000.f,   1, 1.2f, 0.07f },
		{ TEXT("Shotgun"),          TEXT("SG"), 1.00f,  10.f,  10,  10, 200, FLinearColor(1.00f, 0.60f, 0.20f), EArenaSound::ShotgunFire,    false, 11, 4.5f, 50000.f,  1, 1.0f, 0.10f },
		{ TEXT("Grenade Launcher"), TEXT("GL"), 0.80f, 100.f,  10,  10, 200, FLinearColor(0.35f, 0.75f, 0.20f), EArenaSound::GrenadeFire,    true,  0, 0.f,  0.f,       0, 0.f, 0.f },
		{ TEXT("Rocket Launcher"),  TEXT("RL"), 0.80f, 100.f,  10,   5,  50, FLinearColor(0.95f, 0.25f, 0.10f), EArenaSound::RocketFire,     true,  0, 0.f,  0.f,       0, 0.f, 0.f },
		{ TEXT("Lightning Gun"),    TEXT("LG"), 0.05f,   8.f, 100, 100, 200, FLinearColor(0.55f, 0.80f, 1.00f), EArenaSound::LightningFire,  false, 1, 0.f,  QU(768.f), 1, 3.0f, 0.06f },
		{ TEXT("Railgun"),          TEXT("RG"), 1.50f, 100.f,  10,   5,  50, FLinearColor(0.20f, 0.95f, 0.35f), EArenaSound::RailFire,       false, 1, 0.f,  50000.f,   4, 5.0f, 0.90f },
		{ TEXT("Plasma Gun"),       TEXT("PG"), 0.10f,  20.f,  50,  50, 200, FLinearColor(0.45f, 0.45f, 1.00f), EArenaSound::PlasmaFire,     true,  0, 0.f,  0.f,       0, 0.f, 0.f },
	};
	static_assert(UE_ARRAY_COUNT(Infos) == ArenaWeaponCount, "One entry per EArenaWeapon");
	return Infos[FMath::Clamp(static_cast<int32>(Weapon), 0, ArenaWeaponCount - 1)];
}

/** Quake knockback: velocity += dir * min(damage, 200) * 1000 / mass(200). */
inline FVector ArenaKnockback(const FVector& Dir, float Damage)
{
	return Dir.GetSafeNormal() * QU(5.f * FMath::Min(Damage, 200.f));
}

UENUM(BlueprintType)
enum class EArenaPickupType : uint8
{
	Health,     // +25, up to 100
	MegaHealth, // +100, up to 200
	Armor,      // +50
	HeavyArmor, // +100
	Shotgun,
	GrenadeLauncher,
	RocketLauncher,
	LightningGun,
	Railgun,
	PlasmaGun,
	Ammo        // Ammo for every weapon you own
};

/** The weapon a pickup gives, if it is a weapon pickup. */
inline bool GetPickupWeapon(EArenaPickupType Type, EArenaWeapon& OutWeapon)
{
	switch (Type)
	{
	case EArenaPickupType::Shotgun:         OutWeapon = EArenaWeapon::Shotgun; return true;
	case EArenaPickupType::GrenadeLauncher: OutWeapon = EArenaWeapon::GrenadeLauncher; return true;
	case EArenaPickupType::RocketLauncher:  OutWeapon = EArenaWeapon::RocketLauncher; return true;
	case EArenaPickupType::LightningGun:    OutWeapon = EArenaWeapon::LightningGun; return true;
	case EArenaPickupType::Railgun:         OutWeapon = EArenaWeapon::Railgun; return true;
	case EArenaPickupType::PlasmaGun:       OutWeapon = EArenaWeapon::PlasmaGun; return true;
	default:                                return false;
	}
}
