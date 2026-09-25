#include "ArenaSettings.h"

#define LOCTEXT_NAMESPACE "ArenaSettings"

const TArray<FArenaBindableAction>& UArenaSettings::GetBindableActions()
{
	static const TArray<FArenaBindableAction> Actions =
	{
		{ "MoveForward", LOCTEXT("MoveForward", "Move forward"), EKeys::W, EKeys::Up },
		{ "MoveBack",    LOCTEXT("MoveBack", "Move back"),       EKeys::S, EKeys::Down },
		{ "MoveLeft",    LOCTEXT("MoveLeft", "Strafe left"),     EKeys::A, EKeys::Left },
		{ "MoveRight",   LOCTEXT("MoveRight", "Strafe right"),   EKeys::D, EKeys::Right },
		// HL1 bhoppers jump with the scroll wheel.
		{ "Jump",        LOCTEXT("Jump", "Jump"),                EKeys::SpaceBar, EKeys::MouseScrollDown },
		{ "Crouch",      LOCTEXT("Crouch", "Crouch"),            EKeys::LeftControl, EKeys::C },
		{ "Fire",        LOCTEXT("Fire", "Fire"),                EKeys::LeftMouseButton, EKeys::Invalid },
		{ "NextWeapon",  LOCTEXT("NextWeapon", "Next weapon"),   EKeys::E, EKeys::MouseScrollUp },
		{ "LastWeapon",  LOCTEXT("LastWeapon", "Last weapon"),   EKeys::Q, EKeys::Invalid },
		{ "Weapon1",     LOCTEXT("Weapon1", "Gauntlet"),         EKeys::One, EKeys::Invalid },
		{ "Weapon2",     LOCTEXT("Weapon2", "Machinegun"),       EKeys::Two, EKeys::Invalid },
		{ "Weapon3",     LOCTEXT("Weapon3", "Shotgun"),          EKeys::Three, EKeys::Invalid },
		{ "Weapon4",     LOCTEXT("Weapon4", "Grenade launcher"), EKeys::Four, EKeys::Invalid },
		{ "Weapon5",     LOCTEXT("Weapon5", "Rocket launcher"),  EKeys::Five, EKeys::Invalid },
		{ "Weapon6",     LOCTEXT("Weapon6", "Lightning gun"),    EKeys::Six, EKeys::Invalid },
		{ "Weapon7",     LOCTEXT("Weapon7", "Railgun"),          EKeys::Seven, EKeys::Invalid },
		{ "Weapon8",     LOCTEXT("Weapon8", "Plasma gun"),       EKeys::Eight, EKeys::Invalid },
		{ "Scoreboard",  LOCTEXT("Scoreboard", "Scoreboard"),    EKeys::Tab, EKeys::Invalid },
	};
	return Actions;
}

FArenaKeyBinding UArenaSettings::GetBinding(FName Action) const
{
	if (const FArenaKeyBinding* Custom = KeyBindings.FindByPredicate([Action](const FArenaKeyBinding& B) { return B.Action == Action; }))
	{
		return *Custom;
	}
	FArenaKeyBinding Result;
	Result.Action = Action;
	if (const FArenaBindableAction* Def = GetBindableActions().FindByPredicate([Action](const FArenaBindableAction& A) { return A.Id == Action; }))
	{
		Result.Primary = Def->DefaultPrimary;
		Result.Secondary = Def->DefaultSecondary;
	}
	return Result;
}

void UArenaSettings::SetBindingKey(FName Action, int32 Slot, const FKey& Key)
{
	// Materialize the full table on first change so every action has an entry.
	if (KeyBindings.Num() == 0)
	{
		for (const FArenaBindableAction& Def : GetBindableActions())
		{
			KeyBindings.Add(GetBinding(Def.Id));
		}
	}

	// A key does one thing: take it away from wherever else it was bound.
	if (Key.IsValid())
	{
		for (FArenaKeyBinding& Binding : KeyBindings)
		{
			for (int32 i = 0; i < 2; ++i)
			{
				if (Binding.GetKey(i) == Key)
				{
					Binding.GetKey(i) = EKeys::Invalid;
				}
			}
		}
	}

	if (FArenaKeyBinding* Binding = KeyBindings.FindByPredicate([Action](const FArenaKeyBinding& B) { return B.Action == Action; }))
	{
		Binding->GetKey(FMath::Clamp(Slot, 0, 1)) = Key;
	}
}

#undef LOCTEXT_NAMESPACE
