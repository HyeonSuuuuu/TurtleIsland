// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TurtleIsland : ModuleRules
{
	public TurtleIsland(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"Sockets",
			"Networking"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"TurtleIsland",
			"TurtleIsland/_Turtle",
			"TurtleIsland/_Turtle/Character",
			"TurtleIsland/_Turtle/Network",
			"TurtleIsland/_Turtle/UI",
			"TurtleIsland/Variant_Platforming",
			"TurtleIsland/Variant_Platforming/Animation",
			"TurtleIsland/Variant_Combat",
			"TurtleIsland/Variant_Combat/AI",
			"TurtleIsland/Variant_Combat/Animation",
			"TurtleIsland/Variant_Combat/Gameplay",
			"TurtleIsland/Variant_Combat/Interfaces",
			"TurtleIsland/Variant_Combat/UI",
			"TurtleIsland/Variant_SideScrolling",
			"TurtleIsland/Variant_SideScrolling/AI",
			"TurtleIsland/Variant_SideScrolling/Gameplay",
			"TurtleIsland/Variant_SideScrolling/Interfaces",
			"TurtleIsland/Variant_SideScrolling/UI",
			"../../../Common"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
