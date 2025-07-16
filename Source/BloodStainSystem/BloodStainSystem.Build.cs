/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

using UnrealBuildTool;

public class BloodStainSystem : ModuleRules
{
	public BloodStainSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
		});
			
		
		PublicDependencyModuleNames.AddRange(new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore", 
				"UMG",
				"NetCore"
		});
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
		);
	
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AnimGraph", 
					"BlueprintGraph",
					"UnrealEd", // 명시적으로 추가하지 않아도 AnimGraph/BlueprintGraph가 참조할 수 있습니다.
					"KismetCompiler", // 명시적으로 추가하지 않아도 AnimGraph/BlueprintGraph가 참조할 수 있습니다.
					"GraphEditor",
				}
			);
		}
	
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
}
