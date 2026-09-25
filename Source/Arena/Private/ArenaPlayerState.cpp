#include "ArenaPlayerState.h"
#include "ArenaCharacter.h"
#include "Net/UnrealNetwork.h"

void AArenaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaPlayerState, Frags);
	DOREPLIFETIME(AArenaPlayerState, Deaths);
	DOREPLIFETIME(AArenaPlayerState, ColorIndex);
	DOREPLIFETIME(AArenaPlayerState, VotedMap);
}

void AArenaPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (AArenaPlayerState* Other = Cast<AArenaPlayerState>(PlayerState))
	{
		Other->ColorIndex = ColorIndex;
	}
}

FLinearColor AArenaPlayerState::GetPlayerColor() const
{
	static const FLinearColor Palette[] =
	{
		FLinearColor(0.85f, 0.15f, 0.15f),
		FLinearColor(0.15f, 0.35f, 0.90f),
		FLinearColor(0.95f, 0.75f, 0.10f),
		FLinearColor(0.15f, 0.80f, 0.30f),
		FLinearColor(0.70f, 0.20f, 0.85f),
		FLinearColor(0.10f, 0.80f, 0.85f),
		FLinearColor(0.95f, 0.45f, 0.10f),
		FLinearColor(0.90f, 0.90f, 0.90f),
	};
	return Palette[FMath::Abs(ColorIndex) % UE_ARRAY_COUNT(Palette)];
}

void AArenaPlayerState::OnRep_ColorIndex()
{
	if (AArenaCharacter* Character = GetPawn<AArenaCharacter>())
	{
		Character->UpdateColors();
	}
}
