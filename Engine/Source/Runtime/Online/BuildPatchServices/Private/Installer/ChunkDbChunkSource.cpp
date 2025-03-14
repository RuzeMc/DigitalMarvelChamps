// Copyright Epic Games, Inc. All Rights Reserved.

#include "Installer/ChunkDbChunkSource.h"
#include "Misc/Guid.h"
#include "Async/Async.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "Algo/Transform.h"
#include "Core/Platform.h"
#include "Common/FileSystem.h"
#include "Common/StatsCollector.h"
#include "Data/ChunkData.h"
#include "Installer/ChunkStore.h"
#include "Installer/ChunkReferenceTracker.h"
#include "Installer/MessagePump.h"
#include "Installer/InstallerError.h"
#include "Installer/InstallerSharedContext.h"
#include "Misc/OutputDeviceRedirector.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
namespace ChunkDbSourceHelpers
{
	int32 DisableOsIntervention()
	{
		return ::SetErrorMode(SEM_FAILCRITICALERRORS);
	}

	void ResetOsIntervention(int32 Previous)
	{
		::SetErrorMode(Previous);
	}
}
#else
namespace ChunkDbSourceHelpers
{
	int32 DisableOsIntervention()
	{
		return 0;
	}

	void ResetOsIntervention(int32 Previous)
	{
	}
}
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogChunkDbChunkSource, Log, All);
DEFINE_LOG_CATEGORY(LogChunkDbChunkSource);

namespace ChunkDbSourceHelpers
{
	using namespace BuildPatchServices;
	IChunkDbChunkSourceStat::ELoadResult FromSerializer(EChunkLoadResult LoadResult)
	{
		switch (LoadResult)
		{
			case EChunkLoadResult::Success:
				return IChunkDbChunkSourceStat::ELoadResult::Success;
			case EChunkLoadResult::MissingHashInfo:
				return IChunkDbChunkSourceStat::ELoadResult::MissingHashInfo;
			case EChunkLoadResult::HashCheckFailed:
				return IChunkDbChunkSourceStat::ELoadResult::HashCheckFailed;
			default:
				return IChunkDbChunkSourceStat::ELoadResult::SerializationError;
		}
	}
}

namespace BuildPatchServices
{
	/**
	 * Struct holding variables for accessing a chunkdb file's data.
	 */
	struct FChunkDbDataAccess
	{
		FChunkDatabaseHeader Header;
		TUniquePtr<FArchive> Archive;
		FString ChunkDbFileName;

		// When the reference tracker gets below this watermark, then we know we are done with this file and we can
		// close/retire it.
		int32 RetireAt = 0;

		// If we're retired then any access is invalid and fatal as the file has been closed and could be deleted.
		bool bIsRetired = false;

		void Retire(IMessagePump* MessagePump, IFileSystem* FileSystemIfDeleting, bool bDelete)
		{
			bIsRetired = true;
			FString ArchiveName = Archive->GetArchiveName();
			Archive.Reset();
			MessagePump->SendMessage(FChunkSourceEvent{ FChunkSourceEvent::EType::Retired, ArchiveName });

			if (bDelete && FileSystemIfDeleting)
			{
				if (!FileSystemIfDeleting->DeleteFile(*ChunkDbFileName))
				{
					GLog->Logf(TEXT("Failed to delete chunkdb upon retirement: %s"), *ChunkDbFileName);
				}
			}
		}
	};

	/**
	 * Struct holding variables required for accessing a particular chunk.
	 */
	struct FChunkAccessLookup
	{
		FChunkLocation* Location;
		FChunkDbDataAccess* DbFile;
	};

	/**
	 * Struct holding variables for tracking retry attempts on opening chunkdb files.
	 */
	struct FChunkDbRetryInfo
	{
		int32 Count;
		double SecondsAtLastTry;
	};

	/**
	 * The concrete implementation of IChunkDbChunkSource.
	 */
	class FChunkDbChunkSource
		: public IChunkDbChunkSource
	{
	public:
		FChunkDbChunkSource(FChunkDbSourceConfig Configuration, IPlatform* Platform, IFileSystem* FileSystem, IChunkStore* ChunkStore, IChunkReferenceTracker* ChunkReferenceTracker, IChunkDataSerialization* ChunkDataSerialization, IMessagePump* MessagePump, IInstallerError* InstallerError, IChunkDbChunkSourceStat* ChunkDbChunkSourceStat);
		~FChunkDbChunkSource();

		// IControllable interface begin.
		virtual void SetPaused(bool bInIsPaused) override;
		virtual void Abort() override;
		// IControllable interface end.

		// IChunkSource interface begin.
		virtual IChunkDataAccess* Get(const FGuid& DataId) override;
		virtual TSet<FGuid> AddRuntimeRequirements(TSet<FGuid> NewRequirements) override;
		virtual bool AddRepeatRequirement(const FGuid& RepeatRequirement) override;
		virtual void SetUnavailableChunksCallback(TFunction<void(TSet<FGuid>)> Callback) override;
		virtual void ReportFileCompletion() override
		{
			//
			// Since we've completed a file we know we won't need to resume/retry it and can delete
			// the source chunkdb that it used.
			//
			int32 RemainingChunkCount = ChunkReferenceTracker->GetRemainingChunkCount();
			for (FChunkDbDataAccess& ChunkDbDataAccess : ChunkDbDataAccesses)
			{
				if (!ChunkDbDataAccess.bIsRetired &&
					ChunkDbDataAccess.RetireAt > RemainingChunkCount)
				{
					ChunkDbDataAccess.Retire(MessagePump, FileSystem, Configuration.bDeleteChunkDBAfterUse);
				}
			}
		}
		// IChunkSource interface end.

		// IInstallChunkSource interface begin.
		virtual const TSet<FGuid>& GetAvailableChunks() const override;
		virtual uint64 GetChunkDbSizesAtIndexes(const TArray<int32>& FileCompletionIndexes, TArray<uint64>& OutChunkDbSizesAtCompletion) const override;
		// IInstallChunkSource interface end.

		static void LoadChunkDbFiles(
			const TArray<FString>& ChunkDbFiles, IFileSystem* FileSystem, const TArray<FGuid>& ChunkAccessOrderedList,
			TArray<FChunkDbDataAccess>& OutChunkDbDataAccesses, TMap<FGuid, FChunkAccessLookup>& OutChunkDbDataAccessLookup, TSet<FGuid>* OutOptionalAvailableStore);

	private:
		void ThreadRun();
		void LoadChunk(const FGuid& DataId);
		bool HasFailed(const FGuid& DataId);

		typedef TFunction<bool(const FArchive&)> FArchiveSelector;
		typedef TFunction<void(const FArchive&, bool)> FArchiveOpenResult;
		void TryReopenChunkDbFiles(const FArchiveSelector& SelectPredicate, const FArchiveOpenResult& ResultCallback);

	private:
		// Configuration.
		const FChunkDbSourceConfig Configuration;
		// Dependencies.
		IPlatform* Platform = nullptr;
		IFileSystem* FileSystem = nullptr;
		IChunkStore* ChunkStore = nullptr;
		IChunkReferenceTracker* ChunkReferenceTracker = nullptr;
		IChunkDataSerialization* ChunkDataSerialization = nullptr;
		IMessagePump* MessagePump = nullptr;
		IInstallerError* InstallerError = nullptr;
		IChunkDbChunkSourceStat* ChunkDbChunkSourceStat;
		// Worker thread lifetime management
		TPromise<void> Promise;
		TFuture<void> Future;
		IBuildInstallerThread* Thread = nullptr;
		// Control signals.
		std::atomic<bool> bIsPaused = false;
		std::atomic<bool> bShouldAbort = false;
		std::atomic<bool> bStartedLoading = false;
		// Handling of chunks we lose access to.
		TFunction<void(TSet<FGuid>)> UnavailableChunksCallback;
		TSet<FGuid> UnavailableChunks;
		// Storage of our chunkdb and enumerated available chunks lookup.
		TArray<FChunkDbDataAccess> ChunkDbDataAccesses;
		TMap<FGuid, FChunkAccessLookup> ChunkDbDataAccessLookup;
		TMap<FString, FChunkDbRetryInfo> ChunkDbReloadAttempts;
		TSet<FGuid> AvailableChunks;
		TSet<FGuid> PlacedInStore;
		// Communication between the incoming Get calls, and our worker thread.
		TQueue<FGuid, EQueueMode::Spsc> FailedToLoadMessages;
		TSet<FGuid> FailedToLoad;
		// Communication between the incoming AddRepeatRequirement calls, and our worker thread.
		TQueue<FGuid, EQueueMode::Mpsc> RepeatRequirementMessages;

		// Number of chunks to process in this manifest when we started.
		int32 OriginalChunkCount = 0;
	};

	// Read in the headers, evalutate the list of chunks, and determine when we'll be done with our chunk dbs.
	void FChunkDbChunkSource::LoadChunkDbFiles(
		const TArray<FString>& ChunkDbFiles, IFileSystem* FileSystem, const TArray<FGuid>& ChunkAccessOrderedList, 
		TArray<FChunkDbDataAccess>& OutChunkDbDataAccesses, TMap<FGuid, FChunkAccessLookup>& OutChunkDbDataAccessLookup, TSet<FGuid>* OutOptionalAvailableStore)
	{

		// Allow OS intervention only once.
		bool bResetOsIntervention = false;
		int32 PreviousOsIntervention = 0;
		// Load each chunkdb's TOC to enumerate available chunks.
		for (const FString& ChunkDbFilename : ChunkDbFiles)
		{
			TUniquePtr<FArchive> ChunkDbFile(FileSystem->CreateFileReader(*ChunkDbFilename));
			if (ChunkDbFile.IsValid())
			{
				// Load header.
				FChunkDatabaseHeader Header;
				*ChunkDbFile << Header;
				if (ChunkDbFile->IsError())
				{
					GLog->Logf(TEXT("Failed to load chunkdb header for %s"), *ChunkDbFilename);
				}
				else if (Header.Contents.Num() == 0)
				{
					GLog->Logf(TEXT("Loaded empty chunkdb %s"), *ChunkDbFilename);
				}
				else
				{
					// Hold on to the handle and header info.
					FChunkDbDataAccess DataSource;
					DataSource.Archive = MoveTemp(ChunkDbFile);
					DataSource.ChunkDbFileName = ChunkDbFilename;
					DataSource.Header = MoveTemp(Header);
					OutChunkDbDataAccesses.Add(MoveTemp(DataSource));
				}
			}
			else if (!bResetOsIntervention)
			{
				bResetOsIntervention = true;
				PreviousOsIntervention = ChunkDbSourceHelpers::DisableOsIntervention();
			}
		}
		// Reset OS intervention if we disabled it.
		if (bResetOsIntervention)
		{
			ChunkDbSourceHelpers::ResetOsIntervention(PreviousOsIntervention);
		}

		// Index all chunks to their location info.
		for (FChunkDbDataAccess& ChunkDbDataAccess : OutChunkDbDataAccesses)
		{
			for (FChunkLocation& ChunkLocation : ChunkDbDataAccess.Header.Contents)
			{
				if (!OutChunkDbDataAccessLookup.Contains(ChunkLocation.ChunkId))
				{
					OutChunkDbDataAccessLookup.Add(ChunkLocation.ChunkId, { &ChunkLocation, &ChunkDbDataAccess });

					if (OutOptionalAvailableStore)
					{
						OutOptionalAvailableStore->Add(ChunkLocation.ChunkId);
					}
				}
			}
		}

		TMap<FString, int32> FileLastSeenAt;

		int32 GuidIndex = 0;
		for (; GuidIndex < ChunkAccessOrderedList.Num(); GuidIndex++)
		{
			const FGuid& Guid = ChunkAccessOrderedList[GuidIndex];

			FChunkAccessLookup* SourceForGuid = OutChunkDbDataAccessLookup.Find(Guid);
			if (!SourceForGuid)
			{
				continue;
			}

			FileLastSeenAt.FindOrAdd(SourceForGuid->DbFile->Archive->GetArchiveName()) = GuidIndex;
		}

		for (FChunkDbDataAccess& ChunkDbDataAccess : OutChunkDbDataAccesses)
		{
			// Default to retired immediately (this is functionally after the first file completes)
			ChunkDbDataAccess.RetireAt = ChunkAccessOrderedList.Num();
			int32* LastAt = FileLastSeenAt.Find(ChunkDbDataAccess.Archive->GetArchiveName());
			if (LastAt)
			{
				// The reference stack gets popped rather than advanced, so we need to reverse the ordering.
				// LastAt is the chunk index that last used the file, which means when we are at
				// LastAt + 1 through the stack we can delete the file.
				ChunkDbDataAccess.RetireAt = ChunkAccessOrderedList.Num() - (*LastAt + 1);
			}
		}
	}
	
	// Get how many bytes of chunkdbs will still exist on disk after the given indexes (i.e. will not have been retired)
	// Return is the total size of given chunkdbs.
	static uint64 GetChunkDbSizesAtIndexesInternal(const TArray<FChunkDbDataAccess>& InOpenedChunkDbs, int32 InOriginalChunkCount, const TArray<int32>& InFileCompletionIndexes, TArray<uint64>& OutChunkDbSizesAtCompletion)
	{
		uint64 AllChunkDbSize = 0;
		for (const FChunkDbDataAccess& ChunkFile : InOpenedChunkDbs)
		{
			if (!ChunkFile.bIsRetired)
			{
				AllChunkDbSize += ChunkFile.Archive->TotalSize();
			}
		}
		
		// Go over the list of completions and evalute how many chunk dbs are left over.
		for (int32 FileCompletionIndex : InFileCompletionIndexes)
		{
			// retiring happens as the list is _popped_, so everything is backwards.
			int32 RetireAtEquivalent = InOriginalChunkCount - FileCompletionIndex;

			uint64 TotalSizeAtIndex = 0;
			for (const FChunkDbDataAccess& ChunkFile : InOpenedChunkDbs)
			{
				if (!ChunkFile.bIsRetired &&
					ChunkFile.RetireAt < RetireAtEquivalent)
				{
					TotalSizeAtIndex += ChunkFile.Archive->TotalSize();
				}
			}

			OutChunkDbSizesAtCompletion.Add(TotalSizeAtIndex);
		}
		return AllChunkDbSize;
	}


	uint64 FChunkDbChunkSource::GetChunkDbSizesAtIndexes(const TArray<int32>& FileCompletionIndexes, TArray<uint64>& OutChunkDbSizesAtCompletion) const
	{
		return GetChunkDbSizesAtIndexesInternal(ChunkDbDataAccesses, OriginalChunkCount, FileCompletionIndexes, OutChunkDbSizesAtCompletion);
	}

	uint64 IChunkDbChunkSource::GetChunkDbSizesAtIndexes(const TArray<FString>& ChunkDbFiles, IFileSystem* FileSystem, const TArray<FGuid>& ChunkAccessOrderedList, const TArray<int32>& FileCompletionIndexes, TArray<uint64>& OutChunkDbSizesAtCompletion)
	{
		TArray<FChunkDbDataAccess> ChunkFiles;
		TMap<FGuid, FChunkAccessLookup> ChunkGuidLookup;

		FChunkDbChunkSource::LoadChunkDbFiles(ChunkDbFiles, FileSystem, ChunkAccessOrderedList, ChunkFiles, ChunkGuidLookup, nullptr);

		return GetChunkDbSizesAtIndexesInternal(ChunkFiles, ChunkAccessOrderedList.Num(), FileCompletionIndexes, OutChunkDbSizesAtCompletion);
	}

	FChunkDbChunkSource::FChunkDbChunkSource(FChunkDbSourceConfig InConfiguration, IPlatform* InPlatform, IFileSystem* InFileSystem, IChunkStore* InChunkStore, IChunkReferenceTracker* InChunkReferenceTracker, IChunkDataSerialization* InChunkDataSerialization, IMessagePump* InMessagePump, IInstallerError* InInstallerError, IChunkDbChunkSourceStat* InChunkDbChunkSourceStat)
		: Configuration(MoveTemp(InConfiguration))
		, Platform(InPlatform)
		, FileSystem(InFileSystem)
		, ChunkStore(InChunkStore)
		, ChunkReferenceTracker(InChunkReferenceTracker)
		, ChunkDataSerialization(InChunkDataSerialization)
		, MessagePump(InMessagePump)
		, InstallerError(InInstallerError)
		, ChunkDbChunkSourceStat(InChunkDbChunkSourceStat)
		, bStartedLoading(!InConfiguration.bBeginLoadsOnFirstGet)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FChunkDbChunkSource_ctor);

		TArray<FGuid> OrderedGuidList;
		InChunkReferenceTracker->CopyOutOrderedUseList(OrderedGuidList);

		OriginalChunkCount = OrderedGuidList.Num();

		LoadChunkDbFiles(Configuration.ChunkDbFiles, FileSystem, OrderedGuidList, ChunkDbDataAccesses, ChunkDbDataAccessLookup, &AvailableChunks);

		// Immediately retire any chunkdbs we don't need so they don't eat disk space during the first file.
		FChunkDbChunkSource::ReportFileCompletion();

		// Start threaded load worker.
		if (ChunkDbDataAccesses.Num() > 0)
		{
			Future = Promise.GetFuture();
			Thread = Configuration.SharedContext->CreateThread();
			Thread->RunTask([this]() { ThreadRun(); });
		}
	}

	FChunkDbChunkSource::~FChunkDbChunkSource()
	{
		Abort();

		if (!Thread)
		{
			// The thread was never started, so complete the promise here
			Promise.SetValue();
		}

		Future.Wait();
		Configuration.SharedContext->ReleaseThread(Thread);
		Thread = nullptr;
	}

	void FChunkDbChunkSource::SetPaused(bool bInIsPaused)
	{
		bIsPaused = bInIsPaused;
	}

	void FChunkDbChunkSource::Abort()
	{
		bShouldAbort = true;
	}

	IChunkDataAccess* FChunkDbChunkSource::Get(const FGuid& DataId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ChunkDb_Get);

		// Get from our store
		IChunkDataAccess* ChunkData = ChunkStore->Get(DataId);
		if (ChunkData == nullptr)
		{
			bStartedLoading = true;
			FChunkAccessLookup* ChunkInfo = ChunkDbDataAccessLookup.Find(DataId);
			if (ChunkInfo && AvailableChunks.Contains(DataId))
			{
				if (ChunkInfo->DbFile->bIsRetired)
				{
					GLog->Logf(TEXT("BAD!!!!!!!! Accessed chunk after chunkdb %s retired - invalid chunk reference order."), *ChunkInfo->DbFile->Archive->GetArchiveName());
				}

				// Wait for the chunk to be available.
				while (!HasFailed(DataId) && (ChunkData = ChunkStore->Get(DataId)) == nullptr && !bShouldAbort)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(ChunkDb_GetChunkData_Sleep);
					// 10ms was wayyy too long here. (def needs to be an event)
					Platform->Sleep(0.001f);
				}

				// Dump out unavailable chunks on the incoming IO thread.
				if (UnavailableChunksCallback && UnavailableChunks.Num() > 0)
				{
					UnavailableChunksCallback(MoveTemp(UnavailableChunks));
				}
			}
		}
		return ChunkData;
	}

	TSet<FGuid> FChunkDbChunkSource::AddRuntimeRequirements(TSet<FGuid> NewRequirements)
	{
		// We can't actually get more than we are already getting, so just return what we don't already have in our list.
		return NewRequirements.Difference(AvailableChunks);
	}

	bool FChunkDbChunkSource::AddRepeatRequirement(const FGuid& RepeatRequirement)
	{
		if (AvailableChunks.Contains(RepeatRequirement))
		{
			RepeatRequirementMessages.Enqueue(RepeatRequirement);
			return true;
		}
		return false;
	}

	void FChunkDbChunkSource::SetUnavailableChunksCallback(TFunction<void(TSet<FGuid>)> Callback)
	{
		UnavailableChunksCallback = Callback;
	}

	const TSet<FGuid>& FChunkDbChunkSource::GetAvailableChunks() const
	{
		return AvailableChunks;
	}

	void FChunkDbChunkSource::ThreadRun()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ChunkDb_Thread);

		while (!bShouldAbort)
		{
			bool bWorkPerformed = false;
			if (bStartedLoading)
			{
				// 'Forget' any repeat requirements.
				FGuid RepeatRequirement;
				while (RepeatRequirementMessages.Dequeue(RepeatRequirement))
				{
					PlacedInStore.Remove(RepeatRequirement);
				}
				// Select chunks that are contained in our chunk db files.
				TFunction<bool(const FGuid&)> SelectPredicate = [this](const FGuid& ChunkId) { return AvailableChunks.Contains(ChunkId); };
				// Grab all the chunks relevant to this source to fill the store.
				int32 SearchLength = FMath::Max(ChunkStore->GetSize(), Configuration.PreFetchMinimum);
				TArray<FGuid> BatchLoadChunks = ChunkReferenceTracker->SelectFromNextReferences(SearchLength, SelectPredicate);
				// Remove already loaded chunks from our todo list.
				// We only grab more chunks as they come into scope.
				TFunction<bool(const FGuid&)> RemovePredicate = [this](const FGuid& ChunkId) { return PlacedInStore.Contains(ChunkId); };
				BatchLoadChunks.RemoveAll(RemovePredicate);
				// Clamp to configured max.
				BatchLoadChunks.SetNum(FMath::Min(BatchLoadChunks.Num(), Configuration.PreFetchMaximum), EAllowShrinking::No);
				// Load this batch.
				ChunkDbChunkSourceStat->OnBatchStarted(BatchLoadChunks);
				for (int32 ChunkIdx = 0; ChunkIdx < BatchLoadChunks.Num() && !bShouldAbort; ++ChunkIdx)
				{
					const FGuid& BatchLoadChunk = BatchLoadChunks[ChunkIdx];
					LoadChunk(BatchLoadChunk);
				}

				// Set whether we performed work.
				if (BatchLoadChunks.Num() > 0 && !bShouldAbort)
				{
					bWorkPerformed = true;
				}
			}
			// If we had nothing to do, rest a little.
			if(!bWorkPerformed)
			{
				// This sleep is hit during startup since we don't do work before we start loading. For fresh installs
				// this means we usually ask for the first chunk immediately after this sleep starts. So I changed this from 100ms
				// to 10ms. Ideally this would be waiting on an event...
				Platform->Sleep(0.01f);
			}
		}

		Promise.SetValue();
	}

	void FChunkDbChunkSource::LoadChunk(const FGuid& DataId)
	{
		bool bChunkGood = false;
		if (ChunkDbDataAccessLookup.Contains(DataId))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ChunkDb_LoadChunk);
			ChunkDbChunkSourceStat->OnLoadStarted(DataId);
			FChunkAccessLookup& ChunkAccessLookup = ChunkDbDataAccessLookup[DataId];
			FChunkLocation& ChunkLocation = *ChunkAccessLookup.Location;
			FChunkDbDataAccess& ChunkDbDataAccess = *ChunkAccessLookup.DbFile;
			FArchive* ChunkDbFile = ChunkDbDataAccess.Archive.Get();
			check(ChunkDbFile != nullptr); // We never set null.
			bChunkGood = !ChunkDbFile->IsError();
			if (!bChunkGood)
			{
				const double SecondsNow = FStatsCollector::GetSeconds();
				// Time to retry?
				FChunkDbRetryInfo& ChunkDbRetryInfo = ChunkDbReloadAttempts.FindOrAdd(ChunkDbFile->GetArchiveName());
				const bool bIsFirstFail = ChunkDbRetryInfo.Count == 0;
				if (bIsFirstFail)
				{
					MessagePump->SendMessage(FChunkSourceEvent{FChunkSourceEvent::EType::AccessLost, ChunkDbFile->GetArchiveName()});
					// Also try reopening any chunkdbs files that have no error yet, in case they will also be lost.
					// This gives us control over disabling OS intervention popups when we inevitably try to read from them later.
					int32 PreviousOsIntervention = ChunkDbSourceHelpers::DisableOsIntervention();
					TryReopenChunkDbFiles(
						[this](const FArchive& Ar) -> bool
						{
							return Ar.IsError() == false;
						},
						[this](const FArchive& Ar, bool bSuccess) -> void
						{
							if (!bSuccess)
							{
								// Send a message about losing this chunkdb.
								MessagePump->SendMessage(FChunkSourceEvent{FChunkSourceEvent::EType::AccessLost, Ar.GetArchiveName()});
								ChunkDbReloadAttempts.FindOrAdd(Ar.GetArchiveName()).Count = 1;
							}
						});
					// Reset OS intervention setting.
					ChunkDbSourceHelpers::ResetOsIntervention(PreviousOsIntervention);
				}
				if (bIsFirstFail || (SecondsNow - ChunkDbRetryInfo.SecondsAtLastTry) >= Configuration.ChunkDbOpenRetryTime)
				{
					UE_LOG(LogChunkDbChunkSource, Log, TEXT("Retrying ChunkDb archive which has errored %s"), *ChunkDbFile->GetArchiveName());
					ChunkDbRetryInfo.SecondsAtLastTry = SecondsNow;
					// Retry whilst disabling OS intervention. Any OS dialogs if they exist would have been popped during the read that flagged the error.
					int32 PreviousOsIntervention = ChunkDbSourceHelpers::DisableOsIntervention();
					TUniquePtr<FArchive> NewChunkDbFile(FileSystem->CreateFileReader(*ChunkDbFile->GetArchiveName()));
					if (NewChunkDbFile.IsValid())
					{
						ChunkDbDataAccess.Archive = MoveTemp(NewChunkDbFile);
						ChunkDbFile = ChunkDbDataAccess.Archive.Get();
						bChunkGood = true;
						ChunkDbRetryInfo.Count = 0;
						MessagePump->SendMessage(FChunkSourceEvent{FChunkSourceEvent::EType::AccessRegained, ChunkDbFile->GetArchiveName()});
						// Attempt to regain access to other failed chunkdb files too.
						TryReopenChunkDbFiles(
							[this](const FArchive& Ar) -> bool
							{
								return Ar.IsError() == true;
							},
							[this](const FArchive& Ar, bool bSuccess) -> void
							{
								if (bSuccess)
								{
									// Send a message about regaining this chunkdb.
									MessagePump->SendMessage(FChunkSourceEvent{FChunkSourceEvent::EType::AccessRegained, Ar.GetArchiveName()});
									ChunkDbReloadAttempts.FindOrAdd(Ar.GetArchiveName()).Count = 0;
								}
							});
					}
					else
					{
						++ChunkDbRetryInfo.Count;
					}
					// Reset OS intervention setting.
					ChunkDbSourceHelpers::ResetOsIntervention(PreviousOsIntervention);
				}
			}
			if (bChunkGood)
			{
				// Load the chunk.
				bChunkGood = false;
				const int64 TotalFileSize = ChunkDbFile->TotalSize();
				int64 DataEndPoint = ChunkLocation.ByteStart + ChunkLocation.ByteSize;
				if (TotalFileSize >= DataEndPoint)
				{
					ISpeedRecorder::FRecord ActivityRecord;
					ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
					if (ChunkDbFile->Tell() != ChunkLocation.ByteStart)
					{
						ChunkDbFile->Seek(ChunkLocation.ByteStart);
					}
					EChunkLoadResult LoadResult;
					TUniquePtr<IChunkDataAccess> ChunkDataAccess(ChunkDataSerialization->LoadFromArchive(*ChunkDbFile, LoadResult));
					ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
					const uint64 End = ChunkDbFile->Tell();
					ActivityRecord.Size = End - ChunkLocation.ByteStart;
					bChunkGood = LoadResult == EChunkLoadResult::Success && ChunkDataAccess.IsValid() && (End == DataEndPoint);
					ChunkDbChunkSourceStat->OnLoadComplete(DataId, ChunkDbSourceHelpers::FromSerializer(LoadResult), ActivityRecord);
					if (bChunkGood)
					{
						// Add it to our cache.
						PlacedInStore.Add(DataId);
						ChunkStore->Put(DataId, MoveTemp(ChunkDataAccess));
					}
				}
				else
				{
					ChunkDbChunkSourceStat->OnLoadComplete(DataId, IChunkDbChunkSourceStat::ELoadResult::LocationOutOfBounds, ISpeedRecorder::FRecord());
				}
			}
		}
		if (!bChunkGood)
		{
			FailedToLoadMessages.Enqueue(DataId);
		}
	}

	bool FChunkDbChunkSource::HasFailed(const FGuid& DataId)
	{
		FGuid Temp;
		while (FailedToLoadMessages.Dequeue(Temp))
		{
			FailedToLoad.Add(Temp);
			UnavailableChunks.Add(Temp);
		}
		return FailedToLoad.Contains(DataId);
	}

	void FChunkDbChunkSource::TryReopenChunkDbFiles(const FArchiveSelector& SelectPredicate, const FArchiveOpenResult& ResultCallback)
	{
		for (FChunkDbDataAccess& ChunkDbToCheck : ChunkDbDataAccesses)
		{
			TUniquePtr<FArchive>& ArchiveToCheck = ChunkDbToCheck.Archive;
			if (ArchiveToCheck.IsValid() && SelectPredicate(*ArchiveToCheck.Get()))
			{
				TUniquePtr<FArchive> CanOpenFile(FileSystem->CreateFileReader(*ArchiveToCheck->GetArchiveName()));
				if (CanOpenFile.IsValid())
				{
					// Always set in the case that reopening fixes the currently undetected problem.
					ArchiveToCheck.Reset(CanOpenFile.Release());
					ResultCallback(*ArchiveToCheck.Get(), true);
				}
				else
				{
					// Make sure error is set on the archive so that we know to be retrying.
					ArchiveToCheck->SetError();
					ResultCallback(*ArchiveToCheck.Get(), false);
				}
			}
		}
	}

	IChunkDbChunkSource* FChunkDbChunkSourceFactory::Create(FChunkDbSourceConfig Configuration, IPlatform* Platform, IFileSystem* FileSystem, IChunkStore* ChunkStore, IChunkReferenceTracker* ChunkReferenceTracker, IChunkDataSerialization* ChunkDataSerialization, IMessagePump* MessagePump, IInstallerError* InstallerError, IChunkDbChunkSourceStat* ChunkDbChunkSourceStat)
	{
		UE_LOG(LogChunkDbChunkSource, Verbose, TEXT("FChunkDbChunkSourceFactory::Create for %d db files"), Configuration.ChunkDbFiles.Num());

		check(Platform != nullptr);
		check(FileSystem != nullptr);
		check(ChunkStore != nullptr);
		check(ChunkReferenceTracker != nullptr);
		check(ChunkDataSerialization != nullptr);
		check(MessagePump != nullptr);
		check(InstallerError != nullptr);
		check(ChunkDbChunkSourceStat != nullptr);
		return new FChunkDbChunkSource(MoveTemp(Configuration), Platform, FileSystem, ChunkStore, ChunkReferenceTracker, ChunkDataSerialization, MessagePump, InstallerError, ChunkDbChunkSourceStat);
	}

	const TCHAR* ToString(const IChunkDbChunkSourceStat::ELoadResult& LoadResult)
	{
		switch(LoadResult)
		{
			case IChunkDbChunkSourceStat::ELoadResult::Success:
				return TEXT("Success");
			case IChunkDbChunkSourceStat::ELoadResult::MissingHashInfo:
				return TEXT("MissingHashInfo");
			case IChunkDbChunkSourceStat::ELoadResult::HashCheckFailed:
				return TEXT("HashCheckFailed");
			case IChunkDbChunkSourceStat::ELoadResult::LocationOutOfBounds:
				return TEXT("LocationOutOfBounds");
			case IChunkDbChunkSourceStat::ELoadResult::SerializationError:
				return TEXT("SerializationError");
			default:
				return TEXT("Unknown");
		}
	}
}
