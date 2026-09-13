// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class TurtleIslandEditorTarget : TargetRules
{
	public TurtleIslandEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("TurtleIsland");
		ExtraModuleNames.Add("TurtleIslandCollisionEditor");
	}
}
