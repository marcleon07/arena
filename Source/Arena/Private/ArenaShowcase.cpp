#include "ArenaShowcase.h"
#include "Arena.h"
#include "ArenaCharacter.h"
#include "ArenaGameState.h"
#include "ArenaMap.h"
#include "ArenaPickup.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

AArenaShowcase::AArenaShowcase()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
}

void AArenaShowcase::StartFromCommandLine(UWorld* World)
{
#if !UE_BUILD_SHIPPING
	FString Scene;
	if (World && FParse::Value(FCommandLine::Get(), TEXT("ArenaShowcase="), Scene))
	{
		if (AArenaShowcase* Showcase = World->SpawnActor<AArenaShowcase>())
		{
			Showcase->Scene = Scene.ToLower();
			UE_LOG(LogArena, Log, TEXT("Showcase: %s"), *Showcase->Scene);
		}
	}
#endif
}

APlayerController* AArenaShowcase::GetPlayer() const
{
	return GetWorld()->GetFirstPlayerController();
}

void AArenaShowcase::Then(float Delay, TFunction<void()> Action)
{
	ScriptEnd += Delay;
	Steps.Add({ ScriptEnd, MoveTemp(Action) });
}

void AArenaShowcase::Shot(const FString& Name)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Showcase") / Name + TEXT(".png");
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogArena, Log, TEXT("Showcase: %s"), *Path);
}

void AArenaShowcase::Look(const FVector& From, const FVector& At)
{
	Camera->SetActorLocationAndRotation(From, (At - From).Rotation());
	if (APlayerController* PC = GetPlayer())
	{
		PC->SetViewTarget(Camera);
	}
}

AArenaCharacter* AArenaShowcase::SpawnPoser(const FVector& Feet, float Yaw, const FLinearColor& Color, int32 Weapon)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AArenaCharacter* Character = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), Feet + FVector(0.f, 0.f, QU(36.f) + 1.f), FRotator(0.f, Yaw, 0.f), Params);
	if (Character)
	{
		Character->SetBodyColor(Color);
		const EArenaWeapon Gun = static_cast<EArenaWeapon>(Weapon % ArenaWeaponCount);
		Character->GiveWeapon(Gun, 10);
		Character->EquipWeapon(Gun);
		Character->PoseOverride.bEnabled = true;
	}
	return Character;
}

void AArenaShowcase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* PC = GetPlayer();
	if (!bBuilt)
	{
		if (!PC || !PC->GetPawn())
		{
			return;
		}
		// Uncooked runs compile shaders on demand; wait so materials aren't placeholders.
		if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
		{
			WaitLog -= DeltaSeconds;
			if (WaitLog <= 0.f)
			{
				WaitLog = 5.f;
				UE_LOG(LogArena, Log, TEXT("Showcase: waiting for %d shader jobs"), GShaderCompilingManager->GetNumRemainingJobs());
			}
			return;
		}
		if (!Camera)
		{
			Camera = GetWorld()->SpawnActor<ACameraActor>();
			Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		}
		if (AHUD* HUD = PC->GetHUD())
		{
			HUD->bShowHUD = false;
		}
		bBuilt = true;
		Build();
	}

	if (const AArenaCharacter* Target = Follow.Get())
	{
		const FVector At = Target->GetMesh()->GetComponentLocation() + FVector(0.f, 0.f, 110.f);
		const FVector Behind = Target->GetActorRotation().RotateVector(FVector(-320.f, 140.f, 90.f));
		Look(At + Behind, At);
	}

	Clock += DeltaSeconds;
	while (Steps.Num() > 0 && Clock >= Steps[0].Time)
	{
		const TFunction<void()> Action = MoveTemp(Steps[0].Action);
		Steps.RemoveAt(0);
		Action();
	}
	if (Steps.Num() == 0 && Clock > ScriptEnd + 1.f)
	{
		FPlatformMisc::RequestExit(false);
	}
}

void AArenaShowcase::Build()
{
	const FLinearColor Colors[] =
	{
		FLinearColor(0.9f, 0.1f, 0.1f), FLinearColor(0.1f, 0.4f, 1.f), FLinearColor(0.1f, 0.8f, 0.2f), FLinearColor(1.f, 0.8f, 0.1f),
		FLinearColor(0.8f, 0.2f, 0.9f), FLinearColor(0.1f, 0.9f, 0.9f), FLinearColor(1.f, 0.5f, 0.1f), FLinearColor(0.9f, 0.9f, 0.9f),
	};
	APlayerController* PC = GetPlayer();
	AArenaCharacter* Player = Cast<AArenaCharacter>(PC->GetPawn());
	if (Player && Scene != TEXT("view"))
	{
		// The showcase camera isn't the player's view, so their own body would show.
		Player->SetActorHiddenInGame(true);
	}

	if (Scene == TEXT("poses"))
	{
		// A line-up on the Courtyard's central platform (top at 400).
		struct FPose { FVector Velocity; bool bCrouch; bool bAir; float Pitch; };
		const FPose Poses[] =
		{
			{ FVector::ZeroVector, false, false, 0.f },         // idle, rocket launcher
			{ FVector(800.f, 0.f, 0.f), false, false, 0.f },    // run
			{ FVector(0.f, 800.f, 0.f), false, false, 0.f },    // strafe right
			{ FVector(-600.f, 0.f, 0.f), false, false, 0.f },   // backpedal
			{ FVector::ZeroVector, true, false, -10.f },        // crouch
			{ FVector(600.f, 300.f, -300.f), false, true, 10.f }, // falling
			{ FVector::ZeroVector, false, false, 50.f },        // aim up
			{ FVector(300.f, 0.f, 0.f), false, false, -50.f },  // walk, aim down
		};
		const int32 Weapons[] = { 4, 1, 2, 6, 7, 5, 3, 0 };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Poses); ++i)
		{
			AArenaCharacter* Character = SpawnPoser(FVector(0.f, -455.f + i * 130.f, 400.f), 0.f, Colors[i], Weapons[i]);
			if (Character)
			{
				Character->PoseOverride.LocalVelocity = Poses[i].Velocity;
				Character->PoseOverride.bCrouched = Poses[i].bCrouch;
				Character->PoseOverride.bInAir = Poses[i].bAir;
				Character->PoseOverride.AimPitch = Poses[i].Pitch;
			}
		}
		Then(0.f, [this] { Look(FVector(640.f, 0.f, 520.f), FVector(0.f, 0.f, 480.f)); });
		Then(2.5f, [this] { Shot(TEXT("poses_front")); });
		Then(0.5f, [this] { Look(FVector(380.f, -760.f, 560.f), FVector(0.f, -100.f, 480.f)); });
		Then(0.5f, [this] { Shot(TEXT("poses_angle")); });
		for (int32 Pair = 0; Pair < 4; ++Pair)
		{
			const float Y = -455.f + (Pair * 2 + 0.5f) * 130.f;
			Then(0.4f, [this, Y] { Look(FVector(230.f, Y - 60.f, 560.f), FVector(0.f, Y, 490.f)); });
			Then(0.4f, [this, Pair] { Shot(FString::Printf(TEXT("poses_close%d"), Pair)); });
			Then(0.4f, [this, Y] { Look(FVector(-40.f, Y - 240.f, 540.f), FVector(0.f, Y, 490.f)); });
			Then(0.4f, [this, Pair] { Shot(FString::Printf(TEXT("poses_side%d"), Pair)); });
		}
	}
	else if (Scene == TEXT("view") && Player)
	{
		for (int32 i = 0; i < ArenaWeaponCount; ++i)
		{
			const EArenaWeapon Weapon = static_cast<EArenaWeapon>(i);
			Then(0.3f, [Player, Weapon] { Player->GiveWeapon(Weapon, 50); Player->EquipWeapon(Weapon); });
			Then(0.8f, [this, Weapon] { Shot(FString::Printf(TEXT("view_%d_%s"), (int32)Weapon, GetWeaponInfo(Weapon).ShortName)); });
			Then(0.2f, [this, Player, Weapon] { Player->PullTrigger(); Shot(FString::Printf(TEXT("view_%d_%s_fire"), (int32)Weapon, GetWeaponInfo(Weapon).ShortName)); });
		}
	}
	else if (Scene == TEXT("items"))
	{
		TSet<EArenaPickupType> Seen;
		for (TActorIterator<AArenaPickup> It(GetWorld()); It; ++It)
		{
			if (Seen.Contains(It->GetPickupType()))
			{
				continue;
			}
			Seen.Add(It->GetPickupType());
			const FVector Item = It->GetActorLocation();
			const int32 Index = Seen.Num();
			Then(0.4f, [this, Item] { Look(Item + FVector(150.f, 90.f, 30.f), Item - FVector(0.f, 0.f, 15.f)); });
			Then(0.4f, [this, Index] { Shot(FString::Printf(TEXT("item_%d"), Index)); });
		}
	}
	else if (Scene == TEXT("ragdoll"))
	{
		AArenaCharacter* Victim = SpawnPoser(FVector(0.f, -300.f, 400.f), 90.f, Colors[0], 4);
		if (Victim)
		{
			Victim->PoseOverride.LocalVelocity = FVector(800.f, 0.f, 0.f);
		}
		Then(0.f, [this] { Look(FVector(550.f, 100.f, 600.f), FVector(0.f, 0.f, 450.f)); });
		Then(1.5f, [this] { Shot(TEXT("ragdoll_0")); });
		Then(0.1f, [Victim]
		{
			if (Victim)
			{
				Victim->GetCharacterMovement()->Velocity = FVector(-200.f, 700.f, 600.f);
				Victim->Kill();
			}
		});
		Then(0.3f, [this] { Shot(TEXT("ragdoll_1")); });
		Then(0.5f, [this] { Shot(TEXT("ragdoll_2")); });
		Then(1.5f, [this] { Shot(TEXT("ragdoll_3")); });
	}
	else if (Scene == TEXT("map"))
	{
		const AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>();
		const FArenaMapDef& Map = ArenaMap::Get(GS ? GS->MapId : NAME_None);
		const float R = Map.MenuOrbitRadius;
		const float H = Map.MenuOrbitHeight;
		const FString Id = Map.Id.ToString();
		Then(0.5f, [this, R, H] { Look(FVector(R * 0.7f, R * 0.7f, H), FVector::ZeroVector); });
		Then(1.5f, [this, Id] { Shot(Id + TEXT("_overview")); });
		for (int32 i = 0; i < FMath::Min(3, Map.Spawns.Num()); ++i)
		{
			const FVector Eye = Map.Spawns[i * 2 % Map.Spawns.Num()].GetLocation() + FVector(0.f, 0.f, QU(28.f));
			const FVector Ahead = Eye + Map.Spawns[i * 2 % Map.Spawns.Num()].GetRotation().Vector() * 1000.f - FVector(0.f, 0.f, 150.f);
			Then(0.5f, [this, Eye, Ahead] { Look(Eye, Ahead); });
			Then(1.f, [this, Id, i] { Shot(FString::Printf(TEXT("%s_eye%d"), *Id, i)); });
		}
	}
	else if (Scene == TEXT("bots"))
	{
		// A chase camera on a different bot every few seconds of a real match.
		for (int32 i = 0; i < 12; ++i)
		{
			Then(i == 0 ? 2.f : 1.5f, [this, Player]
			{
				TArray<AArenaCharacter*> Candidates;
				for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
				{
					if (*It != Player && !It->IsDead())
					{
						Candidates.Add(*It);
					}
				}
				Follow = Candidates.Num() > 0 ? Candidates[FMath::RandRange(0, Candidates.Num() - 1)] : nullptr;
			});
			Then(1.5f, [this, i] { Shot(FString::Printf(TEXT("bots_%02d"), i)); });
		}
	}
	else
	{
		UE_LOG(LogArena, Warning, TEXT("Showcase: unknown scene '%s'"), *Scene);
	}
}
