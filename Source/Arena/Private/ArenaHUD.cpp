#include "ArenaHUD.h"
#include "ArenaCharacter.h"
#include "ArenaGameState.h"
#include "ArenaMovementComponent.h"
#include "ArenaPlayerController.h"
#include "ArenaPlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/GameMode.h"

namespace
{
	const FLinearColor TextColor(0.95f, 0.95f, 0.95f);
	const FLinearColor DimColor(0.6f, 0.6f, 0.6f);
	const FLinearColor Panel(0.f, 0.f, 0.f, 0.55f);

	FLinearColor HealthColor(int32 Value)
	{
		if (Value > 100) return FLinearColor(0.4f, 0.7f, 1.f);
		if (Value > 50)  return TextColor;
		if (Value > 25)  return FLinearColor(1.f, 0.8f, 0.2f);
		return FLinearColor(1.f, 0.2f, 0.2f);
	}
}

void AArenaHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	UIScale = Canvas->ClipY / 1080.f;

	const AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>();
	const AArenaPlayerController* PC = Cast<AArenaPlayerController>(GetOwningPlayerController());
	const AArenaCharacter* Pawn = Cast<AArenaCharacter>(GetOwningPawn());
	const bool bAlive = Pawn && !Pawn->IsDead();
	const bool bMatchOver = GS && GS->GetMatchState() == MatchState::WaitingPostMatch;

	if (bAlive)
	{
		DrawCrosshair();
		DrawSpeedometer(Pawn);
		DrawStatus(Pawn);
		DrawWeapons(Pawn);
	}
	else if (!bMatchOver)
	{
		DrawCentered(TEXT("You are dead. Click to respawn."), Canvas->ClipY * 0.6f, TextColor, GEngine->GetMediumFont(), 1.2f);
	}

	if (GS)
	{
		DrawMatchInfo(GS);
		DrawKillFeed(GS);
		if (bMatchOver || (PC && PC->IsScoreboardHeld()))
		{
			DrawScoreboard(GS);
		}
	}
}

void AArenaHUD::DrawCentered(const FString& Text, float Y, const FLinearColor& Color, UFont* Font, float TextScale)
{
	float W = 0.f, H = 0.f;
	GetTextSize(Text, W, H, Font, TextScale * UIScale);
	DrawText(Text, Color, (Canvas->ClipX - W) * 0.5f, Y, Font, TextScale * UIScale);
}

void AArenaHUD::DrawCrosshair()
{
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Gap = 4.f * UIScale;
	const float Len = 9.f * UIScale;
	const FLinearColor Color(0.2f, 1.f, 0.3f);
	DrawLine(CX - Gap - Len, CY, CX - Gap, CY, Color, 2.f);
	DrawLine(CX + Gap, CY, CX + Gap + Len, CY, Color, 2.f);
	DrawLine(CX, CY - Gap - Len, CX, CY - Gap, Color, 2.f);
	DrawLine(CX, CY + Gap, CX, CY + Gap + Len, Color, 2.f);
}

void AArenaHUD::DrawSpeedometer(const AArenaCharacter* Pawn)
{
	const UArenaMovementComponent* Move = Pawn->GetArenaMovement();
	if (!Move)
	{
		return;
	}
	// 320 u/s is running speed; anything above that is bhop gain.
	const float Speed = Move->GetHorizontalSpeedUPS();
	const FLinearColor Color = Speed > 330.f ? FLinearColor::LerpUsingHSV(TextColor, FLinearColor(0.2f, 1.f, 0.4f), FMath::Clamp((Speed - 330.f) / 400.f, 0.f, 1.f)) : TextColor;
	DrawCentered(FString::Printf(TEXT("%d"), FMath::RoundToInt(Speed)), Canvas->ClipY * 0.5f + 40.f * UIScale, Color, GEngine->GetMediumFont(), 1.2f);
}

void AArenaHUD::DrawStatus(const AArenaCharacter* Pawn)
{
	UFont* Big = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetSmallFont();
	const float X = 40.f * UIScale;
	const float Y = Canvas->ClipY - 110.f * UIScale;

	DrawRect(Panel, X - 12.f * UIScale, Y - 10.f * UIScale, 330.f * UIScale, 90.f * UIScale);
	DrawText(TEXT("HEALTH"), DimColor, X, Y, Small, UIScale);
	DrawText(FString::FromInt(Pawn->GetHealth()), HealthColor(Pawn->GetHealth()), X, Y + 18.f * UIScale, Big, 2.f * UIScale);
	DrawText(TEXT("ARMOR"), DimColor, X + 170.f * UIScale, Y, Small, UIScale);
	DrawText(FString::FromInt(Pawn->GetArmor()), FLinearColor(1.f, 0.85f, 0.3f), X + 170.f * UIScale, Y + 18.f * UIScale, Big, 2.f * UIScale);
}

void AArenaHUD::DrawWeapons(const AArenaCharacter* Pawn)
{
	UFont* Font = GEngine->GetMediumFont();
	const float SlotW = 170.f * UIScale;
	const float SlotH = 36.f * UIScale;
	float X = Canvas->ClipX - (SlotW + 8.f * UIScale) * ArenaWeaponCount - 30.f * UIScale;
	const float Y = Canvas->ClipY - 70.f * UIScale;

	for (int32 i = 0; i < ArenaWeaponCount; ++i)
	{
		const EArenaWeapon Weapon = static_cast<EArenaWeapon>(i);
		const FArenaWeaponInfo& Info = GetWeaponInfo(Weapon);
		const bool bOwned = Pawn->HasWeapon(Weapon);
		const bool bCurrent = Pawn->GetCurrentWeapon() == Weapon;

		DrawRect(bCurrent ? FLinearColor(Info.Color.R, Info.Color.G, Info.Color.B, 0.45f) : Panel, X, Y, SlotW, SlotH);
		const FString Label = FString::Printf(TEXT("%d %s  %d"), i + 1, Info.Name, bOwned ? Pawn->GetAmmo(Weapon) : 0);
		DrawText(Label, bOwned ? TextColor : DimColor * 0.6f, X + 8.f * UIScale, Y + 6.f * UIScale, Font, 0.9f * UIScale);
		X += SlotW + 8.f * UIScale;
	}
}

void AArenaHUD::DrawMatchInfo(const AArenaGameState* GS)
{
	UFont* Font = GEngine->GetMediumFont();

	const int32 Seconds = FMath::CeilToInt(GS->GetTimeRemaining());
	if (GS->MatchEndTime > 0.f)
	{
		DrawCentered(FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), 20.f * UIScale, TextColor, Font, 1.3f);
	}

	const TArray<AArenaPlayerState*> Sorted = GS->GetSortedPlayers();
	const AArenaPlayerState* Mine = GetOwningPlayerController() ? GetOwningPlayerController()->GetPlayerState<AArenaPlayerState>() : nullptr;
	if (Mine)
	{
		const int32 Rank = Sorted.IndexOfByKey(Mine) + 1;
		const FString Text = FString::Printf(TEXT("Frags %d   Rank %d/%d   Limit %d"), Mine->Frags, Rank, Sorted.Num(), GS->FragLimit);
		float W = 0.f, H = 0.f;
		GetTextSize(Text, W, H, Font, UIScale);
		DrawText(Text, TextColor, Canvas->ClipX - W - 30.f * UIScale, 20.f * UIScale, Font, UIScale);
	}
}

void AArenaHUD::DrawKillFeed(const AArenaGameState* GS)
{
	UFont* Font = GEngine->GetSmallFont();
	const float Now = GetWorld()->GetTimeSeconds();
	float Y = 20.f * UIScale;
	for (const FArenaKillFeedEntry& Entry : GS->GetKillFeed())
	{
		if (Now - Entry.Time > 6.f)
		{
			continue;
		}
		const FString Line = Entry.Killer.IsEmpty()
			? FString::Printf(TEXT("%s died"), *Entry.Victim)
			: FString::Printf(TEXT("%s  [%s]  %s"), *Entry.Killer, GetWeaponInfo(Entry.Weapon).Name, *Entry.Victim);
		DrawText(Line, TextColor, 30.f * UIScale, Y, Font, 1.1f * UIScale);
		Y += 22.f * UIScale;
	}
}

void AArenaHUD::DrawScoreboard(const AArenaGameState* GS)
{
	UFont* Font = GEngine->GetMediumFont();
	const TArray<AArenaPlayerState*> Sorted = GS->GetSortedPlayers();
	const float W = 620.f * UIScale;
	const float RowH = 34.f * UIScale;
	const float X = (Canvas->ClipX - W) * 0.5f;
	float Y = Canvas->ClipY * 0.2f;

	DrawRect(Panel, X, Y, W, RowH * (Sorted.Num() + 2) + 20.f * UIScale);

	const FString Title = GS->WinnerName.IsEmpty() ? FString(TEXT("DEATHMATCH")) : FString::Printf(TEXT("%s WINS"), *GS->WinnerName);
	DrawCentered(Title, Y + 8.f * UIScale, FLinearColor(1.f, 0.8f, 0.2f), Font, 1.3f);
	Y += RowH + 10.f * UIScale;

	DrawText(TEXT("NAME"), DimColor, X + 50.f * UIScale, Y, Font, UIScale);
	DrawText(TEXT("FRAGS"), DimColor, X + 360.f * UIScale, Y, Font, UIScale);
	DrawText(TEXT("DEATHS"), DimColor, X + 450.f * UIScale, Y, Font, UIScale);
	DrawText(TEXT("PING"), DimColor, X + 550.f * UIScale, Y, Font, UIScale);
	Y += RowH;

	for (const AArenaPlayerState* PS : Sorted)
	{
		DrawRect(PS->GetPlayerColor(), X + 16.f * UIScale, Y + 6.f * UIScale, 20.f * UIScale, 20.f * UIScale);
		DrawText(PS->GetPlayerName(), TextColor, X + 50.f * UIScale, Y, Font, UIScale);
		DrawText(FString::FromInt(PS->Frags), TextColor, X + 360.f * UIScale, Y, Font, UIScale);
		DrawText(FString::FromInt(PS->Deaths), TextColor, X + 450.f * UIScale, Y, Font, UIScale);
		DrawText(FString::FromInt(FMath::RoundToInt(PS->GetPingInMilliseconds())), TextColor, X + 550.f * UIScale, Y, Font, UIScale);
		Y += RowH;
	}
}
