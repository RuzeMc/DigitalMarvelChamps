// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CoreNative.h: Native function lookup table.
=============================================================================*/

#pragma once

#include "HAL/Platform.h"
#include "UObject/Object.h"
#include "UObject/Script.h"

class UObject;
struct FFrame;

// HAZE FIX(LV): Expose type-erased method pointers to native C++ methods
/*
* These are type-erasure templates for method pointer-to-member function pointers.
* This code is literally copied in from angelscript.h and renamed so unreal can
* generate the code needed to save these, so they can be bound to angelscript.
*/
class FUnknownClass;
typedef void (FUnknownClass::*FTypeErasedMethodPtr)();

typedef void (*FTypeErasedFuncPtr)();

struct FGenericFuncPtr
{
	constexpr FGenericFuncPtr()
		: ptr{}
		, flag{ 0 }
	{
	}

	template <typename FuncType>
	constexpr FGenericFuncPtr(FuncType func)
		: ptr{ (void*)func }
		, flag{ 2 }
	{
	}

	template <typename MethodType>
	FGenericFuncPtr(uint8 f, MethodType method)
		: flag{ f }
	{
		static_assert(sizeof(method) <= sizeof(FTypeErasedMethodPtr), "");
		for (size_t n = 0; n < sizeof(ptr.dummy); n++)
			ptr.dummy[n] = 0;
		memcpy(ptr.dummy, &method, sizeof(MethodType));
	}

	inline constexpr bool IsBound() const
	{
		return flag != 0;
	}

private:
	union func_t
	{
		// The largest known method point is 20 bytes (MSVC 64bit),
		// but with 8 byte alignment this becomes 24 bytes. So we need
		// to be able to store at least that much.
		void* raw;
		char dummy[25];

		constexpr func_t() : dummy{} {}
		constexpr func_t(void* ptr) : raw{ptr} {}
	};
	
	func_t ptr;
	uint8 flag; // 1 = generic, 2 = global func, 3 = method
};

#define ERASE_ARGUMENT_PACK(...) __VA_ARGS__
#define ERASE_NO_FUNCTION() FGenericFuncPtr{}, ASAutoCaller::FunctionCaller{}
#define ERASE_METHOD_PTR(c,m,p,r) FGenericFuncPtr(3, static_cast<r(c::*)p>(&c::m)), ASAutoCaller::MakeFunctionCaller(static_cast<r(c::*)p>(&c::m))
#define ERASE_FUNCTION_PTR(f,p,r) FGenericFuncPtr(static_cast<r(*)p>(f)), ASAutoCaller::MakeFunctionCaller(static_cast<r(*)p>(f))

namespace ASAutoCaller
{
	class FFakeObject;
	using TFunctionPtr = void(*)();
	using TMethodPtr = void(FFakeObject::*)();

	struct FunctionCaller
	{
		using FunctionCallerPtr = void(*)(TFunctionPtr Method, void** Parameters, void* ReturnValue);
		using MethodCallerPtr = void(*)(TMethodPtr Function, void** Parameters, void* ReturnValue);

		union
		{
			MethodCallerPtr MethodPtr;
			FunctionCallerPtr FuncPtr;
		};
		int type;

		constexpr FunctionCaller()
			: FuncPtr{ nullptr }
			, type{ 0 }
		{
		}

		static constexpr FunctionCaller Make()
		{
			FunctionCaller Caller;
			Caller.FuncPtr = nullptr;
			Caller.type = 0;
			return Caller;
		}

		static constexpr FunctionCaller Make(FunctionCallerPtr InFunctionCaller)
		{
			FunctionCaller Caller;
			Caller.FuncPtr = InFunctionCaller;
			Caller.type = 1;
			return Caller;
		}

		static constexpr FunctionCaller Make(MethodCallerPtr InMethodCaller)
		{
			FunctionCaller Caller;
			Caller.MethodPtr = InMethodCaller;
			Caller.type = 2;
			return Caller;
		}

		constexpr bool IsBound() const { return MethodPtr != nullptr; }
	};

	template <typename T> struct TReferenceToPtr { typedef T Type; };
	template <typename T> struct TReferenceToPtr<T& > { typedef T* Type; };
	template <typename T> struct TReferenceToPtr<T&&> { typedef T* Type; };

	template<typename T>
	FORCEINLINE typename TEnableIf<TIsReferenceType<T>::Value, typename TRemoveReference<T>::Type*>::Type GetAddressIfReference(const T& Value)
	{
		return (typename TRemoveReference<T>::Type*) & Value;
	}

	template<typename T>
	FORCEINLINE typename TEnableIf<!TIsReferenceType<T>::Value, const T&>::Type GetAddressIfReference(const T& Value)
	{
		return Value;
	}

	template<typename T>
	FORCEINLINE typename TEnableIf<TIsPointer<T>::Value, T>::Type PassArgument(void* Value)
	{
		return *(T*)Value;
	}

	template<typename T>
	FORCEINLINE typename TEnableIf<!TIsPointer<T>::Value, typename TRemoveReference<T>::Type&>::Type PassArgument(void* Value)
	{
		return **(typename TRemoveReference<T>::Type**)Value;
	}

	template<typename ReturnType, typename... ParamTypes, int... TIndices>
	FORCEINLINE void IndexedFunctionCaller(TFunctionPtr FunctionPtr, void** Arguments, void* ReturnValue, TIntegerSequence<int, TIndices...>)
	{
		union
		{
			TFunctionPtr Function;
			ReturnType(*Casted)(ParamTypes...);
		} CastedFunctionPtr;
		CastedFunctionPtr.Casted = nullptr;
		CastedFunctionPtr.Function = FunctionPtr;
		new(ReturnValue) (typename TReferenceToPtr<ReturnType>::Type)(
			GetAddressIfReference<ReturnType>(
				(CastedFunctionPtr.Casted)(
					(PassArgument<ParamTypes>(Arguments + TIndices))...
					)
				)
			);
	}

	template<typename ReturnType, typename... ParamTypes>
	typename TEnableIf<!std::is_void<ReturnType>::value>::Type RedirectFunctionCaller(TFunctionPtr FunctionPtr, void** Arguments, void* ReturnValue)
	{
		IndexedFunctionCaller<ReturnType, ParamTypes...>(FunctionPtr, Arguments, ReturnValue, TMakeIntegerSequence<int, sizeof...(ParamTypes)>());
	}

	template<typename... ParamTypes, int... TIndices>
	FORCEINLINE void IndexedFunctionCallerVoid(TFunctionPtr FunctionPtr, void** Arguments, TIntegerSequence<int, TIndices...>)
	{
		union
		{
			TFunctionPtr Function;
			void(*Casted)(ParamTypes...);
		} CastedFunctionPtr;
		CastedFunctionPtr.Casted = nullptr;
		CastedFunctionPtr.Function = FunctionPtr;
		(CastedFunctionPtr.Casted)(
			(PassArgument<ParamTypes>(Arguments + TIndices))...
		);
	}

	template<typename ReturnType, typename... ParamTypes>
	typename TEnableIf<std::is_void<ReturnType>::value>::Type RedirectFunctionCaller(TFunctionPtr FunctionPtr, void** Arguments, void* ReturnValue)
	{
		IndexedFunctionCallerVoid<ParamTypes...>(FunctionPtr, Arguments, TMakeIntegerSequence<int, sizeof...(ParamTypes)>());
	}

	template<typename ReturnType, typename ObjectType, typename... ParamTypes, int... TIndices>
	FORCEINLINE void IndexedMethodCaller(TMethodPtr MethodPtr, void** Arguments, void* ReturnValue, TIntegerSequence<int, TIndices...>)
	{
		union
		{
			TMethodPtr Method;
			ReturnType(ObjectType::*Casted)(ParamTypes...);
		} CastedMethodPtr;
		CastedMethodPtr.Casted = nullptr;
		CastedMethodPtr.Method = MethodPtr;
		new(ReturnValue) (typename TReferenceToPtr<ReturnType>::Type)(
			GetAddressIfReference<ReturnType>(
				(((ObjectType*)Arguments[0])->*CastedMethodPtr.Casted)
					(
					(PassArgument<ParamTypes>(Arguments + TIndices + 1))...
					)
				)
			);
	}

	template<typename ReturnType, typename ObjectType, typename... ParamTypes>
	typename TEnableIf<!std::is_void<ReturnType>::value>::Type RedirectMethodCaller(TMethodPtr MethodPtr, void** Arguments, void* ReturnValue)
	{
		IndexedMethodCaller<ReturnType, ObjectType, ParamTypes...>(MethodPtr, Arguments, ReturnValue, TMakeIntegerSequence<int, sizeof...(ParamTypes)>());
	}

	template<typename ObjectType, typename... ParamTypes, int... TIndices>
	FORCEINLINE void IndexedMethodCallerVoid(TMethodPtr MethodPtr, void** Arguments, TIntegerSequence<int, TIndices...>)
	{
		union
		{
			TMethodPtr Method;
			void(ObjectType::*Casted)(ParamTypes...);
		} CastedMethodPtr;
		CastedMethodPtr.Casted = nullptr;
		CastedMethodPtr.Method = MethodPtr;
		(((ObjectType*)Arguments[0])->*CastedMethodPtr.Casted)(
			(PassArgument<ParamTypes>(Arguments + TIndices + 1))...
		);
	}

	template<typename ReturnType, typename ObjectType, typename... ParamTypes>
	typename TEnableIf<std::is_void<ReturnType>::value>::Type RedirectMethodCaller(TMethodPtr MethodPtr, void** Arguments, void* ReturnValue)
	{
		IndexedMethodCallerVoid<ObjectType, ParamTypes...>(MethodPtr, Arguments, TMakeIntegerSequence<int, sizeof...(ParamTypes)>());
	}

	template<typename ReturnType, typename... ParamTypes>
	constexpr FunctionCaller MakeFunctionCaller(ReturnType(*FunctionPtr)(ParamTypes...))
	{
		return FunctionCaller::Make(&RedirectFunctionCaller<ReturnType, ParamTypes...>);
	}

	template<typename ReturnType, typename... ParamTypes, typename ObjectType>
	constexpr FunctionCaller MakeFunctionCaller(ReturnType(ObjectType::*FunctionPtr)(ParamTypes...))
	{
		return FunctionCaller::Make(&RedirectMethodCaller<ReturnType, ObjectType, ParamTypes...>);
	}

	template<typename ReturnType, typename... ParamTypes, typename ObjectType>
	constexpr FunctionCaller MakeFunctionCaller(ReturnType(ObjectType::*FunctionPtr)(ParamTypes...) const)
	{
		return FunctionCaller::Make(&RedirectMethodCaller<ReturnType, ObjectType, ParamTypes...>);
	}

	template<typename ReturnType, typename... ParamTypes, typename ObjectType>
	constexpr FunctionCaller MakeFunctionCaller(ReturnType(ObjectType::*FunctionPtr)(ParamTypes...) const&)
	{
		return FunctionCaller::Make(&RedirectMethodCaller<ReturnType, ObjectType, ParamTypes...>);
	}
}
// END HAZE FIX

/** The type of a native function callable by script */
typedef void (*FNativeFuncPtr)(UObject* Context, FFrame& TheStack, RESULT_DECL);

// This class is deliberately simple (i.e. POD) to keep generated code size down.
struct FNameNativePtrPair
{
	const char* NameUTF8;
	FNativeFuncPtr Pointer;
// AS FIX(LV): Expose extra function pointers to be used by angelscript
	FGenericFuncPtr DirectGenericPtr;
	ASAutoCaller::FunctionCaller Caller;
// END AS FIX
};

/** A struct that maps a string name to a native function */
struct FNativeFunctionRegistrar
{
	FNativeFunctionRegistrar(class UClass* Class, const ANSICHAR* InName, FNativeFuncPtr InPointer)
	{
		RegisterFunction(Class, InName, InPointer);
	}
	static COREUOBJECT_API void RegisterFunction(class UClass* Class, const ANSICHAR* InName, FNativeFuncPtr InPointer);
	// overload for types generated from blueprints, which can have unicode names:
	static COREUOBJECT_API void RegisterFunction(class UClass* Class, const WIDECHAR* InName, FNativeFuncPtr InPointer);

	static COREUOBJECT_API void RegisterFunctions(class UClass* Class, const FNameNativePtrPair* InArray, int32 NumFunctions);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif