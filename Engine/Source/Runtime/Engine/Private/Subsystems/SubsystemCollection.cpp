// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/SubsystemCollection.h"

#include "Subsystems/Subsystem.h"
#include "UObject/Interface.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY(LogSubsystemCollection);

/** FSubsystemModuleWatcher class to hide the implementation of keeping the DynamicSystemModuleMap up to date*/
class FSubsystemModuleWatcher
{
public:
	static void OnModulesChanged(FName ModuleThatChanged, EModuleChangeReason ReasonForChange);

	/** Init / Deinit the Module watcher, this tracks module startup and shutdown to ensure only the appropriate dynamic subsystems are instantiated */
	static void InitializeModuleWatcher();
	static void DeinitializeModuleWatcher();

private:
	static void AddClassesForModule(const FName& InModuleName);
	static void RemoveClassesForModule(const FName& InModuleName);

	static FDelegateHandle ModulesChangedHandle;
};

// globals without thread protection must be accessed only from GameThread
FDelegateHandle FSubsystemModuleWatcher::ModulesChangedHandle;
static TArray<FSubsystemCollectionBase*> GlobalSubsystemCollections;
static TMap<FName, TArray<TSubclassOf<UDynamicSubsystem>>> GlobalDynamicSystemModuleMap;

FSubsystemCollectionBase::FSubsystemCollectionBase()
	: Outer(nullptr)
	, bPopulating(false)
{
}

FSubsystemCollectionBase::FSubsystemCollectionBase(UClass* InBaseType)
	: BaseType(InBaseType)
	, Outer(nullptr)
	, bPopulating(false)
{
	check(BaseType);
}

USubsystem* FSubsystemCollectionBase::GetSubsystemInternal(UClass* SubsystemClass) const
{
	// It does not make sense to get a subsystem by null class.
	if (!ensure(SubsystemClass))
	{
		return nullptr;
	}
	
#if WITH_EDITOR && UE_BUILD_SHIPPING
	TStringBuilder<200> DebugSubsystemClassName;
	if (SubsystemClass && IsEngineExitRequested())
	{
		SubsystemClass->GetFName().AppendString(DebugSubsystemClassName);
	}
#endif

	USubsystem* SystemPtr = SubsystemMap.FindRef(SubsystemClass);

	if (SystemPtr)
	{
		return SystemPtr;
	}
	else
	{
		const FSubsystemCollectionBase::FSubsystemArray& SystemPtrs = FindAndPopulateSubsystemArrayInternal(SubsystemClass);
		if (SystemPtrs.Subsystems.Num() > 0)
		{
			return SystemPtrs.Subsystems[0];
		}
	}

	return nullptr;
}

const TArray<USubsystem*>& FSubsystemCollectionBase::GetSubsystemArrayInternal(UClass* SubsystemClass) const
{
	FSubsystemArray& List = FindAndPopulateSubsystemArrayInternal(SubsystemClass);
	return List.Subsystems;
}

TArray<USubsystem*> FSubsystemCollectionBase::GetSubsystemArrayCopy(UClass* SubsystemClass) const
{
	FSubsystemArray& List = FindAndPopulateSubsystemArrayInternal(SubsystemClass);
	return List.Subsystems;
}

void FSubsystemCollectionBase::ForEachSubsystem(TFunctionRef<void(USubsystem*)> Operation) const
{
	TGuardValue<bool> Guard{bIterating, true};
	for (auto It = SubsystemMap.CreateConstIterator(); It; ++It)
	{
		Operation(It->Value);
	}	
}

FSubsystemCollectionBase::FSubsystemArray& FSubsystemCollectionBase::FindAndPopulateSubsystemArrayInternal(UClass* SubsystemClass) const
{
	const bool bIsInterface = SubsystemClass->IsChildOf<UInterface>();
	if (!SubsystemArrayMap.Contains(SubsystemClass))
	{
		TUniquePtr<FSubsystemArray>& NewList = SubsystemArrayMap.Emplace(SubsystemClass, MakeUnique<FSubsystemArray>());
		for (auto Iter = SubsystemMap.CreateConstIterator(); Iter; ++Iter)
		{
			UClass* KeyClass = Iter.Key();
			if ((!bIsInterface && KeyClass->IsChildOf(SubsystemClass)) || 
				(bIsInterface && KeyClass->ImplementsInterface(SubsystemClass)))
			{
				NewList->Subsystems.Add(Iter.Value());
			}
		}
		return *NewList;
	}

	const TUniquePtr<FSubsystemArray>& List = SubsystemArrayMap.FindChecked(SubsystemClass);
	return *List;
}

void FSubsystemCollectionBase::ForEachSubsystemOfClass(UClass* SubsystemClass, TFunctionRef<void(USubsystem*)> Operation) const
{
	if (SubsystemClass == nullptr)
	{
		SubsystemClass = USubsystem::StaticClass();
	}

	const FSubsystemArray& List = FindAndPopulateSubsystemArrayInternal(SubsystemClass);

	TGuardValue<bool> IterationGuard{List.bIsIterating, true};
	for (int32 i=0; i < List.Subsystems.Num(); ++i)
	{
		Operation(List.Subsystems[i]);
	}
}

void FSubsystemCollectionBase::Initialize(UObject* NewOuter)
{
	if (Outer != nullptr)
	{
		// already initialized
		return;
	}

	Outer = NewOuter;
	check(Outer);
	if (ensure(BaseType) && ensureMsgf(SubsystemMap.Num() == 0, TEXT("Currently don't support repopulation of Subsystem Collections.")))
	{
		check(!bPopulating); //Populating collections on multiple threads?
		
		//non-thread-safe use of Global lists, must be from GameThread:
		check(IsInGameThread());

		if (GlobalSubsystemCollections.Num() == 0)
		{
			FSubsystemModuleWatcher::InitializeModuleWatcher();
		}

		UE_LOG(LogSubsystemCollection, Verbose, TEXT("Initializing subsystem collection for %s with type %s"), *GetNameSafe(NewOuter), *GetNameSafe(BaseType));
		
		TGuardValue<bool> PopulatingGuard(bPopulating, true);

		if (BaseType->IsChildOf(UDynamicSubsystem::StaticClass()))
		{
			for (const TPair<FName, TArray<TSubclassOf<UDynamicSubsystem>>>& SubsystemClasses : GlobalDynamicSystemModuleMap)
			{
				for (const TSubclassOf<UDynamicSubsystem>& SubsystemClass : SubsystemClasses.Value)
				{
					if (SubsystemClass->IsChildOf(BaseType))
					{
						AddAndInitializeSubsystem(SubsystemClass);
					}
				}
			}
		}
		else
		{
			TArray<UClass*> SubsystemClasses;
			GetDerivedClasses(BaseType, SubsystemClasses, true);

			for (UClass* SubsystemClass : SubsystemClasses)
			{
				AddAndInitializeSubsystem(SubsystemClass);
			}
		}

		// Statically track collections
		GlobalSubsystemCollections.Add(this);
	}
}

FSubsystemCollectionBase::~FSubsystemCollectionBase()
{
	// Deinitialize should have been called before reaching GC object destruction phase
	//  fix users that failed to call it!
	// 
	// TEMP disabled check so we can run without errors for now
	// @todo fix the underlying issue and turn this check back on
	//checkf( Outer == nullptr , TEXT("FSubsystemCollectionBase destructor called before Deinitialize!\n") );

	// ensure that it is called even if client didn't
	//	otherwise a deleted pointer is left in GlobalSubsystemCollections
	Deinitialize();
}

void FSubsystemCollectionBase::Deinitialize()
{
	//non-thread-safe use of Global lists, must be from GameThread:
	check(IsInGameThread());
	check(!bIterating);

	// already Deinitialize'd :
	if (Outer == nullptr)
	{
		return;
	}
	
	// Remove static tracking 
	GlobalSubsystemCollections.Remove(this);
	if (GlobalSubsystemCollections.IsEmpty())
	{
		FSubsystemModuleWatcher::DeinitializeModuleWatcher();
	}
	
	// Check not iterating any lists
	for (TPair<UClass*, TUniquePtr<FSubsystemArray>>& Pair : SubsystemArrayMap)
	{
		UE_CLOG(Pair.Value->bIsIterating, LogSubsystemCollection, Fatal, TEXT("FSubsystemCollectionBase::Deinitialize called while iterating subsystems of type %s"), *Pair.Key->GetPathName());
	}

	// Deinit and clean up existing systems
	for (auto Iter = SubsystemMap.CreateIterator(); Iter; ++Iter)
	{
		UClass* KeyClass = Iter.Key();
		USubsystem* Subsystem = Iter.Value();
		if (Subsystem != nullptr && Subsystem->GetClass() == KeyClass)
		{
			Subsystem->Deinitialize();
			Subsystem->InternalOwningSubsystem = nullptr;
		}
	}

	SubsystemArrayMap.Empty();
	SubsystemMap.Empty();
	Outer = nullptr;
}

USubsystem* FSubsystemCollectionBase::InitializeDependency(TSubclassOf<USubsystem> SubsystemClass)
{
	USubsystem* Subsystem = nullptr;
	if (ensureMsgf(SubsystemClass, TEXT("Attempting to add invalid subsystem as dependancy.")))
	{
		UE_LOG(LogSubsystemCollection, VeryVerbose, TEXT("Attempting to initialize subsystem dependency (%s)"), *SubsystemClass->GetName());

		if (ensureMsgf(bPopulating, TEXT("InitializeDependancy() should only be called from System USubsystem::Initialization() implementations."))
			&& ensureMsgf(SubsystemClass->IsChildOf(BaseType), TEXT("ClassType (%s) must be a subclass of BaseType(%s)."), *SubsystemClass->GetName(), *BaseType->GetName()))
		{
			Subsystem = AddAndInitializeSubsystem(SubsystemClass);
		}

		UE_CLOG(!Subsystem, LogSubsystemCollection, Log, TEXT("Failed to initialize subsystem dependency (%s)"), *SubsystemClass->GetName());
	}

	return Subsystem;
}

void FSubsystemCollectionBase::AddReferencedObjects(UObject* Referencer, FReferenceCollector& Collector)
{
	Collector.AddStableReferenceMap(SubsystemMap);
}

USubsystem* FSubsystemCollectionBase::AddAndInitializeSubsystem(UClass* SubsystemClass)
{
	TGuardValue<bool> PopulatingGuard(bPopulating, true);

	if (!SubsystemMap.Contains(SubsystemClass))
	{
// AS FIX (LV): Don't create subsystems of old reloaded classes
		// Only add instances for non abstract Subsystems
		if (SubsystemClass && !SubsystemClass->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists))
// END AS FIX
		{
			// Catch any attempt to add a subsystem of the wrong type
			checkf(SubsystemClass->IsChildOf(BaseType), TEXT("ClassType (%s) must be a subclass of BaseType(%s)."), *SubsystemClass->GetName(), *BaseType->GetName());

			// Do not create instances of classes that aren't authoritative.
			if (SubsystemClass->GetAuthoritativeClass() != SubsystemClass)
			{	
				return nullptr;
			}

			UE_SCOPED_ENGINE_ACTIVITY(TEXT("Initializing Subsystem %s"), *SubsystemClass->GetName());

			const USubsystem* CDO = SubsystemClass->GetDefaultObject<USubsystem>();
			if (CDO->ShouldCreateSubsystem(Outer))
			{
				USubsystem* Subsystem = NewObject<USubsystem>(Outer, SubsystemClass);
				SubsystemMap.Add(SubsystemClass,Subsystem);
// AS FIX(LV): Reset the subsystems list cache so it gets regenerated
				SubsystemArrayMap.Empty();
// END AS FIX
				Subsystem->InternalOwningSubsystem = this;
				Subsystem->Initialize(*this);
				
				// Add this new subsystem to any existing maps of base classes to lists of subsystems
				// Not calling FatalErrorIfIteratingSubsystems because adding to the end of the array is safe for index-based iteration
				for (TPair<UClass*, TUniquePtr<FSubsystemArray>>& Pair : SubsystemArrayMap)
				{
					const bool bIsInterface = Pair.Key->IsChildOf<UInterface>();
					if ((!bIsInterface && SubsystemClass->IsChildOf(Pair.Key)) || 
						(bIsInterface && SubsystemClass->ImplementsInterface(Pair.Key)))
					{
						Pair.Value->Subsystems.Add(Subsystem);
					}
				}

				return Subsystem;
			}

			UE_LOG(LogSubsystemCollection, VeryVerbose, TEXT("Subsystem does not exist, but CDO choose to not create (%s)"), *SubsystemClass->GetName());
		}
		return nullptr;
	}

	UE_LOG(LogSubsystemCollection, VeryVerbose, TEXT("Subsystem already exists (%s)"), *SubsystemClass->GetName());
	return SubsystemMap.FindRef(SubsystemClass);
}

void FSubsystemCollectionBase::RemoveAndDeinitializeSubsystem(USubsystem* Subsystem)
{
	check(Subsystem);
	check(!bIterating);
	USubsystem* SubsystemFound = SubsystemMap.FindAndRemoveChecked(Subsystem->GetClass());
	check(Subsystem == SubsystemFound);

	const UClass* SubsystemClass = Subsystem->GetClass();

	for (TPair<UClass*, TUniquePtr<FSubsystemArray>>& Pair : SubsystemArrayMap)
	{
		const bool bIsInterface = Pair.Key->IsChildOf<UInterface>();
		if ((!bIsInterface && SubsystemClass->IsChildOf(Pair.Key)) || 
			(bIsInterface && SubsystemClass->ImplementsInterface(Pair.Key)))
		{
			UE_CLOG(Pair.Value->bIsIterating, LogSubsystemCollection, Fatal, TEXT("Attempted to deinitialize subsystem %s while iterating subsystems of type %s"),
				*Subsystem->GetPathName(),
				*Pair.Key->GetPathName());
			Pair.Value->Subsystems.Remove(Subsystem);
		}
	}

	Subsystem->Deinitialize();
	Subsystem->InternalOwningSubsystem = nullptr;
}

void FSubsystemCollectionBase::ActivateExternalSubsystem(UClass* SubsystemClass)
{
	AddAllInstances(SubsystemClass);
}

void FSubsystemCollectionBase::DeactivateExternalSubsystem(UClass* SubsystemClass)
{
	RemoveAllInstances(SubsystemClass);
}

void FSubsystemCollectionBase::AddAllInstances(UClass* SubsystemClass)
{
	//non-thread-safe use of Global lists, must be from GameThread:
	check(IsInGameThread());

	for (FSubsystemCollectionBase* SubsystemCollection : GlobalSubsystemCollections)
	{
		if (SubsystemClass->IsChildOf(SubsystemCollection->BaseType))
		{
			SubsystemCollection->AddAndInitializeSubsystem(SubsystemClass);
		}
	}
}

void FSubsystemCollectionBase::RemoveAllInstances(UClass* SubsystemClass)
{
	TArray<UObject*> SubsystemsToRemove;
	GetObjectsOfClass(SubsystemClass, SubsystemsToRemove);

	for(UObject* SubsystemObj : SubsystemsToRemove)
	{
		USubsystem* Subsystem = CastChecked<USubsystem>(SubsystemObj);

		if (Subsystem->InternalOwningSubsystem)
		{
			Subsystem->InternalOwningSubsystem->RemoveAndDeinitializeSubsystem(Subsystem);
		}
	}
}




/** FSubsystemModuleWatcher Implementations */
void FSubsystemModuleWatcher::OnModulesChanged(FName ModuleThatChanged, EModuleChangeReason ReasonForChange)
{

	switch (ReasonForChange)
	{
	case EModuleChangeReason::ModuleLoaded:
		AddClassesForModule(ModuleThatChanged);
		break;

	case EModuleChangeReason::ModuleUnloaded:
		RemoveClassesForModule(ModuleThatChanged);
		break;
	}
}


void FSubsystemModuleWatcher::InitializeModuleWatcher()
{
	//non-thread-safe use of Global lists, must be from GameThread:
	check(IsInGameThread());

	check(!ModulesChangedHandle.IsValid());

	// Add Loaded Modules
	TArray<UClass*> SubsystemClasses;
	GetDerivedClasses(UDynamicSubsystem::StaticClass(), SubsystemClasses, true);

	for (UClass* SubsystemClass : SubsystemClasses)
	{
		if (!SubsystemClass->HasAllClassFlags(CLASS_Abstract))
		{
			UPackage* const ClassPackage = SubsystemClass->GetOuterUPackage();
			if (ClassPackage)
			{
				const FName ModuleName = FPackageName::GetShortFName(ClassPackage->GetFName());
				if (FModuleManager::Get().IsModuleLoaded(ModuleName))
				{
					TArray<TSubclassOf<UDynamicSubsystem>>& ModuleSubsystemClasses = GlobalDynamicSystemModuleMap.FindOrAdd(ModuleName);
					ModuleSubsystemClasses.Add(SubsystemClass);
				}
			}
		}
	}

	ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddStatic(&FSubsystemModuleWatcher::OnModulesChanged);
}

void FSubsystemModuleWatcher::DeinitializeModuleWatcher()
{
	if (ModulesChangedHandle.IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(ModulesChangedHandle);
		ModulesChangedHandle.Reset();
	}
}

void FSubsystemModuleWatcher::AddClassesForModule(const FName& InModuleName)
{
	//non-thread-safe use of Global lists, must be from GameThread:
	check(IsInGameThread());

	check(! GlobalDynamicSystemModuleMap.Contains(InModuleName));

	// Find the class package for this module
	const UPackage* const ClassPackage = FindPackage(nullptr, *(FString("/Script/") + InModuleName.ToString()));
	if (!ClassPackage)
	{
		return;
	}

	TArray<TSubclassOf<UDynamicSubsystem>> SubsystemClasses;
	TArray<UObject*> PackageObjects;
	GetObjectsWithPackage(ClassPackage, PackageObjects, false);
	for (UObject* Object : PackageObjects)
	{
		UClass* const CurrentClass = Cast<UClass>(Object);
		if (CurrentClass && !CurrentClass->HasAllClassFlags(CLASS_Abstract) && CurrentClass->IsChildOf(UDynamicSubsystem::StaticClass()))
		{
			SubsystemClasses.Add(CurrentClass);
			FSubsystemCollectionBase::AddAllInstances(CurrentClass);
		}
	}
	if (SubsystemClasses.Num() > 0)
	{
		GlobalDynamicSystemModuleMap.Add(InModuleName, MoveTemp(SubsystemClasses));
	}
}
void FSubsystemModuleWatcher::RemoveClassesForModule(const FName& InModuleName)
{
	//non-thread-safe use of Global lists, must be from GameThread:
	check(IsInGameThread());

	TArray<TSubclassOf<UDynamicSubsystem>>* SubsystemClasses = GlobalDynamicSystemModuleMap.Find(InModuleName);
	if (SubsystemClasses)
	{
		for (TSubclassOf<UDynamicSubsystem>& SubsystemClass : *SubsystemClasses)
		{
			FSubsystemCollectionBase::RemoveAllInstances(SubsystemClass);
		}
		GlobalDynamicSystemModuleMap.Remove(InModuleName);
	}
}
