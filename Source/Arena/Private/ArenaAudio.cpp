#include "ArenaAudio.h"
#include "Arena.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

bool UArenaAudio::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UArenaAudio::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (IsRunningDedicatedServer())
	{
		return;
	}

	const UEnum* SoundEnum = StaticEnum<EArenaSound>();
	Sounds.SetNum(static_cast<int32>(EArenaSound::Count));
	for (int32 i = 1; i < Sounds.Num(); ++i)
	{
		const FString Name = SoundEnum->GetNameStringByValue(i);
		const FString Path = FString::Printf(TEXT("/Game/Audio/%s.%s"), *Name, *Name);
		Sounds[i] = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn);
		if (!Sounds[i])
		{
			UE_LOG(LogArena, Warning, TEXT("Missing sound %s (run Scripts/ImportSounds.bat)"), *Path);
		}
	}

	// Arena sounds carry across the whole map, like Quake's.
	Attenuation = NewObject<USoundAttenuation>(this);
	FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector(400.f, 0.f, 0.f);
	Settings.FalloffDistance = 6000.f;
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
}

UArenaAudio* UArenaAudio::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	return World->GetSubsystem<UArenaAudio>();
}

USoundBase* UArenaAudio::Find(EArenaSound Sound) const
{
	const int32 Index = static_cast<int32>(Sound);
	return Sounds.IsValidIndex(Index) ? Sounds[Index].Get() : nullptr;
}

void UArenaAudio::Play2D(const UObject* WorldContext, EArenaSound Sound, float Volume, float Pitch)
{
	if (const UArenaAudio* Audio = Get(WorldContext))
	{
		if (USoundBase* Asset = Audio->Find(Sound))
		{
			UGameplayStatics::PlaySound2D(WorldContext, Asset, Volume, Pitch);
		}
	}
}

void UArenaAudio::PlayAt(const UObject* WorldContext, EArenaSound Sound, const FVector& Location, float Volume, float Pitch)
{
	if (const UArenaAudio* Audio = Get(WorldContext))
	{
		if (USoundBase* Asset = Audio->Find(Sound))
		{
			UGameplayStatics::PlaySoundAtLocation(WorldContext, Asset, Location, Volume, Pitch, 0.f, Audio->Attenuation);
		}
	}
}
