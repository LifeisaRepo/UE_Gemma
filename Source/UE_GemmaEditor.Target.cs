// Copyright 2026 Sanjyot Dahale (LifeIsARepo). All rights reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UE_GemmaEditorTarget : TargetRules
{
	public UE_GemmaEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;

		ExtraModuleNames.AddRange( new string[] { "UE_Gemma" } );
	}
}
