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

		PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry" });

		// 모듈 루트 + 공통 프로토콜만. 하위 폴더는 "Network/NetSubsystem.h" 처럼 경로를 명시해 참조한다.
		PublicIncludePaths.AddRange(new string[] {
			"TurtleIsland",
			"../../../Common"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
