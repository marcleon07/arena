#include "ArenaGameState.h"
#include "ArenaAudio.h"
#include "ArenaCharacter.h"
#include "ArenaMap.h"
#include "ArenaMovementComponent.h"
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
	ArenaMap::BuildLocal(GetWorld());
}

void AArenaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaGameState, AirAccelerate);
	DOREPLIFETIME(AArenaGameState, FragLimit);
	DOREPLIFETIME(AArenaGameState, MatchEndTime);
	DOREPLIFETIME(AArenaGameState, WinnerName);
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

void AArenaGameState::SpawnShotVisual(UWorld* World, const AArenaCharacter* Shooter, EArenaWeapon Weapon, const FVector& End)
{
	if (!Shooter)
	{
		return;
	}
	const FVector Start = Shooter->GetMuzzleLocation();
	const FLinearColor Color = GetWeaponInfo(Weapon).Color;
	if (Weapon == EArenaWeapon::Railgun)
	{
		ArenaVisuals::SpawnBeam(World, Start, End, Color, 5.f, 0.9f);
		ArenaVisuals::SpawnBlast(World, End, Color, 25.f, 0.3f);
	}
	else
	{
		ArenaVisuals::SpawnBeam(World, Start, End, Color, 1.2f, 0.07f);
		ArenaVisuals::SpawnBlast(World, End, Color, 8.f, 0.15f);
	}
}

void AArenaGameState::MulticastShot_Implementation(AArenaCharacter* Shooter, EArenaWeapon Weapon, FVector_NetQuantize End)
{
	if (!Shooter)
	{
		return;
	}
	// The shooter already played the sound locally, and a remote shooter
	// already drew its own predicted tracer.
	if (!Shooter->IsLocallyControlled())
	{
		UArenaAudio::PlayAt(this, GetFireSound(Weapon), Shooter->GetMuzzleLocation(), 1.f, FMath::FRandRange(0.97f, 1.03f));
	}
	if (!(Shooter->IsLocallyControlled() && !Shooter->HasAuthority()))
	{
		SpawnShotVisual(GetWorld(), Shooter, Weapon, End);
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
