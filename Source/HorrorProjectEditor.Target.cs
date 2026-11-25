using UnrealBuildTool;

public class HorrorProjectEditorTarget : TargetRules
{
    public HorrorProjectEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("HorrorProject");
    }
}
