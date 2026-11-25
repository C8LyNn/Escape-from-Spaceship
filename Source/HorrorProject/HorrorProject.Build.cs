using UnrealBuildTool;

public class HorrorProject : ModuleRules
{
    public HorrorProject(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "UMG" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "PhysicsCore", "EnhancedInput" });
    }
}
