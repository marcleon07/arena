#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"
#include "ArenaSettings.generated.h"

/** Up to two keys for one rebindable action. */
USTRUCT()
struct FArenaKeyBinding
{
	GENERATED_BODY()

	UPROPERTY()
	FName Action;

	UPROPERTY()
	FKey Primary;

	UPROPERTY()
	FKey Secondary;

	const FKey& GetKey(int32 Slot) const { return Slot == 0 ? Primary : Secondary; }
	FKey& GetKey(int32 Slot) { return Slot == 0 ? Primary : Secondary; }
};

/** A rebindable action as listed in the Controls menu. */
struct FArenaBindableAction
{
	FName Id;
	FText Label;
	FKey DefaultPrimary;
	FKey DefaultSecondary;
};

/**
 * Per-user preferences, edited from the menu or console and saved to
 * Saved/Config/<Platform>/GameUserSettings.ini. Read via UArenaSettings::Get().
 */
UCLASS(Config = GameUserSettings)
class ARENA_API UArenaSettings : public UObject
{
	GENERATED_BODY()

public:
	static UArenaSettings* Get() { return GetMutableDefault<UArenaSettings>(); }
	void Save() { SaveConfig(); }

	/** Every rebindable action, in menu order, with its default keys. */
	static const TArray<FArenaBindableAction>& GetBindableActions();

	/** The user's binding for Action, or its defaults if never changed. */
	FArenaKeyBinding GetBinding(FName Action) const;

	/** Binds Key to Action's Slot (0 or 1), unbinding it from anything else. EKeys::Invalid clears. */
	void SetBindingKey(FName Action, int32 Slot, const FKey& Key);

	void ResetBindings() { KeyBindings.Reset(); }

	/** Empty keeps the server-assigned "PlayerN". */
	UPROPERTY(Config)
	FString PlayerName;

	/** Quake-style: degrees per mouse count = 0.022 * Sensitivity. */
	UPROPERTY(Config)
	float Sensitivity = 2.5f;

	UPROPERTY(Config)
	bool bInvertMouse = false;

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
	bool bVSync = false;

	/** 0 = unlimited. */
	UPROPERTY(Config)
	float FrameRateLimit = 0.f;

	UPROPERTY(Config)
	bool bShowFPS = false;

	UPROPERTY(Config)
	FString LastJoinAddress = TEXT("127.0.0.1");

	UPROPERTY(Config)
	FName HostMap = TEXT("Courtyard");

	UPROPERTY(Config)
	int32 HostFragLimit = 20;

	/** Minutes; 0 means no time limit. */
	UPROPERTY(Config)
	int32 HostTimeLimit = 10;

private:
	/** Empty until the user changes a key; then holds every action. */
	UPROPERTY(Config)
	TArray<FArenaKeyBinding> KeyBindings;
};
