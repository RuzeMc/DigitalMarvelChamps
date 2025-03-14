// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HarmonixMetasoundTests : ModuleRules
{
	public HarmonixMetasoundTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"FunctionalTesting",
				"UnrealEd",
				"AudioExtensions",
				"MetasoundEngine",
				"MetasoundEngineTest",
				"MetasoundFrontend",
				"MetasoundGenerator",
				"MetasoundGraphCore",
				"MetasoundStandardNodes",
				"HarmonixDsp",
				"HarmonixMetasound",
				"HarmonixMidi",
				"Projects",
				"SignalProcessing"
			}
		);
	}
}
