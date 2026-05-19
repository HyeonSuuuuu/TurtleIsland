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
			"Sockets",
			"Networking"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			// 모듈 루트 (TurtleIsland.h 등 루트 파일 접근용)
			"TurtleIsland",
			// 팀 작업 코드
			"TurtleIsland/_Turtle",
			"TurtleIsland/_Turtle/UI",
			"TurtleIsland/_Turtle/Network",
			"TurtleIsland/_Turtle/Character",
			// Epic 샘플 참고 코드 (루트로 Variant_*/... 스타일 include 지원)
			"TurtleIsland/_Reference",
			"TurtleIsland/_Reference/Variant_Platforming",
			"TurtleIsland/_Reference/Variant_Platforming/Animation",
			"TurtleIsland/_Reference/Variant_Combat",
			"TurtleIsland/_Reference/Variant_Combat/AI",
			"TurtleIsland/_Reference/Variant_Combat/Animation",
			"TurtleIsland/_Reference/Variant_Combat/Gameplay",
			"TurtleIsland/_Reference/Variant_Combat/Interfaces",
			"TurtleIsland/_Reference/Variant_Combat/UI",
			"TurtleIsland/_Reference/Variant_SideScrolling",
			"TurtleIsland/_Reference/Variant_SideScrolling/AI",
			"TurtleIsland/_Reference/Variant_SideScrolling/Gameplay",
			"TurtleIsland/_Reference/Variant_SideScrolling/Interfaces",
			"TurtleIsland/_Reference/Variant_SideScrolling/UI",
			"../../../Common"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
