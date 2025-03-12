#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class ANGELSCRIPTEDITOR_API FAngelscriptEditorModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void ShowAssetListPopup(const TArray<FString>& AssetPaths, class UASClass* BaseClass);
	static void ShowCreateBlueprintPopup(class UASClass* Class);

private:
	void ReloadTags();
	void RegisterToolsMenuEntries();
};
