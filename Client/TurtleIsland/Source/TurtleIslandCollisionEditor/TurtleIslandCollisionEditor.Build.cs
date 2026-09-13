using UnrealBuildTool;

public class TurtleIslandCollisionEditor : ModuleRules
{
    public TurtleIslandCollisionEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "Json", "PhysicsCore", "Landscape", "Water", "NavigationSystem", "TurtleIsland" });
    }
}
