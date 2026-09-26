#include "ArenaGameMode.h"
#include "ArenaShowcase.h"
#include "Arena.h"
#include "ArenaBotController.h"
#include "ArenaCharacter.h"
#include "ArenaGameState.h"
#include "ArenaHUD.h"
#include "ArenaMap.h"
#include "ArenaOnline.h"
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
	BotCount = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Bots"), BotCount), 0, 15);
	BotSkill = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("BotSkill"), BotSkill), 1, 5);
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
	AArenaShowcase::StartFromCommandLine(GetWorld());

	// After a map vote the session carries over; show the new map in the browser.
	if (UArenaOnline* Online = UArenaOnline::Get(this))
	{
		Online->UpdateListing(MapId, BotCount);
	}
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

	if (!AArenaGameState::IsMenuWorld(GetWorld()))
	{
		for (int32 i = 0; i < BotCount; ++i)
		{
			AddBot(BotSkill);
		}
	}
}

void AArenaGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (AArenaPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AArenaPlayerState>() : nullptr)
	{
		// Keep real names (Steam persona). Without an online account the name defaults to
		// the machine name, which collides when testing locally, so number those.
		PS->ColorIndex = NextColorIndex++;
		const FString Name = PS->GetPlayerName();
		if (Name.IsEmpty() || Name == TEXT("Player") || Name.StartsWith(FPlatformProcess::ComputerName()))
		{
			ChangeName(NewPlayer, FString::Printf(TEXT("Player%d"), NextColorIndex), false);
		}
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
	for (AArenaBotController* Bot : Bots)
	{
		if (Bot && !Bot->GetPawn() && GetWorld()->GetTimeSeconds() - Bot->DeathTime >= AutoRespawnDelay)
		{
			RestartPlayer(Bot);
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

bool AArenaGameMode::ReadyToStartMatch_Implementation()
{
	// AGameMode waits for a human; a match with bots can start without one.
	if (GetMatchState() == MatchState::WaitingToStart && BotCount > 0 && !AArenaGameState::IsMenuWorld(GetWorld()))
	{
		return true;
	}
	return Super::ReadyToStartMatch_Implementation();
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

	UE_LOG(LogArena, Log, TEXT("Kill: %s -> %s (%s)"), KillerPS ? *KillerPS->GetPlayerName() : TEXT("world"),
		VictimPS ? *VictimPS->GetPlayerName() : TEXT("?"), Weapon == EArenaWeapon::Count ? TEXT("fell") : GetWeaponInfo(Weapon).Name);

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
	const FString URL = FString::Printf(TEXT("/Engine/Maps/Entry?Arena=%s?FragLimit=%d?TimeLimit=%d?Bots=%d?BotSkill=%d%s"),
		*NextMap.ToString(), FragLimit, FMath::RoundToInt(TimeLimitMinutes), Bots.Num(), BotSkill,
		GetNetMode() == NM_ListenServer ? TEXT("?listen") : TEXT(""));
	GetWorld()->ServerTravel(URL, true);
}

// ---------------------------------------------------------------------------
// Bots
// ---------------------------------------------------------------------------

bool AArenaGameMode::AddBot(int32 Skill)
{
	const int32 Humans = GetNumPlayers();
	if (Humans + Bots.Num() >= 16)
	{
		return false;
	}

	static const TCHAR* Names[] =
	{
		TEXT("Ajax"), TEXT("Blitz"), TEXT("Cinder"), TEXT("Dagger"), TEXT("Echo"), TEXT("Flux"), TEXT("Grit"), TEXT("Havoc"),
		TEXT("Ion"), TEXT("Jolt"), TEXT("Kite"), TEXT("Lynx"), TEXT("Mako"), TEXT("Nova"), TEXT("Onyx"), TEXT("Pike"),
	};
	FString Name = FString::Printf(TEXT("Bot%d"), Bots.Num() + 1);
	for (const TCHAR* Candidate : Names)
	{
		if (!Bots.ContainsByPredicate([Candidate](const AArenaBotController* Bot)
			{
				return Bot && Bot->PlayerState && Bot->PlayerState->GetPlayerName() == Candidate;
			}))
		{
			Name = Candidate;
			break;
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AArenaBotController* Bot = GetWorld()->SpawnActor<AArenaBotController>(Params);
	if (!Bot)
	{
		return false;
	}
	Bot->InitBot(Skill > 0 ? Skill : BotSkill);
	if (AArenaPlayerState* PS = Bot->GetPlayerState<AArenaPlayerState>())
	{
		PS->ColorIndex = NextColorIndex++;
	}
	ChangeName(Bot, Name, false);
	Bots.Add(Bot);
	++NumBots;

	if (IsMatchInProgress())
	{
		RestartPlayer(Bot);
	}
	UE_LOG(LogArena, Log, TEXT("Added bot %s (skill %d)"), *Name, Bot->GetSkill());
	return true;
}

void AArenaGameMode::RemoveBot()
{
	if (Bots.Num() == 0)
	{
		return;
	}
	AArenaBotController* Bot = Bots.Pop();
	--NumBots;
	if (Bot)
	{
		if (APawn* Pawn = Bot->GetPawn())
		{
			Pawn->Destroy();
		}
		Bot->Destroy();
	}
}

const FArenaBotNav* AArenaGameMode::GetBotNav()
{
	if (!BotNav.IsBuilt())
	{
		// Items and spawns become nodes too, so bots can path right onto them.
		TArray<FVector> Extra;
		const FArenaMapDef& Map = ArenaMap::Get(MapId);
		for (const FTransform& Spawn : Map.Spawns)
		{
			Extra.Add(Spawn.GetLocation());
		}
		for (const FArenaPickupSpot& Spot : Map.Pickups)
		{
			Extra.Add(Spot.Location);
		}
		BotNav.Build(GetWorld(), Extra);
	}
	return &BotNav;
}
