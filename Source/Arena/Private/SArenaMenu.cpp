#include "SArenaMenu.h"
#include "ArenaMap.h"
#include "ArenaPlayerController.h"
#include "ArenaSettings.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ArenaMenu"

namespace
{
	const FLinearColor Accent(0.95f, 0.45f, 0.08f);
	const FLinearColor TextColor(0.93f, 0.93f, 0.93f);
	const FLinearColor DimText(0.72f, 0.72f, 0.75f);
	constexpr float ColumnWidth = 520.f;
	constexpr float SettingsHeight = 520.f;

	FSlateFontInfo Font(const char* Weight, int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}

	FButtonStyle MakeButtonStyle(const FLinearColor& Normal, const FLinearColor& Hovered, const FLinearColor& Pressed)
	{
		return FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(Normal, 4.f))
			.SetHovered(FSlateRoundedBoxBrush(Hovered, 4.f))
			.SetPressed(FSlateRoundedBoxBrush(Pressed, 4.f))
			.SetDisabled(FSlateRoundedBoxBrush(Normal * 0.5f, 4.f))
			.SetNormalPadding(FMargin(0.f))
			.SetPressedPadding(FMargin(0.f, 2.f, 0.f, 0.f));
	}
}

DECLARE_DELEGATE_OneParam(FOnArenaKeyCaptured, const FKey&);

/** Full-screen catcher for the next key, mouse button or wheel tick. */
class SArenaKeyCapture : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArenaKeyCapture) {}
		SLATE_EVENT(FOnArenaKeyCaptured, OnKeyCaptured)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnKeyCaptured = InArgs._OnKeyCaptured;
		ChildSlot[InArgs._Content.Widget];
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& InKeyEvent) override
	{
		OnKeyCaptured.ExecuteIfBound(InKeyEvent.GetKey());
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent) override
	{
		OnKeyCaptured.ExecuteIfBound(MouseEvent.GetEffectingButton());
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent& MouseEvent) override
	{
		OnKeyCaptured.ExecuteIfBound(MouseEvent.GetEffectingButton());
		return FReply::Handled();
	}

	virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& MouseEvent) override
	{
		OnKeyCaptured.ExecuteIfBound(MouseEvent.GetWheelDelta() > 0.f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown);
		return FReply::Handled();
	}

private:
	FOnArenaKeyCaptured OnKeyCaptured;
};

void SArenaMenu::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	bInGame = InArgs._InGame;

	ButtonStyle = MakeButtonStyle(FLinearColor(0.08f, 0.08f, 0.1f, 0.85f), FLinearColor(0.2f, 0.2f, 0.24f, 0.95f), FLinearColor(0.05f, 0.05f, 0.06f, 1.f));
	PrimaryButtonStyle = MakeButtonStyle(Accent * 0.8f, Accent, Accent * 0.6f);
	KeyButtonStyle = MakeButtonStyle(FLinearColor(0.16f, 0.16f, 0.2f, 0.95f), FLinearColor(0.3f, 0.3f, 0.36f, 1.f), Accent * 0.6f);

	// Main menu shows the arena orbiting behind a light tint; in-game dims the match more.
	const FLinearColor Backdrop = bInGame ? FLinearColor(0.f, 0.f, 0.f, 0.6f) : FLinearColor(0.f, 0.f, 0.02f, 0.35f);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(Backdrop)
		]
		// Dark column behind the menu so it reads over a bright scene.
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		[
			SNew(SBox)
			.WidthOverride(ColumnWidth + 220.f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.01f, 0.01f, 0.02f, 0.72f))
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(FMargin(110.f, 60.f))
		[
			SNew(SBox)
			.WidthOverride(ColumnWidth)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "ARENA"))
					.Font(Font("Bold", 84))
					.ColorAndOpacity(Accent)
					.ShadowOffset(FVector2D(3.f, 3.f))
					.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 0.f, 36.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Tagline", "Rockets, rails and bunnyhops"))
					.Font(Font("Regular", 16))
					.ColorAndOpacity(DimText)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(Switcher, SWidgetSwitcher)
					+ SWidgetSwitcher::Slot()[MakeMainPage()]
					+ SWidgetSwitcher::Slot()[MakeHostPage()]
					+ SWidgetSwitcher::Slot()[MakeJoinPage()]
					+ SWidgetSwitcher::Slot()[MakeSettingsPage()]
				]
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(24.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Hint", "Esc: back    ` : console"))
			.Font(Font("Regular", 11))
			.ColorAndOpacity(DimText)
		]
		+ SOverlay::Slot()
		[
			MakeCaptureOverlay()
		]
	];
}

FReply SArenaMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F10)
	{
		if (CurrentPage != EPage::Main)
		{
			ShowPage(EPage::Main);
		}
		else if (bInGame && Owner.IsValid())
		{
			Owner->CloseMenu();
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SArenaMenu::ShowPage(EPage Page)
{
	if (CurrentPage == EPage::Settings && Page != EPage::Settings)
	{
		SaveSettings();
	}
	CurrentPage = Page;
	Switcher->SetActiveWidgetIndex(static_cast<int32>(Page));
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
}

void SArenaMenu::SaveSettings()
{
	UArenaSettings* Settings = UArenaSettings::Get();
	if (NameBox.IsValid())
	{
		Settings->PlayerName = NameBox->GetText().ToString().TrimStartAndEnd().Left(20);
	}
	Settings->Save();
	if (Owner.IsValid())
	{
		Owner->ApplyUserSettings(true);
	}
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SArenaMenu::MakeMainPage()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto AddButton = [this, &Box](const FText& Label, FOnClicked OnClicked, bool bPrimary = false)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[MakeButton(Label, OnClicked, bPrimary)];
	};

	if (bInGame)
	{
		AddButton(LOCTEXT("Resume", "RESUME"), FOnClicked::CreateLambda([this]
		{
			if (Owner.IsValid()) { Owner->CloseMenu(); }
			return FReply::Handled();
		}), true);
	}
	else
	{
		AddButton(LOCTEXT("Host", "HOST GAME"), FOnClicked::CreateLambda([this] { ShowPage(EPage::Host); return FReply::Handled(); }), true);
		AddButton(LOCTEXT("Join", "JOIN GAME"), FOnClicked::CreateLambda([this] { ShowPage(EPage::Join); return FReply::Handled(); }));
	}
	AddButton(LOCTEXT("Settings", "SETTINGS"), FOnClicked::CreateLambda([this] { ShowPage(EPage::Settings); return FReply::Handled(); }));
	if (bInGame)
	{
		AddButton(LOCTEXT("Disconnect", "LEAVE MATCH"), FOnClicked::CreateLambda([this]
		{
			if (Owner.IsValid()) { Owner->Disconnect(); }
			return FReply::Handled();
		}));
	}
	AddButton(LOCTEXT("Quit", "QUIT"), FOnClicked::CreateLambda([this]
	{
		if (Owner.IsValid()) { Owner->QuitToDesktop(); }
		return FReply::Handled();
	}));
	return Box;
}

TSharedRef<SWidget> SArenaMenu::MakeHostPage()
{
	UArenaSettings* Settings = UArenaSettings::Get();

	// One button per map; the chosen one is highlighted, with its description below.
	TSharedRef<SHorizontalBox> MapButtons = SNew(SHorizontalBox);
	const TArray<FArenaMapDef>& Maps = ArenaMap::GetMaps();
	for (int32 i = 0; i < Maps.Num(); ++i)
	{
		const FName Id = Maps[i].Id;
		MapButtons->AddSlot().FillWidth(1.f).Padding(i == 0 ? 0.f : 5.f, 0.f, i == Maps.Num() - 1 ? 0.f : 5.f, 0.f)
		[
			SNew(SBox)
			.HeightOverride(44.f)
			[
				SNew(SButton)
				.ButtonStyle(&KeyButtonStyle)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([Settings, Id] { Settings->HostMap = Id; return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(Maps[i].DisplayName)
					.Font(Font("Bold", 15))
					.ColorAndOpacity_Lambda([Settings, Id]
					{
						return FSlateColor(ArenaMap::Get(Settings->HostMap).Id == Id ? Accent : TextColor);
					})
				]
			]
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeHeading(LOCTEXT("HostHeading", "Host a deathmatch"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeLabel(LOCTEXT("Map", "Map"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 6.f)[MapButtons]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
		[
			SNew(SBox)
			.MinDesiredHeight(36.f)
			[
				SNew(STextBlock)
				.Font(Font("Regular", 12))
				.ColorAndOpacity(DimText)
				.AutoWrapText(true)
				.Text_Lambda([Settings] { return ArenaMap::Get(Settings->HostMap).Description; })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSliderRow(LOCTEXT("FragLimit", "Frag limit"), 5.f, 100.f, 5.f, 0,
				[Settings] { return static_cast<float>(Settings->HostFragLimit); },
				[Settings](float V) { Settings->HostFragLimit = FMath::RoundToInt(V); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSliderRow(LOCTEXT("TimeLimit", "Time limit (min, 0 = none)"), 0.f, 60.f, 1.f, 0,
				[Settings] { return static_cast<float>(Settings->HostTimeLimit); },
				[Settings](float V) { Settings->HostTimeLimit = FMath::RoundToInt(V); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 18.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HostHint", "Friends join with your IP address (port 7777)."))
			.Font(Font("Regular", 12))
			.ColorAndOpacity(DimText)
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			MakeButton(LOCTEXT("Start", "START MATCH"), FOnClicked::CreateLambda([this]
			{
				UArenaSettings::Get()->Save();
				if (Owner.IsValid()) { Owner->HostGame(); }
				return FReply::Handled();
			}), true)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeButton(LOCTEXT("Back", "BACK"), FOnClicked::CreateLambda([this] { ShowPage(EPage::Main); return FReply::Handled(); }))
		];
}

TSharedRef<SWidget> SArenaMenu::MakeJoinPage()
{
	auto Connect = [this]
	{
		const FString Address = AddressBox->GetText().ToString().TrimStartAndEnd();
		if (!Address.IsEmpty() && Owner.IsValid())
		{
			UArenaSettings::Get()->LastJoinAddress = Address;
			UArenaSettings::Get()->Save();
			Owner->JoinGame(Address);
		}
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeHeading(LOCTEXT("JoinHeading", "Join a game"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeLabel(LOCTEXT("Address", "Server address (IP or IP:port)"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 18.f)
		[
			SAssignNew(AddressBox, SEditableTextBox)
			.Text(FText::FromString(UArenaSettings::Get()->LastJoinAddress))
			.Font(Font("Regular", 18))
			.SelectAllTextWhenFocused(true)
			.OnTextCommitted_Lambda([Connect](const FText&, ETextCommit::Type Commit)
			{
				if (Commit == ETextCommit::OnEnter) { Connect(); }
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			MakeButton(LOCTEXT("Connect", "CONNECT"), FOnClicked::CreateLambda([Connect] { Connect(); return FReply::Handled(); }), true)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeButton(LOCTEXT("Back", "BACK"), FOnClicked::CreateLambda([this] { ShowPage(EPage::Main); return FReply::Handled(); }))
		];
}

TSharedRef<SWidget> SArenaMenu::MakeSettingsPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeHeading(LOCTEXT("SettingsHeading", "Settings"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)[MakeTabButton(LOCTEXT("TabGeneral", "GENERAL"), ESettingsTab::General)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(5.f, 0.f, 0.f, 0.f)[MakeTabButton(LOCTEXT("TabControls", "CONTROLS"), ESettingsTab::Controls)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
		[
			SNew(SBox)
			.HeightOverride(SettingsHeight)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this] { return static_cast<int32>(SettingsTab); })
				+ SWidgetSwitcher::Slot()[MakeGeneralSettings()]
				+ SWidgetSwitcher::Slot()[MakeControlsSettings()]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeButton(LOCTEXT("Back", "BACK"), FOnClicked::CreateLambda([this] { ShowPage(EPage::Main); return FReply::Handled(); }))
		];
}

TSharedRef<SWidget> SArenaMenu::MakeTabButton(const FText& Label, ESettingsTab Tab)
{
	return SNew(SBox)
		.HeightOverride(44.f)
		[
			SNew(SButton)
			.ButtonStyle(&ButtonStyle)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this, Tab] { SettingsTab = Tab; return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font("Bold", 16))
				.ColorAndOpacity_Lambda([this, Tab] { return FSlateColor(SettingsTab == Tab ? Accent : DimText); })
			]
		];
}

TSharedRef<SWidget> SArenaMenu::MakeGeneralSettings()
{
	UArenaSettings* Settings = UArenaSettings::Get();
	TWeakObjectPtr<AArenaPlayerController> WeakOwner = Owner;
	// Volume and FOV apply live; display options apply when leaving the page.
	auto Apply = [WeakOwner]
	{
		if (WeakOwner.IsValid()) { WeakOwner->ApplyUserSettings(false); }
	};

	return SNew(SScrollBox)
		+ SScrollBox::Slot()[MakeLabel(LOCTEXT("Name", "Player name"))]
		+ SScrollBox::Slot().Padding(0.f, 6.f, 12.f, 14.f)
		[
			SAssignNew(NameBox, SEditableTextBox)
			.Text(FText::FromString(Settings->PlayerName))
			.HintText(LOCTEXT("NameHint", "PlayerN"))
			.Font(Font("Regular", 16))
		]
		+ SScrollBox::Slot().Padding(0.f, 0.f, 12.f, 0.f)
		[
			MakeSliderRow(LOCTEXT("Sensitivity", "Mouse sensitivity"), 0.2f, 10.f, 0.05f, 2,
				[Settings] { return Settings->Sensitivity; },
				[Settings](float V) { Settings->Sensitivity = V; })
		]
		+ SScrollBox::Slot()
		[
			MakeCheckRow(LOCTEXT("Invert", "Invert mouse"),
				[Settings] { return Settings->bInvertMouse; },
				[Settings](bool b) { Settings->bInvertMouse = b; })
		]
		+ SScrollBox::Slot().Padding(0.f, 0.f, 12.f, 0.f)
		[
			MakeSliderRow(LOCTEXT("FOV", "Field of view"), 80.f, 130.f, 1.f, 0,
				[Settings] { return Settings->FieldOfView; },
				[Settings](float V) { Settings->FieldOfView = V; })
		]
		+ SScrollBox::Slot().Padding(0.f, 0.f, 12.f, 0.f)
		[
			MakeSliderRow(LOCTEXT("Volume", "Volume"), 0.f, 100.f, 1.f, 0,
				[Settings] { return Settings->MasterVolume * 100.f; },
				[Settings, Apply](float V) { Settings->MasterVolume = V / 100.f; Apply(); })
		]
		+ SScrollBox::Slot()
		[
			MakeCheckRow(LOCTEXT("AutoHop", "Auto-hop (hold jump to bunnyhop)"),
				[Settings] { return Settings->bAutoHop; },
				[Settings](bool b) { Settings->bAutoHop = b; })
		]
		+ SScrollBox::Slot()
		[
			MakeCheckRow(LOCTEXT("Fullscreen", "Fullscreen"),
				[Settings] { return Settings->bFullscreen; },
				[Settings, WeakOwner](bool b)
				{
					Settings->bFullscreen = b;
					if (WeakOwner.IsValid()) { WeakOwner->ApplyUserSettings(true); }
				})
		]
		+ SScrollBox::Slot()
		[
			MakeCheckRow(LOCTEXT("VSync", "VSync"),
				[Settings] { return Settings->bVSync; },
				[Settings](bool b) { Settings->bVSync = b; })
		]
		+ SScrollBox::Slot().Padding(0.f, 0.f, 12.f, 0.f)
		[
			MakeSliderRow(LOCTEXT("FrameLimit", "Frame rate limit (0 = unlimited)"), 0.f, 360.f, 10.f, 0,
				[Settings] { return Settings->FrameRateLimit; },
				[Settings](float V) { Settings->FrameRateLimit = V; })
		]
		+ SScrollBox::Slot()
		[
			MakeCheckRow(LOCTEXT("ShowFPS", "Show FPS"),
				[Settings] { return Settings->bShowFPS; },
				[Settings](bool b) { Settings->bShowFPS = b; })
		];
}

TSharedRef<SWidget> SArenaMenu::MakeControlsSettings()
{
	TSharedRef<SScrollBox> Rows = SNew(SScrollBox);
	for (const FArenaBindableAction& Action : UArenaSettings::GetBindableActions())
	{
		Rows->AddSlot().Padding(0.f, 0.f, 12.f, 6.f)[MakeBindingRow(Action)];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 12.f, 8.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ControlsHint", "Click a key to change it, then press a key, mouse button or scroll the wheel."))
			.Font(Font("Regular", 12))
			.ColorAndOpacity(DimText)
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[Rows]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeButton(LOCTEXT("ResetKeys", "RESET TO DEFAULTS"), FOnClicked::CreateLambda([this]
			{
				UArenaSettings::Get()->ResetBindings();
				UArenaSettings::Get()->Save();
				if (Owner.IsValid()) { Owner->ApplyKeyBindings(); }
				return FReply::Handled();
			}))
		];
}

TSharedRef<SWidget> SArenaMenu::MakeBindingRow(const FArenaBindableAction& Action)
{
	const FName Id = Action.Id;
	auto KeyButton = [this, Id](int32 Slot) -> TSharedRef<SWidget>
	{
		return SNew(SBox)
			.WidthOverride(170.f)
			.HeightOverride(34.f)
			[
				SNew(SButton)
				.ButtonStyle(&KeyButtonStyle)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([this, Id, Slot] { BeginCapture(Id, Slot); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Font(Font("Bold", 13))
					.Text_Lambda([this, Id, Slot]
					{
						if (CaptureAction == Id && CaptureSlot == Slot)
						{
							return LOCTEXT("Waiting", "...");
						}
						const FKey Key = UArenaSettings::Get()->GetBinding(Id).GetKey(Slot);
						return Key.IsValid() ? Key.GetDisplayName(false) : FText::FromString(TEXT("-"));
					})
					.ColorAndOpacity_Lambda([this, Id, Slot]
					{
						const bool bEmpty = !UArenaSettings::Get()->GetBinding(Id).GetKey(Slot).IsValid();
						return FSlateColor(bEmpty ? DimText : TextColor);
					})
				]
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Action.Label)
			.Font(Font("Regular", 14))
			.ColorAndOpacity(TextColor)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f)[KeyButton(0)]
		+ SHorizontalBox::Slot().AutoWidth()[KeyButton(1)];
}

TSharedRef<SWidget> SArenaMenu::MakeCaptureOverlay()
{
	return SAssignNew(CaptureWidget, SArenaKeyCapture)
		.Visibility_Lambda([this] { return IsCapturing() ? EVisibility::Visible : EVisibility::Collapsed; })
		.OnKeyCaptured(FOnArenaKeyCaptured::CreateSP(this, &SArenaMenu::OnKeyCaptured))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.75f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Font(Font("Bold", 28))
					.ColorAndOpacity(TextColor)
					.Text_Lambda([this]
					{
						const FArenaBindableAction* Action = UArenaSettings::GetBindableActions().FindByPredicate(
							[this](const FArenaBindableAction& A) { return A.Id == CaptureAction; });
						return FText::Format(LOCTEXT("PressKey", "Press a key for \"{0}\""), Action ? Action->Label : FText::GetEmpty());
					})
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(Font("Regular", 16))
					.ColorAndOpacity(DimText)
					.Text(LOCTEXT("CaptureHint", "Esc to cancel    Backspace to clear"))
				]
			]
		];
}

void SArenaMenu::BeginCapture(FName Action, int32 Slot)
{
	CaptureAction = Action;
	CaptureSlot = Slot;
	FSlateApplication::Get().SetKeyboardFocus(CaptureWidget);
}

void SArenaMenu::OnKeyCaptured(const FKey& Key)
{
	if (Key == EKeys::Escape)
	{
		EndCapture();
		return;
	}
	// Reserved for the menu and console.
	if (Key == EKeys::F10 || Key == EKeys::Tilde || Key.IsGamepadKey() || Key.IsTouch())
	{
		return;
	}

	const FKey NewKey = (Key == EKeys::BackSpace || Key == EKeys::Delete) ? EKeys::Invalid : Key;
	UArenaSettings* Settings = UArenaSettings::Get();
	Settings->SetBindingKey(CaptureAction, CaptureSlot, NewKey);
	Settings->Save();
	if (Owner.IsValid())
	{
		Owner->ApplyKeyBindings();
	}
	EndCapture();
}

void SArenaMenu::EndCapture()
{
	CaptureAction = NAME_None;
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
}

// ---------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SArenaMenu::MakeHeading(const FText& Text)
{
	return SNew(SBox)
		.Padding(FMargin(0.f, 0.f, 0.f, 16.f))
		[
			SNew(STextBlock)
			.Text(Text)
			.Font(Font("Bold", 26))
			.ColorAndOpacity(TextColor)
		];
}

TSharedRef<SWidget> SArenaMenu::MakeLabel(const FText& Text)
{
	return SNew(STextBlock)
		.Text(Text)
		.Font(Font("Regular", 14))
		.ColorAndOpacity(DimText);
}

TSharedRef<SWidget> SArenaMenu::MakeButton(const FText& Label, FOnClicked OnClicked, bool bPrimary)
{
	return SNew(SBox)
		.HeightOverride(52.f)
		[
			SNew(SButton)
			.ButtonStyle(bPrimary ? &PrimaryButtonStyle : &ButtonStyle)
			.OnClicked(OnClicked)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(20.f, 0.f))
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font("Bold", 18))
				.ColorAndOpacity(TextColor)
			]
		];
}

TSharedRef<SWidget> SArenaMenu::MakeSliderRow(const FText& Label, float Min, float Max, float Step, int32 Decimals,
	TFunction<float()> Getter, TFunction<void(float)> Setter)
{
	FNumberFormattingOptions Format;
	Format.MinimumFractionalDigits = Decimals;
	Format.MaximumFractionalDigits = Decimals;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[MakeLabel(Label)]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([Getter, Format] { return FText::AsNumber(Getter(), &Format); })
				.Font(Font("Bold", 14))
				.ColorAndOpacity(TextColor)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 14.f)
		[
			SNew(SSlider)
			.MinValue(Min)
			.MaxValue(Max)
			.StepSize(Step)
			.MouseUsesStep(true)
			.Value_Lambda([Getter] { return Getter(); })
			.OnValueChanged_Lambda([Setter](float V) { Setter(V); })
		];
}

TSharedRef<SWidget> SArenaMenu::MakeCheckRow(const FText& Label, TFunction<bool()> Getter, TFunction<void(bool)> Setter)
{
	return SNew(SBox)
		.Padding(FMargin(0.f, 0.f, 0.f, 10.f))
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([Getter] { return Getter() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Setter](ECheckBoxState State) { Setter(State == ECheckBoxState::Checked); })
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font("Regular", 14))
				.ColorAndOpacity(TextColor)
			]
		];
}

#undef LOCTEXT_NAMESPACE
