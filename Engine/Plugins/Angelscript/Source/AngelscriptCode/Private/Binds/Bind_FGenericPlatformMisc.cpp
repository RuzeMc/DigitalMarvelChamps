#include "AngelscriptManager.h"
#include "AngelscriptType.h"
#include "AngelscriptBinds.h"

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FGenericPlatformMisc((int32)FAngelscriptBinds::EOrder::Late, []
{
	{
		FAngelscriptBinds::FNamespace ns("FGenericPlatformMisc");
		FAngelscriptBinds::BindGlobalFunction("void RequestExit(bool Force)", [](bool Force)
		{
			FGenericPlatformMisc::RequestExit(Force, nullptr);
		});
		FAngelscriptBinds::BindGlobalFunction("void RequestExit(bool Force, const FString& CallSite)", [](bool Force, const FString& CallSite)
		{
			FGenericPlatformMisc::RequestExit(Force, *CallSite);
		});
	}
});
