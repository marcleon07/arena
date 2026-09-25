#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class AArenaPlayerController;
class AArenaGameState;
struct FArenaMapDef;

/**
 * End-of-match map vote: one card per map on offer with live vote counts and a
 * countdown. Reads everything from the replicated game state, so it can be shown
 * before the vote options arrive.
 */
class SArenaMapVote : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArenaMapVote) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AArenaPlayerController>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Takes focus while the vote is up, so game input stays off until the next map. */
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	TSharedRef<SWidget> MakeCard(const FArenaMapDef& Map);
	const AArenaGameState* GetGameState() const;
	FName GetMyVote() const;

	TWeakObjectPtr<AArenaPlayerController> Owner;
	FButtonStyle CardStyle;
};
