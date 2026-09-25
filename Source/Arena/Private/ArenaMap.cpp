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

// ---------------------------------------------------------------------------
// Layout (all values in cm; floor top is Z = 0)
// ---------------------------------------------------------------------------
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

namespace
{
	constexpr float CapsuleHalfHeight = QU(36.f);
	constexpr float TowerTop = 800.f;
	constexpr float PlatformTop = 400.f;

	const FLinearColor FloorColor(0.10f, 0.10f, 0.12f);
	const FLinearColor WallColor(0.32f, 0.16f, 0.10f);
	const FLinearColor PlatformColor(0.55f, 0.33f, 0.12f);
	const FLinearColor TowerColor(0.28f, 0.28f, 0.33f);
	const FLinearColor BridgeColor(0.22f, 0.25f, 0.30f);
	const FLinearColor PillarColor(0.40f, 0.40f, 0.42f);
	const FLinearColor CrateColor(0.45f, 0.30f, 0.15f);
	const FLinearColor PadColor(0.10f, 0.70f, 0.95f);

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
	void SpawnRamp(UWorld* World, const FVector& Bottom, const FVector& Top, float Width)
	{
		constexpr float Thickness = 40.f;
		const FVector Dir = Top - Bottom;
		const FRotator Rotation = Dir.Rotation();
		const FVector Up = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z);
		const FVector Center = (Bottom + Top) * 0.5f - Up * Thickness * 0.5f;
		SpawnBlock(World, Center, FVector(Dir.Size() * 0.5f + 10.f, Width * 0.5f, Thickness * 0.5f), PlatformColor, Rotation);
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

	void SpawnJumpPad(UWorld* World, const FVector& FloorLocation, const FVector& TargetFloor)
	{
		const FVector From = FloorLocation + FVector(0.f, 0.f, CapsuleHalfHeight);
		const FVector To = TargetFloor + FVector(0.f, 0.f, CapsuleHalfHeight);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AArenaJumpPad* Pad = World->SpawnActor<AArenaJumpPad>(FloorLocation, FRotator::ZeroRotator, Params))
		{
			Pad->Init(SolveLaunch(From, To, 500.f));
		}
	}

	void BuildGeometry(UWorld* World)
	{
		// Floor and outer walls.
		SpawnBlock(World, FVector(0.f, 0.f, -50.f), FVector(3300.f, 3300.f, 50.f), FloorColor);
		for (const float S : { -1.f, 1.f })
		{
			SpawnBlock(World, FVector(S * 3250.f, 0.f, 1000.f), FVector(50.f, 3300.f, 1000.f), WallColor);
			SpawnBlock(World, FVector(0.f, S * 3250.f, 1000.f), FVector(3300.f, 50.f, 1000.f), WallColor);
		}

		// Central platform and its four ramps.
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
				// Corner towers, with a jump pad in front of each.
				SpawnBlock(World, FVector(X * 2300.f, Y * 2300.f, TowerTop * 0.5f), FVector(500.f, 500.f, TowerTop * 0.5f), TowerColor);
				SpawnJumpPad(World, FVector(X * 1300.f, Y * 1300.f, 0.f), FVector(X * 2300.f, Y * 2300.f, TowerTop));

				// Pillars and crates.
				SpawnBlock(World, FVector(X * 1400.f, Y * 600.f, 500.f), FVector(100.f, 100.f, 500.f), PillarColor);
				SpawnBlock(World, FVector(X * 600.f, Y * 1400.f, 500.f), FVector(100.f, 100.f, 500.f), PillarColor);
				SpawnBlock(World, FVector(X * 1000.f, Y * 2400.f, 50.f), FVector(150.f, 150.f, 50.f), CrateColor);
			}
		}

		// Bridge ring connecting the towers.
		for (const float S : { -1.f, 1.f })
		{
			SpawnBlock(World, FVector(0.f, S * 2300.f, TowerTop - 25.f), FVector(1800.f, 250.f, 25.f), BridgeColor);
			SpawnBlock(World, FVector(S * 2300.f, 0.f, TowerTop - 25.f), FVector(250.f, 1800.f, 25.f), BridgeColor);
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

const TArray<FTransform>& ArenaMap::GetSpawnPoints()
{
	static const TArray<FTransform> Spawns = []
	{
		TArray<FTransform> Result;
		auto Add = [&Result](const FVector& Feet)
		{
			const FVector Location = Feet + FVector(0.f, 0.f, CapsuleHalfHeight + 10.f);
			Result.Add(FTransform(FRotator(0.f, (-Location).Rotation().Yaw, 0.f), Location));
		};
		for (const float S : { -1.f, 1.f })
		{
			Add(FVector(S * 2700.f, 0.f, 0.f));
			Add(FVector(0.f, S * 2700.f, 0.f));
			Add(FVector(S * 2300.f, S * 2300.f, TowerTop));
			Add(FVector(S * 2300.f, -S * 2300.f, TowerTop));
		}
		return Result;
	}();
	return Spawns;
}

const TArray<FArenaPickupSpot>& ArenaMap::GetPickupSpots()
{
	static const TArray<FArenaPickupSpot> Spots = []
	{
		constexpr float Hover = 50.f;
		TArray<FArenaPickupSpot> Result;
		Result.Add({ EArenaPickupType::MegaHealth, FVector(0.f, 0.f, PlatformTop + Hover) });
		Result.Add({ EArenaPickupType::Armor, FVector(0.f, 2300.f, TowerTop + Hover) });
		Result.Add({ EArenaPickupType::HeavyArmor, FVector(0.f, -2300.f, TowerTop + Hover) });
		Result.Add({ EArenaPickupType::Railgun, FVector(2300.f, 0.f, TowerTop + Hover) });
		Result.Add({ EArenaPickupType::Railgun, FVector(-2300.f, 0.f, TowerTop + Hover) });
		Result.Add({ EArenaPickupType::RocketLauncher, FVector(0.f, 2200.f, Hover) });
		Result.Add({ EArenaPickupType::RocketLauncher, FVector(0.f, -2200.f, Hover) });
		// Floor between the jump pads and the side walls.
		Result.Add({ EArenaPickupType::Shotgun, FVector(2000.f, -1000.f, Hover) });
		Result.Add({ EArenaPickupType::Shotgun, FVector(-2000.f, 1000.f, Hover) });
		Result.Add({ EArenaPickupType::GrenadeLauncher, FVector(2000.f, 1000.f, Hover) });
		Result.Add({ EArenaPickupType::GrenadeLauncher, FVector(-2000.f, -1000.f, Hover) });
		// Up on the bridge ring, either side of the armors.
		Result.Add({ EArenaPickupType::LightningGun, FVector(-1200.f, 2300.f, TowerTop + Hover) });
		Result.Add({ EArenaPickupType::LightningGun, FVector(1200.f, -2300.f, TowerTop + Hover) });
		Result.Add({ EArenaPickupType::PlasmaGun, FVector(1200.f, 2300.f, TowerTop + Hover) });
		Result.Add({ EArenaPickupType::PlasmaGun, FVector(-1200.f, -2300.f, TowerTop + Hover) });
		for (const float X : { -1.f, 1.f })
		{
			for (const float Y : { -1.f, 1.f })
			{
				Result.Add({ EArenaPickupType::Health, FVector(X * 1000.f, Y * 1000.f, Hover) });
				Result.Add({ EArenaPickupType::Ammo, FVector(X * 1000.f, Y * 2400.f, 100.f + Hover) });
			}
		}
		return Result;
	}();
	return Spots;
}

void ArenaMap::BuildLocal(UWorld* World)
{
	if (!World)
	{
		return;
	}
	BuildGeometry(World);
	UE_LOG(LogArena, Log, TEXT("Built arena (%s)"), World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"));
	if (World->GetNetMode() != NM_DedicatedServer)
	{
		BuildLighting(World);
	}
}

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

void AArenaJumpPad::Init(const FVector& InLaunchVelocity)
{
	LaunchVelocity = InLaunchVelocity;
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
