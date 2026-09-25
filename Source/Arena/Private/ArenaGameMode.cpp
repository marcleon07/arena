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
	MapId = ArenaMap::Get(FName(*UGameplayStatics::ParseOption(Options, TEXT("Arena")))).Id;
	UE_LOG(LogArena, Log, TEXT("Arena map: %s"), *MapId.ToString());

	if (AArenaGameState* GS = GetGameState<AArenaGameState>())
	{
		GS->MapId = MapId;
	}
}

void AArenaGameMode::InitGameState()
{
	Super::InitGameState();
	if (AArenaGameState* GS = GetGameState<AArenaGameState>())
	{
		GS->MapId = MapId;
	}
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
	for (const FArenaPickupSpot& Spot : ArenaMap::Get(MapId).Pickups)
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
	for (const FTransform& Spawn : ArenaMap::Get(MapId).Spawns)
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

	SendFragMessages(Killer, Victim, KillerPS, VictimPS, Weapon);

	if (!bSuicide && FragLimit > 0 && KillerPS->Frags >= FragLimit)
	{
		FinishMatch(KillerPS);
	}
}

void AArenaGameMode::SendFragMessages(AController* Killer, AController* Victim, AArenaPlayerState* KillerPS, AArenaPlayerState* VictimPS, EArenaWeapon Weapon)
{
	const bool bSuicide = !KillerPS || KillerPS == VictimPS;
	if (AArenaPlayerController* VictimPC = Cast<AArenaPlayerController>(Victim))
	{
		const FString Text = !bSuicide ? FString::Printf(TEXT("Fragged by %s"), *KillerPS->GetPlayerName())
			: Weapon == EArenaWeapon::Count ? FString(TEXT("You fell into the void"))
			: FString(TEXT("You killed yourself"));
		VictimPC->ClientFragMessage(Text, false);
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
	// Set up the vote before the state change reaches clients, so it arrives with it.
	StartMapVote();
	Super::HandleMatchHasEnded();
}

void AArenaGameMode::StartMapVote()
{
	AArenaGameState* GS = GetGameState<AArenaGameState>();
	if (!GS)
	{
		return;
	}

	// Offer up to three maps (all of them while there are only three).
	TArray<FName> Options;
	for (const FArenaMapDef& Map : ArenaMap::GetMaps())
	{
		Options.Add(Map.Id);
	}
	for (int32 i = Options.Num() - 1; i > 0; --i)
	{
		Options.Swap(i, FMath::RandRange(0, i));
	}
	Options.SetNum(FMath::Min(Options.Num(), 3));
	Options.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AArenaPlayerState* APS = Cast<AArenaPlayerState>(PS))
		{
			APS->VotedMap = NAME_None;
		}
	}
	GS->VoteOptions = Options;
	GS->VoteEndTime = GetWorld()->GetTimeSeconds() + VoteDuration;
	GS->RecountVotes();

	GetWorldTimerManager().SetTimer(RestartTimer, this, &AArenaGameMode::FinishMapVote, VoteDuration);
}

void AArenaGameMode::CastVote(APlayerController* Voter, FName Map)
{
	AArenaGameState* GS = GetGameState<AArenaGameState>();
	AArenaPlayerState* PS = Voter ? Voter->GetPlayerState<AArenaPlayerState>() : nullptr;
	if (GS && PS && GS->VoteOptions.Contains(Map))
	{
		PS->VotedMap = Map;
		GS->RecountVotes();
	}
}

void AArenaGameMode::FinishMapVote()
{
	const AArenaGameState* GS = GetGameState<AArenaGameState>();
	FName NextMap = MapId;
	if (GS && GS->VoteOptions.Num() > 0)
	{
		// Most votes wins; ties (including nobody voting) are broken at random.
		int32 Best = -1;
		TArray<FName> Leaders;
		for (int32 i = 0; i < GS->VoteOptions.Num(); ++i)
		{
			const int32 Votes = GS->VoteCounts.IsValidIndex(i) ? GS->VoteCounts[i] : 0;
			if (Votes > Best)
			{
				Best = Votes;
				Leaders.Reset();
			}
			if (Votes == Best)
			{
				Leaders.Add(GS->VoteOptions[i]);
			}
		}
		NextMap = Leaders[FMath::RandRange(0, Leaders.Num() - 1)];
	}

	UE_LOG(LogArena, Log, TEXT("Next map: %s"), *NextMap.ToString());
	const FString URL = FString::Printf(TEXT("/Engine/Maps/Entry?Arena=%s?FragLimit=%d?TimeLimit=%d%s"),
		*NextMap.ToString(), FragLimit, FMath::RoundToInt(TimeLimitMinutes), GetNetMode() == NM_ListenServer ? TEXT("?listen") : TEXT(""));
	GetWorld()->ServerTravel(URL, true);
}
