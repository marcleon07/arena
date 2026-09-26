#include "ArenaGameState.h"
#include "ArenaAudio.h"
#include "ArenaCharacter.h"
#include "ArenaMap.h"
#include "ArenaMovementComponent.h"
#include "ArenaPlayerController.h"
#include "ArenaPlayerState.h"
#include "ArenaVisuals.h"
#include "Net/UnrealNetwork.h"

void AArenaGameState::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		AirAccelerate = GetDefault<UArenaMovementComponent>()->AirAccelerate;
	}

	// The level is generated from code on every machine (server and clients), so
	// the geometry needs no replication and is identical everywhere.
	ArenaMap::BuildLocal(GetWorld(), MapId);
}

void AArenaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaGameState, AirAccelerate);
	DOREPLIFETIME(AArenaGameState, FragLimit);
	DOREPLIFETIME(AArenaGameState, MatchEndTime);
	DOREPLIFETIME(AArenaGameState, WinnerName);
	DOREPLIFETIME(AArenaGameState, MapId);
	DOREPLIFETIME(AArenaGameState, VoteOptions);
	DOREPLIFETIME(AArenaGameState, VoteCounts);
	DOREPLIFETIME(AArenaGameState, VoteEndTime);
}

bool AArenaGameState::IsMenuWorld(const UWorld* World)
{
	return World && World->GetNetMode() == NM_Standalone;
}

void AArenaGameState::HandleMatchHasEnded()
{
	Super::HandleMatchHasEnded();

	// Runs on the server and every client: show the vote to local players.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AArenaPlayerController* PC = Cast<AArenaPlayerController>(It->Get());
		if (PC && PC->IsLocalController())
		{
			PC->ShowMapVote();
		}
	}
}

float AArenaGameState::GetVoteTimeRemaining() const
{
	return FMath::Max(0.f, VoteEndTime - GetServerWorldTimeSeconds());
}

void AArenaGameState::RecountVotes()
{
	VoteCounts.Init(0, VoteOptions.Num());
	for (APlayerState* PS : PlayerArray)
	{
		if (const AArenaPlayerState* APS = Cast<AArenaPlayerState>(PS))
		{
			const int32 Index = VoteOptions.IndexOfByKey(APS->VotedMap);
			if (VoteCounts.IsValidIndex(Index))
			{
				++VoteCounts[Index];
			}
		}
	}
}

float AArenaGameState::GetTimeRemaining() const
{
	return FMath::Max(0.f, MatchEndTime - GetServerWorldTimeSeconds());
}

TArray<AArenaPlayerState*> AArenaGameState::GetSortedPlayers() const
{
	TArray<AArenaPlayerState*> Result;
	for (APlayerState* PS : PlayerArray)
	{
		if (AArenaPlayerState* APS = Cast<AArenaPlayerState>(PS))
		{
			Result.Add(APS);
		}
	}
	Result.Sort([](const AArenaPlayerState& A, const AArenaPlayerState& B)
	{
		return A.Frags != B.Frags ? A.Frags > B.Frags : A.Deaths < B.Deaths;
	});
	return Result;
}

void AArenaGameState::SpawnShotVisual(UWorld* World, const AArenaCharacter* Shooter, EArenaWeapon Weapon, const TArray<FVector_NetQuantize>& Ends)
{
	const FArenaWeaponInfo& Info = GetWeaponInfo(Weapon);
	if (!Shooter || Info.TracerWidth <= 0.f)
	{
		return;
	}
	const FVector Start = Shooter->GetMuzzleLocation();
	// Impact puff scales with the tracer: big for the rail, tiny for pellets.
	const float BlastRadius = FMath::Clamp(Info.TracerWidth * 5.f, 6.f, 25.f);
	for (const FVector_NetQuantize& End : Ends)
	{
		ArenaVisuals::SpawnBeam(World, Start, End, Info.Color, Info.TracerWidth, Info.TracerLife);
		ArenaVisuals::SpawnBlast(World, End, Info.Color, BlastRadius, FMath::Min(Info.TracerLife * 2.f, 0.3f));
	}
}

void AArenaGameState::MulticastShot_Implementation(AArenaCharacter* Shooter, EArenaWeapon Weapon, const TArray<FVector_NetQuantize>& Ends)
{
	if (!Shooter)
	{
		return;
	}
	// The shooter already played the sound locally, and a remote shooter
	// already drew its own predicted tracer.
	if (!Shooter->IsLocalPlayerView())
	{
		UArenaAudio::PlayAt(this, GetWeaponInfo(Weapon).FireSound, Shooter->GetMuzzleLocation(), 1.f, FMath::FRandRange(0.97f, 1.03f));
	}
	if (!(Shooter->IsLocallyControlled() && !Shooter->HasAuthority()))
	{
		SpawnShotVisual(GetWorld(), Shooter, Weapon, Ends);
	}
}

void AArenaGameState::MulticastExplosion_Implementation(FVector_NetQuantize Location, FLinearColor Color, float Radius, EArenaSound Sound)
{
	ArenaVisuals::SpawnBlast(GetWorld(), Location, Color, Radius, 0.35f);
	UArenaAudio::PlayAt(this, Sound, Location);
}

void AArenaGameState::MulticastKill_Implementation(const FString& Killer, const FString& Victim, EArenaWeapon Weapon)
{
	KillFeed.Add({ Killer, Victim, Weapon, static_cast<float>(GetWorld()->GetTimeSeconds()) });
	if (KillFeed.Num() > 6)
	{
		KillFeed.RemoveAt(0);
	}
}
