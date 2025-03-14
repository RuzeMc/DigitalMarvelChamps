// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "CoreTypes.h"
#include "Serialization/StructuredArchiveSlotBase.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "UObject/ObjectResource.h"
#include "VVMAbstractVisitor.h"

class FStructuredArchive;

namespace Verse
{
struct VCppClassInfo;

struct FVCellSerializeContext
{
	TMap<VCell*, FPackageIndex> CellToPackageIndex;
	TArray<VCell*> ExportMap;

	struct FBatch
	{
#if !UE_BUILD_SHIPPING
		int Iterations;
#endif
		FPackageIndex RootCell;
		TArray<FPackageIndex> Exports;
		TArray<FPackageIndex> Precreate;
	};

	FBatch BuildBatch(VCell* Root);
};

struct FStructuredArchiveVisitor : FAbstractVisitor
{
	FStructuredArchiveVisitor(FAllocationContext InContext, FVCellSerializeContext* InSerializeContext = nullptr)
		: Context(InContext)
		, SerializeContext(InSerializeContext)
	{
	}

	// Temporary entry point used for UE serialization
	static void Serialize(FStructuredArchiveSlot InSlot, TWriteBarrier<VValue>& InOutValue, FVCellSerializeContext* InSerializeContext = nullptr);
	static void Serialize(FAllocationContext InContext, FStructuredArchiveSlot InSlot, TWriteBarrier<VValue>& InOutValue, FVCellSerializeContext* InSerializeContext = nullptr);

	static void Serialize(FStructuredArchiveSlot InSlot, VRestValue& InOutValue, FVCellSerializeContext* InSerializeContext = nullptr)
	{
		Serialize(InSlot, reinterpret_cast<TWriteBarrier<VValue>&>(InOutValue), InSerializeContext);
	}

	static void Serialize(FAllocationContext InContext, FStructuredArchiveSlot InSlot, VRestValue& InOutValue, FVCellSerializeContext* InSerializeContext = nullptr)
	{
		Serialize(InContext, InSlot, reinterpret_cast<TWriteBarrier<VValue>&>(InOutValue), InSerializeContext);
	}

	// Entry points used primarily for testing
	void Serialize(FStructuredArchiveSlot InSlot, VCell*& InOutCell);
	void Serialize(FStructuredArchiveSlot InSlot, VValue& InOutValue);

	virtual void BeginArray(const TCHAR* ElementName, uint64& NumElements) override;
	virtual void EndArray() override;
	virtual void BeginSet(const TCHAR* ElementName, uint64& NumElements) override;
	virtual void EndSet() override;
	virtual void BeginMap(const TCHAR* ElementName, uint64& NumElements) override;
	virtual void EndMap() override;
	virtual void VisitObject(const TCHAR* ElementName, FUtf8StringView TypeName, TFunctionRef<void()>) override;
	virtual void VisitNonNull(VCell*& InCell, const TCHAR* ElementName) override;
	virtual void VisitEmergentType(const VEmergentType* InEmergentType) override;
	virtual void VisitNonNull(UObject*& InObject, const TCHAR* ElementName) override;
	virtual void Visit(VCell*& InCell, const TCHAR* ElementName) override;
	virtual void Visit(UObject*& InObject, const TCHAR* ElementName) override;
	virtual void Visit(VValue& Value, const TCHAR* ElementName) override;
	virtual void Visit(VRestValue& Value, const TCHAR* ElementName) override;
	virtual void Visit(bool& bValue, const TCHAR* ElementName) override;
	virtual void Visit(FString& Value, const TCHAR* ElementName) override;
	virtual void Visit(uint64& Value, const TCHAR* ElementName) override;
	virtual void Visit(int64& Value, const TCHAR* ElementName) override;
	virtual void Visit(uint32& Value, const TCHAR* ElementName) override;
	virtual void Visit(int32& Value, const TCHAR* ElementName) override;
	virtual void Visit(uint16& Value, const TCHAR* ElementName) override;
	virtual void Visit(int16& Value, const TCHAR* ElementName) override;
	virtual void Visit(uint8& Value, const TCHAR* ElementName) override;
	virtual void Visit(int8& Value, const TCHAR* ElementName) override;
	virtual void VisitBulkData(void* Data, uint64 DataSize, const TCHAR* ElementName) override;

	virtual FArchive* GetUnderlyingArchive() override;
	virtual bool IsLoading() override;
	virtual bool IsTextFormat() override;
	virtual FAccessContext GetLoadingContext() override;

private:
	enum class ENestingType : uint8
	{
		None,
		Object,
		Array,
		Set,
		Map,
	};

	enum class EEncodedType : uint8
	{
		None,
		Null,
		True,
		False,
		Int,
		Float,
		Char,
		Char32,
		Cell, // The name will follow this value
		Batch,
		CellIndex,
	};

	struct FEncodedType
	{
		explicit FEncodedType(EEncodedType InEncodedType, const VCppClassInfo* InCppClassInfo = nullptr)
			: EncodedType(InEncodedType)
			, CppClassInfo(InCppClassInfo)
		{
		}
		EEncodedType EncodedType;
		const VCppClassInfo* CppClassInfo;
	};

	// Read/Write the element type description
	void WriteElementType(FStructuredArchiveRecord Record, FEncodedType EncodedType);
	FEncodedType ReadElementType(FStructuredArchiveRecord Record);

	// Enter and leaving objects or arrays
	FStructuredArchiveArray EnterArray(const TCHAR* ElementName, int32& Num, ENestingType Type);
	void LeaveArray(ENestingType Type);
	FStructuredArchiveRecord EnterObject(const TCHAR* ElementName);
	void LeaveObject();
	FStructuredArchiveSlot Slot(const TCHAR* ElementName);

	// Read/Write a cell while handling null, true, and false types
	void WriteCellBodyInternal(FStructuredArchiveRecord Record, VCell* InCell);
	void WriteCellBody(FStructuredArchiveRecord Record, VCell* InCell);
	void ReadCellBodyInternal(FStructuredArchiveRecord Record, FEncodedType EncodedType, VCell*& InOutCell);
	VCell* ReadCellBody(FStructuredArchiveRecord Record, FEncodedType EncodedType);
	void VisitCellBody(FStructuredArchiveRecord Record, VCell*& InOutCell);

	// Read/Write a value
	VValue FollowPlaceholder(VValue InValue);
	void WriteValueBody(FStructuredArchiveRecord Record, VValue InValue);
	VValue ReadValueBody(FStructuredArchiveRecord Record, FEncodedType EncodedType);
	void VisitValueBody(FStructuredArchiveRecord Record, VValue& InOutValue);

	struct NestingEntry
	{
		UE::StructuredArchive::Private::FSlotBase Slot;
		ENestingType Type;
	};
	TArray<NestingEntry> NestingInfo;
	FAllocationContext Context;
	FVCellSerializeContext* SerializeContext = nullptr;
	bool bIsInBatch = false;

	struct ScopedRecord
	{
		ScopedRecord(FStructuredArchiveVisitor& InVisitor, const TCHAR* InName);
		~ScopedRecord();

		FStructuredArchiveVisitor& Visitor;
		const TCHAR* Name;
		FStructuredArchiveRecord Record;
	};
};

} // namespace Verse

#endif // WITH_VERSE_VM
