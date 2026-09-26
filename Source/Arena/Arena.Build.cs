using UnrealBuildTool;

public class Arena : ModuleRules
{
	public Arena(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file compiles on its own, so file-local helpers can share names safely.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "NetCore", "Slate", "SlateCore", "AIModule", "OnlineSubsystem", "OnlineSubsystemUtils"
		});
	}
}
