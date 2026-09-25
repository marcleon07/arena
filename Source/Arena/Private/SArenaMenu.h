#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class AArenaPlayerController;
class SEditableTextBox;
class SWidgetSwitcher;

/**
 * Main menu (standalone, before joining a game) and pause menu (in a match).
 * Pure Slate so the project needs no widget assets. All actions go through the
 * owning player controller.
 */
class SArenaMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArenaMenu)
		: _InGame(false)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<AArenaPlayerController>, Owner)
		SLATE_ARGUMENT(bool, InGame)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	enum class EPage : int32
	{
		Main,
		Host,
		Join,
		Settings,
	};

	void ShowPage(EPage Page);
	void SaveSettings();

	TSharedRef<SWidget> MakeMainPage();
	TSharedRef<SWidget> MakeHostPage();
	TSharedRef<SWidget> MakeJoinPage();
	TSharedRef<SWidget> MakeSettingsPage();

	TSharedRef<SWidget> MakeHeading(const FText& Text);
	TSharedRef<SWidget> MakeButton(const FText& Label, FOnClicked OnClicked, bool bPrimary = false);
	TSharedRef<SWidget> MakeSliderRow(const FText& Label, float Min, float Max, float Step, int32 Decimals,
		TFunction<float()> Getter, TFunction<void(float)> Setter);
	TSharedRef<SWidget> MakeCheckRow(const FText& Label, TFunction<bool()> Getter, TFunction<void(bool)> Setter);
	TSharedRef<SWidget> MakeLabel(const FText& Text);

	TWeakObjectPtr<AArenaPlayerController> Owner;
	bool bInGame = false;
	EPage CurrentPage = EPage::Main;

	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> AddressBox;

	// Slate keeps pointers to styles, so they live as long as the widget.
	FButtonStyle ButtonStyle;
	FButtonStyle PrimaryButtonStyle;
};
