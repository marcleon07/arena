#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ArenaHUD.generated.h"

class AArenaCharacter;
class AArenaGameState;
class UFont;

/** Canvas-drawn HUD: crosshair, speedometer, health/armor/ammo, kill feed, scoreboard, combat feedback. */
UCLASS()
class ARENA_API AArenaHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** You damaged someone: hit marker + floating damage number. */
	void OnHitConfirmed(const FVector& VictimLocation, int32 Damage, bool bKilled);

	/** You took damage: red flash + direction indicator toward the source. */
	void OnDamaged(const FVector& SourceLocation, int32 Damage);

	/** Big centered message ("You fragged X"), split on '\n'. */
	void ShowCenterMessage(const FString& Text, const FLinearColor& Color);

private:
	struct FHitNumber
	{
		FVector Location;
		int32 Damage;
		float Time;
	};

	struct FDamageMarker
	{
		FVector Source;
		float Strength;
		float Time;
	};

	void DrawCrosshair();
	void DrawHitMarker();
	void DrawHitNumbers();
	void DrawDamageFeedback();
	void DrawCenterMessage();
	void DrawSpeedometer(const AArenaCharacter* Pawn);
	void DrawStatus(const AArenaCharacter* Pawn);
	void DrawWeapons(const AArenaCharacter* Pawn);
	void DrawMatchInfo(const AArenaGameState* GS);
	void DrawKillFeed(const AArenaGameState* GS);
	void DrawScoreboard(const AArenaGameState* GS);
	void DrawCentered(const FString& Text, float Y, const FLinearColor& Color, UFont* Font, float TextScale);
	float Now() const;

	float UIScale = 1.f;
	float SmoothedFPS = 0.f;

	TArray<FHitNumber> HitNumbers;
	float LastHitTime = -100.f;
	bool bLastHitKilled = false;

	TArray<FDamageMarker> DamageMarkers;
	float LastDamageTime = -100.f;
	float LastDamageAmount = 0.f;

	FString CenterMessage;
	FLinearColor CenterMessageColor = FLinearColor::White;
	float CenterMessageTime = -100.f;
};
