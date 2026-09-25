#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ArenaPlayerState.generated.h"

UCLASS()
class ARENA_API AArenaPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;

	FLinearColor GetPlayerColor() const;

	UPROPERTY(Replicated)
	int32 Frags = 0;

	UPROPERTY(Replicated)
	int32 Deaths = 0;

	UPROPERTY(ReplicatedUsing = OnRep_ColorIndex)
	int32 ColorIndex = 0;

	/** Map this player voted for at the end of the match (None = no vote). */
	UPROPERTY(Replicated)
	FName VotedMap;

protected:
	UFUNCTION()
	void OnRep_ColorIndex();
};
