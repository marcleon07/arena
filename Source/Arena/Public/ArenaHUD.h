#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ArenaHUD.generated.h"

class AArenaCharacter;
class AArenaGameState;
class UFont;

/** Canvas-drawn HUD: crosshair, speedometer, health/armor/ammo, kill feed, scoreboard. */
UCLASS()
class ARENA_API AArenaHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawCrosshair();
	void DrawSpeedometer(const AArenaCharacter* Pawn);
	void DrawStatus(const AArenaCharacter* Pawn);
	void DrawWeapons(const AArenaCharacter* Pawn);
	void DrawMatchInfo(const AArenaGameState* GS);
	void DrawKillFeed(const AArenaGameState* GS);
	void DrawScoreboard(const AArenaGameState* GS);
	void DrawCentered(const FString& Text, float Y, const FLinearColor& Color, UFont* Font, float TextScale);

	float UIScale = 1.f;
};
