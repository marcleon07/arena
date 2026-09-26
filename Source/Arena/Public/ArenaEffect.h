#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaEffect.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

/** Local-only cosmetic actor (beam or blast) that glows, fades out and dies. */
UCLASS(NotPlaceable, Transient)
class ARENA_API AArenaEffect : public AActor
{
	GENERATED_BODY()

public:
	AArenaEffect();

	void InitBeam(const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Life);
	void InitBlast(const FVector& Location, const FLinearColor& Color, float Radius, float Life);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Only explosions light up their surroundings. */
	UPROPERTY()
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;

	FVector StartScale = FVector::OneVector;
	FVector EndScale = FVector::ZeroVector;
	float Duration = 0.5f;
	float Age = 0.f;
	float LightIntensity = 0.f;
};
