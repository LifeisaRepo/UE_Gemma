// Copyright © 2026 Sanjyot Dahale.

using UnrealBuildTool;
using System.Collections.Generic;

public class UE_GemmaTarget : TargetRules
{
	public UE_GemmaTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;

		ExtraModuleNames.AddRange( new string[] { "UE_Gemma" } );
	}
}
