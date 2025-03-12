#include "StructUtils/InstancedStruct.h"

#include "AngelscriptDocs.h"
#include "AngelscriptBinds.h"
#include "AngelscriptManager.h"

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FInstancedStruct(
	FAngelscriptBinds::EOrder::Late,
	[]
	{
		auto FInstancedStruct_ = FAngelscriptBinds::ExistingClass("FInstancedStruct");

		FInstancedStruct_.Method("bool opEquals(const FInstancedStruct& Other) const", METHODPR(bool, FInstancedStruct, operator==, (const FInstancedStruct&) const));
		SCRIPT_BIND_DOCUMENTATION("Comparison operators. Deep compares the struct instance when identical.")

		FInstancedStruct_.Method(
			"void InitializeAs(const ?&in Struct)",
			[](FInstancedStruct* Self, const uint8* Data, const int TypeId)
			{
				const UStruct* StructDef = FAngelscriptManager::Get().GetUnrealStructFromAngelscriptTypeId(TypeId);
				if (StructDef == nullptr)
				{
					FAngelscriptManager::Throw("Not a valid USTRUCT");
					return;
				}

				const UScriptStruct* ScriptStructDef = Cast<UScriptStruct>(StructDef);
				if (ScriptStructDef == nullptr)
				{
					FAngelscriptManager::Throw("Not a valid UScriptStruct");
					return;
				}

				Self->InitializeAs(ScriptStructDef, Data);
			});
		SCRIPT_BIND_DOCUMENTATION("Initializes from struct type and emplace construct.")

		FInstancedStruct_.Method(
			"void Get(?&out Struct) const",
			[](const FInstancedStruct* Self, uint8* Data, int TypeId)
			{
				if (!Self->IsValid())
				{
					FAngelscriptManager::Throw("Source is empty or not valid. Check `IsValid()` before trying to `Get()` the underlying struct.");
					return;
				}

				const UStruct* StructDef = FAngelscriptManager::Get().GetUnrealStructFromAngelscriptTypeId(TypeId);
				if (StructDef == nullptr)
				{
					FAngelscriptManager::Throw("Not a valid USTRUCT");
					return;
				}

				const UScriptStruct* ScriptStructDef = Cast<UScriptStruct>(StructDef);
				if (ScriptStructDef == nullptr)
				{
					FAngelscriptManager::Throw("Not a valid UScriptStruct");
					return;
				}

				if (ScriptStructDef != Self->GetScriptStruct())
				{
					const FString Debug =
						FString::Printf(TEXT("\nMismatching types. Got %s but expected %s."), *ScriptStructDef->GetStructCPPName(), *Self->GetScriptStruct()->GetStructCPPName());
					FAngelscriptManager::Throw(TCHAR_TO_ANSI(*Debug));
					return;
				}

				// This is making a copy, which we'd prefer to avoid, but the alternative is very complicated (see TOptional)
				ScriptStructDef->CopyScriptStruct(Data, Self->GetMemory());
			});
		SCRIPT_BIND_DOCUMENTATION("Returns a copy of the struct. This getter assumes that all data is valid.");

		FInstancedStruct_.Method("void Reset()", METHOD(FInstancedStruct, Reset));

		FInstancedStruct_.Method("bool IsValid() const", METHOD_TRIVIAL(FInstancedStruct, IsValid));
		SCRIPT_BIND_DOCUMENTATION("Returns True if the struct is valid.")

		{
			FAngelscriptBinds::FNamespace ns("FInstancedStruct");

			FAngelscriptBinds::BindGlobalFunction(
				"FInstancedStruct Make(const ?&in Struct)",
				[](const uint8* Data, const int TypeId) -> FInstancedStruct
				{
					const UStruct* StructDef = FAngelscriptManager::Get().GetUnrealStructFromAngelscriptTypeId(TypeId);
					if (StructDef == nullptr)
					{
						FAngelscriptManager::Throw("Not a valid USTRUCT");
						return FInstancedStruct();
					}

					const UScriptStruct* ScriptStructDef = Cast<UScriptStruct>(StructDef);
					if (ScriptStructDef == nullptr)
					{
						FAngelscriptManager::Throw("Not a valid UScriptStruct");
						return FInstancedStruct();
					}

					FInstancedStruct InstancedStruct;
					InstancedStruct.InitializeAs(ScriptStructDef, Data);
					return InstancedStruct;
				});
			SCRIPT_BIND_DOCUMENTATION("Creates a new FInstancedStruct from struct.")
		}
	});
