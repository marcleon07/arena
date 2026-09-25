#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ArenaTypes.h"
#include "ArenaAudio.generated.h"

class USoundBase;
class USoundAttenuation;

/**
 * Loads the game's sound effects and plays them. Everything is local-only:
 * callers decide which machines should hear a sound. No-op on dedicated servers.
 */
UCLASS()
class ARENA_API UArenaAudio : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Non-spatial, e.g. hit confirmation or your own pain. */
	static void Play2D(const UObject* WorldContext, EArenaSound Sound, float Volume = 1.f, float Pitch = 1.f);

	/** Spatialized with the arena's attenuation. */
	static void PlayAt(const UObject* WorldContext, EArenaSound Sound, const FVector& Location, float Volume = 1.f, float Pitch = 1.f);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	static UArenaAudio* Get(const UObject* WorldContext);
	USoundBase* Find(EArenaSound Sound) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> Sounds;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> Attenuation;
};
