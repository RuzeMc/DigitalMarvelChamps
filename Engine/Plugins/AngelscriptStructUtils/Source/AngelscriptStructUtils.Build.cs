using System.IO;
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class AngelscriptStructUtils : ModuleRules
	{
		public AngelscriptStructUtils(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(new string[]
			{
				"AngelscriptCode",

				"StructUtils"
			});

			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"CoreUObject"
			});
		}
	}
}
