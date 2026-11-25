using UnrealBuildTool;

public class HorrorProjectTarget : TargetRules
{
    public HorrorProjectTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("HorrorProject");
    }
}
