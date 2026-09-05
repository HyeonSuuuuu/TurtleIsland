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
			"TurtleIsland/_Turtle/Gameplay",
			"TurtleIsland/_Turtle/Network",
			"TurtleIsland/_Turtle/UI",
			"../../../Common"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
