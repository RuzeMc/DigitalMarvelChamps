// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class FActorModifierCoreEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

protected:
	void RegisterDetailCustomizations() const;
	void UnregisterDetailCustomizations() const;

	void RegisterBlueprintCustomizations();
};