#include "SArenaMapVote.h"
#include "ArenaGameState.h"
#include "ArenaMap.h"
#include "ArenaPlayerController.h"
#include "ArenaPlayerState.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ArenaMapVote"

namespace
{
	const FLinearColor Accent(0.95f, 0.45f, 0.08f);
	const FLinearColor TextColor(0.93f, 0.93f, 0.93f);
	const FLinearColor DimText(0.72f, 0.72f, 0.75f);

	FSlateFontInfo Font(const char* Weight, int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}
}

void SArenaMapVote::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	CardStyle = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.06f, 0.06f, 0.08f, 0.9f), 6.f))
		.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.16f, 0.16f, 0.2f, 0.95f), 6.f))
		.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.04f, 0.04f, 0.05f, 1.f), 6.f))
		.SetNormalPadding(FMargin(0.f))
		.SetPressedPadding(FMargin(0.f, 2.f, 0.f, 0.f));

	TSharedRef<SHorizontalBox> Cards = SNew(SHorizontalBox);
	for (const FArenaMapDef& Map : ArenaMap::GetMaps())
	{
		Cards->AddSlot().AutoWidth().Padding(8.f, 0.f)[MakeCard(Map)];
	}

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Bottom)
	.Padding(FMargin(0.f, 0.f, 0.f, 70.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Font(Font("Bold", 22))
			.ColorAndOpacity(TextColor)
			.ShadowOffset(FVector2D(2.f, 2.f))
			.Text_Lambda([this]
			{
				const AArenaGameState* GS = GetGameState();
				const int32 Seconds = GS ? FMath::CeilToInt(GS->GetVoteTimeRemaining()) : 0;
				return FText::Format(LOCTEXT("Title", "VOTE FOR THE NEXT MAP   {0}"), FText::AsNumber(Seconds));
			})
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 14.f)
		[
			SNew(STextBlock)
			.Font(Font("Regular", 13))
			.ColorAndOpacity(DimText)
			.ShadowOffset(FVector2D(1.f, 1.f))
			.Text(LOCTEXT("Hint", "Click a map to vote. Most votes wins; ties are decided at random."))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			Cards
		]
	];
}

const AArenaGameState* SArenaMapVote::GetGameState() const
{
	return Owner.IsValid() ? Owner->GetWorld()->GetGameState<AArenaGameState>() : nullptr;
}

FName SArenaMapVote::GetMyVote() const
{
	const AArenaPlayerState* PS = Owner.IsValid() ? Owner->GetPlayerState<AArenaPlayerState>() : nullptr;
	return PS ? PS->VotedMap : NAME_None;
}

TSharedRef<SWidget> SArenaMapVote::MakeCard(const FArenaMapDef& Map)
{
	const FName Id = Map.Id;
	return SNew(SBox)
		.WidthOverride(260.f)
		.HeightOverride(150.f)
		.Visibility_Lambda([this, Id]
		{
			const AArenaGameState* GS = GetGameState();
			return GS && GS->VoteOptions.Contains(Id) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			// Orange frame around the map you voted for.
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([this, Id] { return FSlateColor(GetMyVote() == Id ? Accent : FLinearColor::Transparent); })
			.Padding(3.f)
			[
				SNew(SButton)
				.ButtonStyle(&CardStyle)
				.ContentPadding(FMargin(16.f, 12.f))
				.OnClicked_Lambda([this, Id]
				{
					if (Owner.IsValid()) { Owner->ServerVoteMap(Id); }
					return FReply::Handled();
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(Map.DisplayName)
						.Font(Font("Bold", 20))
						.ColorAndOpacity_Lambda([this, Id] { return FSlateColor(GetMyVote() == Id ? Accent : TextColor); })
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 4.f)
					[
						SNew(STextBlock)
						.Text(Map.Description)
						.Font(Font("Regular", 11))
						.ColorAndOpacity(DimText)
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Font(Font("Bold", 14))
						.ColorAndOpacity(TextColor)
						.Text_Lambda([this, Id]
						{
							const AArenaGameState* GS = GetGameState();
							const int32 Index = GS ? GS->VoteOptions.IndexOfByKey(Id) : INDEX_NONE;
							const int32 Votes = GS && GS->VoteCounts.IsValidIndex(Index) ? GS->VoteCounts[Index] : 0;
							return FText::Format(LOCTEXT("Votes", "{0} {0}|plural(one=vote,other=votes)"), Votes);
						})
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
