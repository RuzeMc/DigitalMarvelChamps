// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AutoRTFMEngineTests : ModuleRules
{
	public AutoRTFMEngineTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");
		PrivateIncludePathModuleNames.Add("DerivedDataCache");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"AutomationController",
				"AutomationWorker",
				"Core",
				"Projects",
				"Engine",
				"HeadMountedDisplay",
				"InstallBundleManager",
				"MediaUtils",
				"MRMesh",
				"MoviePlayer",
				"MoviePlayerProxy",
				"MovieScene",
				"PreLoadScreen",
				"SessionServices",
				"SlateNullRenderer",
				"SlateRHIRenderer",
				"ProfileVisualizer",
			}
		);
	}
}
