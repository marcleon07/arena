#include "ArenaGameMode.h"
#include "Arena.h"
#include "ArenaCharacter.h"
#include "ArenaGameState.h"
#include "ArenaHUD.h"
#include "ArenaMap.h"
#include "ArenaPickup.h"
#include "ArenaPlayerController.h"
#include "ArenaPlayerState.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AArenaGameMode::AArenaGameMode()
{
	DefaultPawnClass = AArenaCharacter::StaticClass();
	PlayerControllerClass = AArenaPlayerController::StaticClass();
	PlayerStateClass = AArenaPlayerState::StaticClass();
	GameStateClass = AArenaGameState::StaticClass();
	HUDClass = AArenaHUD::StaticClass();

	bDelayedStart = false;
	bUseSeamlessTravel = false;
	PrimaryActorTick.bCanEverTick = true;
}

void AArenaGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	FragLimit = UGameplayStatics::GetIntOption(Options, TEXT("FragLimit"), FragLimit);
	TimeLimitMinutes = UGameplayStatics::GetIntOption(Options, TEXT("TimeLimit"), FMath::RoundToInt(TimeLimitMinutes));
}

void AArenaGameMode::StartPlay()
{
	Super::StartPlay();
	SpawnPickups();
}

void AArenaGameMode::SpawnPickups()
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FArenaPickupSpot& Spot : ArenaMap::GetPickupSpots())
	{
		if (AArenaPickup* Pickup = GetWorld()->SpawnActor<AArenaPickup>(Spot.Location, FRotator::ZeroRotator, Params))
		{
			Pickup->InitPickup(Spot.Type);
		}
	}
}

void AArenaGameMode::HandleMatchHasStarted()
{
	if (AArenaGameState* GS = GetGameState<AArenaGameState>())
	{
		GS->FragLimit = FragLimit;
		GS->WinnerName.Reset();
		const bool bTimed = TimeLimitMinutes > 0.f && !AArenaGameState::IsMenuWorld(GetWorld());
		GS->MatchEndTime = bTimed ? GetWorld()->GetTimeSeconds() + TimeLimitMinutes * 60.f : 0.f;
	}
	Super::HandleMatchHasStarted();
}

void AArenaGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (AArenaPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AArenaPlayerState>() : nullptr)
	{
		// Default names are the machine name, which collides when testing locally.
		// Players can rename with the "SetName" console command.
		PS->ColorIndex = NextColorIndex++;
		ChangeName(NewPlayer, FString::Printf(TEXT("Player%d"), NextColorIndex), false);
	}
	Super::PostLogin(NewPlayer);
}

void AArenaGameMode::EnsureSpawnPoints()
{
	if (SpawnPoints.Num() > 0)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FTransform& Spawn : ArenaMap::GetSpawnPoints())
	{
		if (AActor* Point = GetWorld()->SpawnActor<ATargetPoint>(Spawn.GetLocation(), Spawn.Rotator(), Params))
		{
			SpawnPoints.Add(Point);
		}
	}
}

AActor* AArenaGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	EnsureSpawnPoints();
	if (SpawnPoints.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	// Quake 3 SelectRandomFurthestSpawnPoint: rank by distance to the nearest
	// living opponent, pick randomly among the better half.
	TArray<TPair<float, AActor*>> Ranked;
	for (AActor* Point : SpawnPoints)
	{
		float Nearest = UE_BIG_NUMBER;
		for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsDead() && It->GetController() != Player)
			{
				Nearest = FMath::Min(Nearest, FVector::Dist(It->GetActorLocation(), Point->GetActorLocation()));
			}
		}
		Ranked.Emplace(Nearest, Point);
	}
	Ranked.Sort([](const TPair<float, AActor*>& A, const TPair<float, AActor*>& B) { return A.Key > B.Key; });
	const int32 Candidates = FMath::Max(1, Ranked.Num() / 2);
	return Ranked[FMath::RandRange(0, Candidates - 1)].Value;
}

void AArenaGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsMatchInProgress())
	{
		return;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AArenaPlayerController* PC = Cast<AArenaPlayerController>(It->Get()))
		{
			TryRespawn(PC, false);
		}
	}

	const AArenaGameState* GS = GetGameState<AArenaGameState>();
	if (GS && GS->MatchEndTime > 0.f && GetWorld()->GetTimeSeconds() >= GS->MatchEndTime)
	{
		const TArray<AArenaPlayerState*> Sorted = GS->GetSortedPlayers();
		FinishMatch(Sorted.Num() > 0 ? Sorted[0] : nullptr);
	}
}

bool AArenaGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	// The main menu world has no players in it, just the orbiting camera.
	return !AArenaGameState::IsMenuWorld(GetWorld()) && Super::PlayerCanRestart_Implementation(Player);
}

void AArenaGameMode::TryRespawn(AArenaPlayerController* PC, bool bRequested)
{
	if (!PC || !IsMatchInProgress() || AArenaGameState::IsMenuWorld(GetWorld()) || (PC->GetPawn() && !PC->GetPawn()->IsPendingKillPending()))
	{
		return;
	}
	if (PC->PlayerState && PC->PlayerState->IsOnlyASpectator())
	{
		return;
	}

	// DeathTime < 0 means never spawned: spawn right away. A failed spawn simply retries next tick.
	const float Required = PC->DeathTime < 0.f ? 0.f : (bRequested ? ClickRespawnDelay : AutoRespawnDelay);
	const float SinceDeath = PC->DeathTime < 0.f ? 0.f : GetWorld()->GetTimeSeconds() - PC->DeathTime;
	if (SinceDeath >= Required)
	{
		RestartPlayer(PC);
	}
}

void AArenaGameMode::OnPlayerKilled(AController* Killer, AController* Victim, EArenaWeapon Weapon)
{
	AArenaPlayerState* VictimPS = Victim ? Victim->GetPlayerState<AArenaPlayerState>() : nullptr;
	AArenaPlayerState* KillerPS = Killer ? Killer->GetPlayerState<AArenaPlayerState>() : nullptr;

	if (VictimPS)
	{
		++VictimPS->Deaths;
	}
	const bool bSuicide = !KillerPS || KillerPS == VictimPS;
	if (bSuicide)
	{
		if (VictimPS)
		{
			--VictimPS->Frags;
		}
	}
	else
	{
		++KillerPS->Frags;
	}

	if (AArenaGameState* GS = GetGameState<AArenaGameState>())
	{
		GS->MulticastKill(bSuicide ? FString() : KillerPS->GetPlayerName(), VictimPS ? VictimPS->GetPlayerName() : FString(TEXT("?")), Weapon);
	}

	SendFragMessages(Killer, Victim, KillerPS, VictimPS);

	if (!bSuicide && FragLimit > 0 && KillerPS->Frags >= FragLimit)
	{
		FinishMatch(KillerPS);
	}
}

void AArenaGameMode::SendFragMessages(AController* Killer, AController* Victim, AArenaPlayerState* KillerPS, AArenaPlayerState* VictimPS)
{
	const bool bSuicide = !KillerPS || KillerPS == VictimPS;
	if (AArenaPlayerController* VictimPC = Cast<AArenaPlayerController>(Victim))
	{
		VictimPC->ClientFragMessage(bSuicide ? FString(TEXT("You killed yourself")) : FString::Printf(TEXT("Fragged by %s"), *KillerPS->GetPlayerName()), false);
	}
	AArenaPlayerController* KillerPC = Cast<AArenaPlayerController>(Killer);
	const AArenaGameState* GS = GetGameState<AArenaGameState>();
	if (bSuicide || !KillerPC || !GS)
	{
		return;
	}

	// Quake 3: "You fragged X" and "2nd place with 7".
	const TArray<AArenaPlayerState*> Sorted = GS->GetSortedPlayers();
	const int32 Place = Sorted.IndexOfByKey(KillerPS) + 1;
	const bool bTied = Sorted.ContainsByPredicate([KillerPS](const AArenaPlayerState* Other) { return Other != KillerPS && Other->Frags == KillerPS->Frags; });
	const TCHAR* Suffix = (Place % 100 >= 11 && Place % 100 <= 13) ? TEXT("th") : Place % 10 == 1 ? TEXT("st") : Place % 10 == 2 ? TEXT("nd") : Place % 10 == 3 ? TEXT("rd") : TEXT("th");
	KillerPC->ClientFragMessage(FString::Printf(TEXT("You fragged %s\n%s%d%s place with %d"),
		*VictimPS->GetPlayerName(), bTied ? TEXT("Tied for ") : TEXT(""), Place, Suffix, KillerPS->Frags), true);
}

void AArenaGameMode::FinishMatch(AArenaPlayerState* Winner)
{
	if (!IsMatchInProgress())
	{
		return;
	}
	if (AArenaGameState* GS = GetGameState<AArenaGameState>())
	{
		GS->WinnerName = Winner ? Winner->GetPlayerName() : FString(TEXT("Nobody"));
	}
	UE_LOG(LogArena, Log, TEXT("Match over, winner: %s"), Winner ? *Winner->GetPlayerName() : TEXT("none"));
	EndMatch();
}

void AArenaGameMode::HandleMatchHasEnded()
{
	Super::HandleMatchHasEnded();
	// Show the scoreboard for a while, then reload the map for a new match.
	GetWorldTimerManager().SetTimer(RestartTimer, this, &AArenaGameMode::RestartGame, 10.f);
}
