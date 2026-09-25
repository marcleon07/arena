#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ArenaSettings.generated.h"

/**
 * Per-user preferences, edited from the menu or console and saved to
 * Saved/Config/<Platform>/GameUserSettings.ini. Read via GetDefault<UArenaSettings>().
 */
UCLASS(Config = GameUserSettings)
class ARENA_API UArenaSettings : public UObject
{
	GENERATED_BODY()

public:
	static UArenaSettings* Get() { return GetMutableDefault<UArenaSettings>(); }
	void Save() { SaveConfig(); }

	/** Empty keeps the server-assigned "PlayerN". */
	UPROPERTY(Config)
	FString PlayerName;

	/** Quake-style: degrees per mouse count = 0.022 * Sensitivity. */
	UPROPERTY(Config)
	float Sensitivity = 2.5f;

	UPROPERTY(Config)
	float FieldOfView = 100.f;

	UPROPERTY(Config)
	float MasterVolume = 0.8f;

	/** Holding jump re-jumps on landing. Off for scroll-wheel HL1 purism. */
	UPROPERTY(Config)
	bool bAutoHop = true;

	UPROPERTY(Config)
	bool bFullscreen = false;

	UPROPERTY(Config)
	FString LastJoinAddress = TEXT("127.0.0.1");

	UPROPERTY(Config)
	int32 HostFragLimit = 20;

	/** Minutes; 0 means no time limit. */
	UPROPERTY(Config)
	int32 HostTimeLimit = 10;
};
