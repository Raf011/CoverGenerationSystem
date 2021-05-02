// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CoverSystem : ModuleRules
{
	public CoverSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay" });
	}
}
