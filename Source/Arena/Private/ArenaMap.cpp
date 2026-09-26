#include "ArenaMap.h"
#include "Arena.h"
#include "ArenaAudio.h"
#include "ArenaCharacter.h"
#include "ArenaVisuals.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ArenaMap"

// All values in cm. Each map lists its geometry, spawns and items; see the
// sketches above each Build function.

namespace
{
	constexpr float CapsuleHalfHeight = QU(36.f);
	constexpr float Hover = 50.f; // Item height above the floor.

	const FLinearColor FloorColor(0.10f, 0.10f, 0.12f);
	const FLinearColor WallColor(0.32f, 0.16f, 0.10f);
	const FLinearColor PlatformColor(0.55f, 0.33f, 0.12f);
	const FLinearColor TowerColor(0.28f, 0.28f, 0.33f);
	const FLinearColor BridgeColor(0.22f, 0.25f, 0.30f);
	const FLinearColor PillarColor(0.40f, 0.40f, 0.42f);
	const FLinearColor CrateColor(0.45f, 0.30f, 0.15f);
	const FLinearColor PadColor(0.10f, 0.70f, 0.95f);
	const FLinearColor StoneColor(0.30f, 0.32f, 0.36f);
	const FLinearColor IslandColor(0.20f, 0.35f, 0.30f);
	const FLinearColor SandColor(0.50f, 0.38f, 0.22f);
	const FLinearColor CliffColor(0.38f, 0.22f, 0.14f);

	void SpawnBlock(UWorld* World, const FVector& Center, const FVector& HalfExtent, const FLinearColor& Color, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AArenaBlock* Block = World->SpawnActor<AArenaBlock>(Center, Rotation, Params))
		{
			Block->Init(HalfExtent, Color);
		}
	}

	/** A ramp whose top surface runs from Bottom to Top (edge midpoints). */
	void SpawnRamp(UWorld* World, const FVector& Bottom, const FVector& Top, float Width, const FLinearColor& Color = PlatformColor)
	{
		constexpr float Thickness = 40.f;
		const FVector Dir = Top - Bottom;
		const FRotator Rotation = Dir.Rotation();
		const FVector Up = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z);
		const FVector Center = (Bottom + Top) * 0.5f - Up * Thickness * 0.5f;
		SpawnBlock(World, Center, FVector(Dir.Size() * 0.5f + 10.f, Width * 0.5f, Thickness * 0.5f), Color, Rotation);
	}

	/** Ballistic launch that peaks ApexAbove over the higher endpoint and lands on To. */
	FVector SolveLaunch(const FVector& From, const FVector& To, float ApexAbove)
	{
		const float Gravity = QU(800.f);
		const float ApexZ = FMath::Max(From.Z, To.Z) + ApexAbove;
		const float Vz = FMath::Sqrt(2.f * Gravity * (ApexZ - From.Z));
		const float FlightTime = Vz / Gravity + FMath::Sqrt(2.f * (ApexZ - To.Z) / Gravity);
		FVector Horizontal = To - From;
		Horizontal.Z = 0.f;
		return Horizontal / FlightTime + FVector(0.f, 0.f, Vz);
	}

	void SpawnJumpPad(UWorld* World, const FVector& FloorLocation, const FVector& TargetFloor, float ApexAbove = 500.f)
	{
		const FVector From = FloorLocation + FVector(0.f, 0.f, CapsuleHalfHeight);
		const FVector To = TargetFloor + FVector(0.f, 0.f, CapsuleHalfHeight);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AArenaJumpPad* Pad = World->SpawnActor<AArenaJumpPad>(FloorLocation, FRotator::ZeroRotator, Params))
		{
			Pad->Init(SolveLaunch(From, To, ApexAbove), TargetFloor);
		}
	}

	/** Spawn at Feet, facing the middle of the map. */
	FTransform MakeSpawn(const FVector& Feet)
	{
		const FVector Location = Feet + FVector(0.f, 0.f, CapsuleHalfHeight + 10.f);
		const FVector ToCenter = FVector(-Location.X, -Location.Y, 0.f);
		return FTransform(FRotator(0.f, ToCenter.IsNearlyZero() ? 0.f : ToCenter.Rotation().Yaw, 0.f), Location);
	}

	// -----------------------------------------------------------------------
	// Courtyard: walled square, central platform, corner towers, bridge ring.
	//
	//   +------------------------------------------+   Towers (T) in the corners, 8 m tall,
	//   | T==========bridge (armor)============T   |   joined by a ring of bridges.
	//   | |     [crate]    RL    [crate]       |   |   Jump pads (J) launch you onto them.
	//   | |  J       |              |      J   |   |
	//   | |       P  |   ramp       |  P       |   |   Central platform (C) with MegaHealth,
	//   |rail  ramp==C==ramp               rail|   |   reached by four ramps.
	//   | |       P  |   ramp       |  P       |   |
	//   | |  J       |              |      J   |   |   Pillars (P) for cover.
	//   | |     [crate]    RL    [crate]       |   |
	//   | T=======bridge (heavy armor)=========T   |
	//   +------------------------------------------+
	// -----------------------------------------------------------------------

	namespace Courtyard
	{
		constexpr float TowerTop = 800.f;
		constexpr float PlatformTop = 400.f;

		void Build(UWorld* World)
		{
			SpawnBlock(World, FVector(0.f, 0.f, -50.f), FVector(3300.f, 3300.f, 50.f), FloorColor);
			for (const float S : { -1.f, 1.f })
			{
				SpawnBlock(World, FVector(S * 3250.f, 0.f, 1000.f), FVector(50.f, 3300.f, 1000.f), WallColor);
				SpawnBlock(World, FVector(0.f, S * 3250.f, 1000.f), FVector(3300.f, 50.f, 1000.f), WallColor);
			}

			SpawnBlock(World, FVector(0.f, 0.f, PlatformTop * 0.5f), FVector(700.f, 700.f, PlatformTop * 0.5f), PlatformColor);
			for (const float S : { -1.f, 1.f })
			{
				SpawnRamp(World, FVector(S * 1900.f, 0.f, 0.f), FVector(S * 700.f, 0.f, PlatformTop), 400.f);
				SpawnRamp(World, FVector(0.f, S * 1900.f, 0.f), FVector(0.f, S * 700.f, PlatformTop), 400.f);
			}

			for (const float X : { -1.f, 1.f })
			{
				for (const float Y : { -1.f, 1.f })
				{
					SpawnBlock(World, FVector(X * 2300.f, Y * 2300.f, TowerTop * 0.5f), FVector(500.f, 500.f, TowerTop * 0.5f), TowerColor);
					SpawnJumpPad(World, FVector(X * 1300.f, Y * 1300.f, 0.f), FVector(X * 2300.f, Y * 2300.f, TowerTop));
					SpawnBlock(World, FVector(X * 1400.f, Y * 600.f, 500.f), FVector(100.f, 100.f, 500.f), PillarColor);
					SpawnBlock(World, FVector(X * 600.f, Y * 1400.f, 500.f), FVector(100.f, 100.f, 500.f), PillarColor);
					SpawnBlock(World, FVector(X * 1000.f, Y * 2400.f, 50.f), FVector(150.f, 150.f, 50.f), CrateColor);
				}
			}

			for (const float S : { -1.f, 1.f })
			{
				SpawnBlock(World, FVector(0.f, S * 2300.f, TowerTop - 25.f), FVector(1800.f, 250.f, 25.f), BridgeColor);
				SpawnBlock(World, FVector(S * 2300.f, 0.f, TowerTop - 25.f), FVector(250.f, 1800.f, 25.f), BridgeColor);
			}
		}

		FArenaMapDef Make()
		{
			FArenaMapDef Map;
			Map.Id = TEXT("Courtyard");
			Map.DisplayName = LOCTEXT("CourtyardName", "Courtyard");
			Map.Description = LOCTEXT("CourtyardDesc", "Walled square with corner towers, a bridge ring and a central platform.");
			Map.BuildGeometry = &Build;
			Map.KillZ = -1000.f;
			Map.MenuOrbitRadius = 2800.f;
			Map.MenuOrbitHeight = 1600.f;
			for (const float S : { -1.f, 1.f })
			{
				Map.Spawns.Add(MakeSpawn(FVector(S * 2700.f, 0.f, 0.f)));
				Map.Spawns.Add(MakeSpawn(FVector(0.f, S * 2700.f, 0.f)));
				Map.Spawns.Add(MakeSpawn(FVector(S * 2300.f, S * 2300.f, TowerTop)));
				Map.Spawns.Add(MakeSpawn(FVector(S * 2300.f, -S * 2300.f, TowerTop)));
			}
			TArray<FArenaPickupSpot>& P = Map.Pickups;
			P.Add({ EArenaPickupType::MegaHealth, FVector(0.f, 0.f, PlatformTop + Hover) });
			P.Add({ EArenaPickupType::Armor, FVector(0.f, 2300.f, TowerTop + Hover) });
			P.Add({ EArenaPickupType::HeavyArmor, FVector(0.f, -2300.f, TowerTop + Hover) });
			P.Add({ EArenaPickupType::Railgun, FVector(2300.f, 0.f, TowerTop + Hover) });
			P.Add({ EArenaPickupType::Railgun, FVector(-2300.f, 0.f, TowerTop + Hover) });
			P.Add({ EArenaPickupType::RocketLauncher, FVector(0.f, 2200.f, Hover) });
			P.Add({ EArenaPickupType::RocketLauncher, FVector(0.f, -2200.f, Hover) });
			P.Add({ EArenaPickupType::Shotgun, FVector(2000.f, -1000.f, Hover) });
			P.Add({ EArenaPickupType::Shotgun, FVector(-2000.f, 1000.f, Hover) });
			P.Add({ EArenaPickupType::GrenadeLauncher, FVector(2000.f, 1000.f, Hover) });
			P.Add({ EArenaPickupType::GrenadeLauncher, FVector(-2000.f, -1000.f, Hover) });
			P.Add({ EArenaPickupType::LightningGun, FVector(-1200.f, 2300.f, TowerTop + Hover) });
			P.Add({ EArenaPickupType::LightningGun, FVector(1200.f, -2300.f, TowerTop + Hover) });
			P.Add({ EArenaPickupType::PlasmaGun, FVector(1200.f, 2300.f, TowerTop + Hover) });
			P.Add({ EArenaPickupType::PlasmaGun, FVector(-1200.f, -2300.f, TowerTop + Hover) });
			for (const float X : { -1.f, 1.f })
			{
				for (const float Y : { -1.f, 1.f })
				{
					P.Add({ EArenaPickupType::Health, FVector(X * 1000.f, Y * 1000.f, Hover) });
					P.Add({ EArenaPickupType::Ammo, FVector(X * 1000.f, Y * 2400.f, 100.f + Hover) });
				}
			}
			return Map;
		}
	}

	// -----------------------------------------------------------------------
	// Skyline: floating islands over a void (in the spirit of Q3's Longest Yard).
	//
	//                  [LG island]
	//                       ^
	//        [corner]       J       [corner]      Corners sit lower and further out:
	//                 +-----------+               bhop across the gap from the main
	//   [rail isl.] <J|  main (RL) |J> [rail isl.] platform, then take their pads up to
	//                 +-----------+               the high platform (Mega) above the centre.
	//        [corner]       J       [corner]
	//                       v                     Fall off and you die.
	//                  [LG island]
	// -----------------------------------------------------------------------

	namespace Skyline
	{
		constexpr float IslandTop = 300.f;
		constexpr float CornerTop = -150.f;
		constexpr float HighTop = 1000.f;
		constexpr float IslandDist = 3300.f;
		constexpr float CornerDist = 1800.f;

		void Build(UWorld* World)
		{
			// Main platform.
			SpawnBlock(World, FVector(0.f, 0.f, -60.f), FVector(900.f, 900.f, 60.f), StoneColor);

			for (const float S : { -1.f, 1.f })
			{
				// Side islands, with pads out from the main platform and back.
				SpawnBlock(World, FVector(S * IslandDist, 0.f, IslandTop - 60.f), FVector(500.f, 500.f, 60.f), IslandColor);
				SpawnBlock(World, FVector(0.f, S * IslandDist, IslandTop - 60.f), FVector(500.f, 500.f, 60.f), IslandColor);
				SpawnJumpPad(World, FVector(S * 700.f, 0.f, 0.f), FVector(S * IslandDist, 0.f, IslandTop));
				SpawnJumpPad(World, FVector(0.f, S * 700.f, 0.f), FVector(0.f, S * IslandDist, IslandTop));
				SpawnJumpPad(World, FVector(S * (IslandDist - 350.f), 0.f, IslandTop), FVector(S * 300.f, 0.f, 0.f));
				SpawnJumpPad(World, FVector(0.f, S * (IslandDist - 350.f), IslandTop), FVector(0.f, S * 300.f, 0.f));
			}

			for (const float X : { -1.f, 1.f })
			{
				for (const float Y : { -1.f, 1.f })
				{
					// Lower corner platforms: a bhop gap from the main platform, pads up to the high one.
					SpawnBlock(World, FVector(X * CornerDist, Y * CornerDist, CornerTop - 40.f), FVector(350.f, 350.f, 40.f), IslandColor);
					SpawnJumpPad(World, FVector(X * (CornerDist + 150.f), Y * (CornerDist + 150.f), CornerTop), FVector(X * 150.f, Y * 150.f, HighTop));
				}
			}

			// High platform over the centre, and a few floating cover blocks.
			SpawnBlock(World, FVector(0.f, 0.f, HighTop - 40.f), FVector(350.f, 350.f, 40.f), StoneColor);
			for (const float S : { -1.f, 1.f })
			{
				SpawnBlock(World, FVector(S * 450.f, S * -450.f, 120.f), FVector(90.f, 90.f, 120.f), PillarColor);
			}
		}

		FArenaMapDef Make()
		{
			FArenaMapDef Map;
			Map.Id = TEXT("Skyline");
			Map.DisplayName = LOCTEXT("SkylineName", "Skyline");
			Map.Description = LOCTEXT("SkylineDesc", "Floating islands over a void. Jump pads, bhop gaps, and a long way down.");
			Map.BuildGeometry = &Build;
			Map.KillZ = -1500.f;
			Map.MenuOrbitRadius = 4200.f;
			Map.MenuOrbitHeight = 1900.f;
			for (const float S : { -1.f, 1.f })
			{
				Map.Spawns.Add(MakeSpawn(FVector(S * 450.f, S * 450.f, 0.f)));
				Map.Spawns.Add(MakeSpawn(FVector(S * IslandDist + S * 200.f, 0.f, IslandTop)));
				Map.Spawns.Add(MakeSpawn(FVector(0.f, S * IslandDist + S * 200.f, IslandTop)));
				Map.Spawns.Add(MakeSpawn(FVector(S * CornerDist, -S * CornerDist, CornerTop)));
			}
			TArray<FArenaPickupSpot>& P = Map.Pickups;
			P.Add({ EArenaPickupType::MegaHealth, FVector(0.f, 0.f, HighTop + Hover) });
			P.Add({ EArenaPickupType::RocketLauncher, FVector(0.f, 0.f, Hover) });
			for (const float S : { -1.f, 1.f })
			{
				P.Add({ EArenaPickupType::Railgun, FVector(S * (IslandDist + 150.f), 0.f, IslandTop + Hover) });
				P.Add({ EArenaPickupType::LightningGun, FVector(0.f, S * (IslandDist + 150.f), IslandTop + Hover) });
				P.Add({ EArenaPickupType::Ammo, FVector(S * IslandDist, S * 300.f, IslandTop + Hover) });
				P.Add({ EArenaPickupType::Ammo, FVector(S * 300.f, S * IslandDist, IslandTop + Hover) });
				P.Add({ EArenaPickupType::Shotgun, FVector(S * 400.f, 0.f, Hover) });
				P.Add({ EArenaPickupType::GrenadeLauncher, FVector(0.f, S * 400.f, Hover) });
			}
			P.Add({ EArenaPickupType::HeavyArmor, FVector(CornerDist, CornerDist, CornerTop + Hover) });
			P.Add({ EArenaPickupType::Armor, FVector(-CornerDist, -CornerDist, CornerTop + Hover) });
			P.Add({ EArenaPickupType::PlasmaGun, FVector(CornerDist, -CornerDist, CornerTop + Hover) });
			P.Add({ EArenaPickupType::PlasmaGun, FVector(-CornerDist, CornerDist, CornerTop + Hover) });
			for (const float X : { -1.f, 1.f })
			{
				for (const float Y : { -1.f, 1.f })
				{
					P.Add({ EArenaPickupType::Health, FVector(X * 700.f, Y * 700.f, Hover) });
				}
			}
			return Map;
		}
	}

	// -----------------------------------------------------------------------
	// Canyon: long walled strip; a valley in the middle you can ramp through for
	// speed or cross on the bridge; raised bases at each end.
	//
	//   +-----------------------------------------------------------------+
	//   | base  |   J    P            \  valley  /             P   J |  base |
	//   | (rail)|<ramp            ==== bridge (RL, Mega below) ====   ramp>| (rail)|
	//   |       |   J    P            /          \             P   J |       |
	//   +-----------------------------------------------------------------+
	// -----------------------------------------------------------------------

	namespace Canyon
	{
		constexpr float HalfLength = 4500.f;
		constexpr float HalfWidth = 1300.f;
		constexpr float ValleyDepth = -400.f;
		constexpr float BaseTop = 300.f;
		constexpr float BaseInner = 3200.f;

		void Build(UWorld* World)
		{
			for (const float S : { -1.f, 1.f })
			{
				// Floors either side of the valley, and ramps down into it (26 degrees, walkable).
				SpawnBlock(World, FVector(S * 3000.f, 0.f, -50.f), FVector(1500.f, HalfWidth, 50.f), SandColor);
				SpawnRamp(World, FVector(S * 700.f, 0.f, ValleyDepth), FVector(S * 1500.f, 0.f, 0.f), HalfWidth * 2.f, SandColor);

				// Raised bases at the ends, with a ramp and two jump pads up.
				SpawnBlock(World, FVector(S * (BaseInner + 650.f), 0.f, BaseTop * 0.5f), FVector(650.f, HalfWidth, BaseTop * 0.5f), CliffColor);
				SpawnRamp(World, FVector(S * 2400.f, 0.f, 0.f), FVector(S * BaseInner, 0.f, BaseTop), 700.f, CliffColor);
				SpawnJumpPad(World, FVector(S * 2700.f, 900.f, 0.f), FVector(S * 3850.f, 900.f, BaseTop), 300.f);
				SpawnJumpPad(World, FVector(S * 2700.f, -900.f, 0.f), FVector(S * 3850.f, -900.f, BaseTop), 300.f);

				// Walls.
				SpawnBlock(World, FVector(0.f, S * (HalfWidth + 50.f), 350.f), FVector(HalfLength + 50.f, 50.f, 850.f), WallColor);
				SpawnBlock(World, FVector(S * (HalfLength + 50.f), 0.f, 350.f), FVector(50.f, HalfWidth + 100.f, 850.f), WallColor);

				// Cover.
				SpawnBlock(World, FVector(S * 2200.f, 800.f, 400.f), FVector(120.f, 120.f, 400.f), PillarColor);
				SpawnBlock(World, FVector(S * 2200.f, -800.f, 400.f), FVector(120.f, 120.f, 400.f), PillarColor);
				SpawnBlock(World, FVector(0.f, S * 800.f, ValleyDepth + 300.f), FVector(100.f, 100.f, 300.f), PillarColor);
			}

			// Valley floor and the bridge over it.
			SpawnBlock(World, FVector(0.f, 0.f, ValleyDepth - 50.f), FVector(700.f, HalfWidth, 50.f), SandColor);
			SpawnBlock(World, FVector(0.f, 0.f, -25.f), FVector(1500.f, 200.f, 25.f), BridgeColor);
		}

		FArenaMapDef Make()
		{
			FArenaMapDef Map;
			Map.Id = TEXT("Canyon");
			Map.DisplayName = LOCTEXT("CanyonName", "Canyon");
			Map.Description = LOCTEXT("CanyonDesc", "Long strip with raised bases and a valley to ramp through for speed.");
			Map.BuildGeometry = &Build;
			Map.KillZ = -1500.f;
			Map.MenuOrbitRadius = 3600.f;
			Map.MenuOrbitHeight = 2200.f;
			for (const float S : { -1.f, 1.f })
			{
				Map.Spawns.Add(MakeSpawn(FVector(S * 4200.f, 700.f, BaseTop)));
				Map.Spawns.Add(MakeSpawn(FVector(S * 4200.f, -700.f, BaseTop)));
				Map.Spawns.Add(MakeSpawn(FVector(S * 2500.f, S * 1100.f, 0.f)));
				Map.Spawns.Add(MakeSpawn(FVector(S * 1800.f, -S * 400.f, 0.f)));
			}
			TArray<FArenaPickupSpot>& P = Map.Pickups;
			P.Add({ EArenaPickupType::MegaHealth, FVector(0.f, 0.f, ValleyDepth + Hover) });
			P.Add({ EArenaPickupType::RocketLauncher, FVector(0.f, 0.f, Hover) });
			P.Add({ EArenaPickupType::HeavyArmor, FVector(0.f, 1000.f, ValleyDepth + Hover) });
			P.Add({ EArenaPickupType::Armor, FVector(0.f, -1000.f, ValleyDepth + Hover) });
			for (const float S : { -1.f, 1.f })
			{
				P.Add({ EArenaPickupType::Railgun, FVector(S * 4200.f, 0.f, BaseTop + Hover) });
				P.Add({ EArenaPickupType::PlasmaGun, FVector(S * 3700.f, S * 1000.f, BaseTop + Hover) });
				P.Add({ EArenaPickupType::Health, FVector(S * 3600.f, 400.f, BaseTop + Hover) });
				P.Add({ EArenaPickupType::Health, FVector(S * 3600.f, -400.f, BaseTop + Hover) });
				P.Add({ EArenaPickupType::LightningGun, FVector(S * 2200.f, 0.f, Hover) });
				P.Add({ EArenaPickupType::Shotgun, FVector(S * 1800.f, S * 1000.f, Hover) });
				P.Add({ EArenaPickupType::GrenadeLauncher, FVector(S * 1800.f, -S * 1000.f, Hover) });
				P.Add({ EArenaPickupType::Ammo, FVector(S * 3000.f, 1100.f, Hover) });
				P.Add({ EArenaPickupType::Ammo, FVector(S * 3000.f, -1100.f, Hover) });
			}
			return Map;
		}
	}

	void BuildLighting(UWorld* World)
	{
		const FTransform SunTransform(FRotator(-48.f, 35.f, 0.f));
		ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), SunTransform);
		if (Sun)
		{
			if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
			{
				SunLight->SetMobility(EComponentMobility::Movable);
				SunLight->SetIntensity(8.f);
				SunLight->SetAtmosphereSunLight(true);
			}
			Sun->FinishSpawning(SunTransform);
		}

		World->SpawnActor<ASkyAtmosphere>();

		ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity);
		if (Sky)
		{
			if (USkyLightComponent* SkyLight = Sky->GetLightComponent())
			{
				SkyLight->SetMobility(EComponentMobility::Movable);
				SkyLight->bRealTimeCapture = true;
				SkyLight->SetIntensity(1.5f);
			}
			Sky->FinishSpawning(FTransform::Identity);
		}

		World->SpawnActor<AExponentialHeightFog>(FVector(0.f, 0.f, -500.f), FRotator::ZeroRotator);
	}
}

const TArray<FArenaMapDef>& ArenaMap::GetMaps()
{
	static const TArray<FArenaMapDef> Maps = { Courtyard::Make(), Skyline::Make(), Canyon::Make() };
	return Maps;
}

const FArenaMapDef& ArenaMap::Get(FName Id)
{
	const TArray<FArenaMapDef>& Maps = GetMaps();
	const FArenaMapDef* Found = Maps.FindByPredicate([Id](const FArenaMapDef& Map) { return Map.Id == Id; });
	return Found ? *Found : Maps[0];
}

void ArenaMap::BuildLocal(UWorld* World, FName MapId)
{
	if (!World)
	{
		return;
	}
	const FArenaMapDef& Map = Get(MapId);
	Map.BuildGeometry(World);
	UE_LOG(LogArena, Log, TEXT("Built %s (%s)"), *Map.Id.ToString(), World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"));
	if (World->GetNetMode() != NM_DedicatedServer)
	{
		BuildLighting(World);
	}
}

#undef LOCTEXT_NAMESPACE

// ---------------------------------------------------------------------------
// AArenaBlock
// ---------------------------------------------------------------------------

AArenaBlock::AArenaBlock()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RootComponent = Mesh;
}

void AArenaBlock::Init(const FVector& HalfExtent, const FLinearColor& Color)
{
	Mesh->SetStaticMesh(ArenaVisuals::Cube());
	SetActorScale3D(HalfExtent / 50.f); // Cube mesh is 100 cm across.
	ArenaVisuals::SetColor(Mesh, Color);
}

// ---------------------------------------------------------------------------
// AArenaJumpPad
// ---------------------------------------------------------------------------

AArenaJumpPad::AArenaJumpPad()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->InitBoxExtent(FVector(80.f, 80.f, 40.f));
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	ArenaVisuals::SetupCosmeticMesh(Mesh, nullptr);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, -35.f));
	Mesh->SetRelativeScale3D(FVector(1.6f, 1.6f, 0.1f));
}

void AArenaJumpPad::Init(const FVector& InLaunchVelocity, const FVector& InTarget)
{
	LaunchVelocity = InLaunchVelocity;
	Target = InTarget;
	Mesh->SetStaticMesh(ArenaVisuals::Cylinder());
	ArenaVisuals::SetColor(Mesh, PadColor);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AArenaJumpPad::OnOverlap);
}

void AArenaJumpPad::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	AArenaCharacter* Character = Cast<AArenaCharacter>(Other);
	if (!Character || Character->IsDead())
	{
		return;
	}
	UArenaAudio::PlayAt(this, EArenaSound::JumpPad, GetActorLocation());
	// Launch where movement is simulated authoritatively or predicted.
	if (Character->HasAuthority() || Character->IsLocallyControlled())
	{
		Character->LaunchCharacter(LaunchVelocity, true, true);
	}
}
