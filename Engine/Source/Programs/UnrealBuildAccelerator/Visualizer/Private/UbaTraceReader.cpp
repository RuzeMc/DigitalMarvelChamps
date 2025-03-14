// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaTraceReader.h"
#include "UbaFile.h"
#include "UbaFileAccessor.h"
#include "UbaNetworkClient.h"
#include "UbaNetworkMessage.h"

namespace uba
{
	const TraceView::Process& TraceView::GetProcess(const ProcessLocation& loc)
	{
		static TraceView::Process emptyProcess;
		if (loc.sessionIndex >= sessions.size())
			return emptyProcess;
		auto& session = sessions[loc.sessionIndex];
		if (loc.processorIndex >= session.processors.size())
			return emptyProcess;
		auto& processor = session.processors[loc.processorIndex];
		if (loc.processIndex >= processor.processes.size())
			return emptyProcess;
		return processor.processes[loc.processIndex];
	}

	const TraceView::Session& TraceView::GetSession(const ProcessLocation& loc)
	{
		if (loc.sessionIndex < sessions.size())
			return sessions[loc.sessionIndex];
		static TraceView::Session emptySession;
		return emptySession;
	}

	void TraceView::Clear()
	{
		sessions.clear();
		workTracks.clear();
		strings.clear();
		statusMap.clear();
		cacheWrites.clear();
		startTime = 0;
		totalProcessActiveCount = 0;
		totalProcessExitedCount = 0;
		activeSessionCount = 0;
		progressProcessesTotal = 0;
		progressProcessesDone = 0;
		progressErrorCount = 0;
		remoteExecutionDisabled = false;
		finished = true;
	};


	TraceReader::TraceReader(Logger& logger)
	:	m_logger(logger)
	,	m_channel(logger)
	{
	}

	TraceReader::~TraceReader()
	{
		Unmap();
	}

#if PLATFORM_WINDOWS

	u64 ConvertTime(const TraceView& view, u64 time)
	{
		return time * GetFrequency() / view.frequency;
	}

	bool TraceReader::ReadFile(TraceView& out, const tchar* fileName, bool replay)
	{
		Reset(out);

		FileHandle readHandle;
		if (!OpenFileSequentialRead(m_logger, fileName, readHandle))
			return false;
		auto closeFile = MakeGuard([&]() { CloseFile(fileName, readHandle); });
		u64 fileSize;
		if (!uba::GetFileSizeEx(fileSize, readHandle))
			return false;

		m_memoryHandle = uba::CreateFileMappingW(readHandle, PAGE_READONLY);
		if (!m_memoryHandle.IsValid())
			return false;
		auto closeFileMapping = MakeGuard([&]() { CloseFileMapping(m_memoryHandle); m_memoryHandle = {}; });

		m_memoryBegin = MapViewOfFile(m_memoryHandle, FILE_MAP_READ, 0, 0);
		if (!m_memoryBegin)
			return false;
		auto closeView = MakeGuard([&]() { UnmapViewOfFile(m_memoryBegin, 0, TC("TraceReader")); m_memoryBegin = nullptr; m_memoryPos = nullptr; m_memoryEnd = nullptr; });

		m_memoryPos = m_memoryBegin;
		m_memoryEnd = m_memoryBegin + fileSize;

		BinaryReader reader(m_memoryBegin);

		u32 traceSize = reader.ReadU32(); (void)traceSize;
		u32 version = reader.ReadU32();
		if (version < TraceReadCompatibilityVersion || version > TraceVersion)
			return m_logger.Error(L"Incompatible trace version (%u). Current executable supports version %u to %u.", version, TraceReadCompatibilityVersion, TraceVersion);

		out.version = version;
		reader.ReadU32(); // ProcessId
		u64 traceSystemStartTimeUs = 0;
		if (version >= 18)
			traceSystemStartTimeUs = reader.Read7BitEncoded();
		if (version >= 18)
			out.frequency = reader.Read7BitEncoded();
		else
			out.frequency = GetFrequency();

		out.realStartTime = reader.Read7BitEncoded();

		out.startTime = out.realStartTime;
		if (replay)
			out.startTime = GetTime();
		else if (traceSystemStartTimeUs)
			out.startTime = GetTime() - UsToTime(GetSystemTimeUs() - traceSystemStartTimeUs);

		m_memoryPos += reader.GetPosition();
		out.finished = false;

		if (replay)
		{
			closeFileMapping.Cancel();
			closeView.Cancel();
			return true;
		}

		while (reader.GetPosition() < fileSize)
			if (!ReadTrace(out, reader, ~u64(0)))
				return false;
		out.finished = true;
		return true;
	}

	bool TraceReader::UpdateReadFile(TraceView& out, u64 maxTime, bool& outChanged)
	{
		BinaryReader traceReader(m_memoryPos);
		u64 left = u64(m_memoryEnd - m_memoryPos);
		while (traceReader.GetPosition() < left)
		{
			u64 pos = traceReader.GetPosition();
			if (!ReadTrace(out, traceReader, maxTime))
				return false;
			if (pos == traceReader.GetPosition())
			{
				if (!traceReader.GetLeft())
					out.finished = true;
				break;
			}
		}
		outChanged = traceReader.GetPosition() != 0 || !m_activeProcesses.empty();
		m_memoryPos += traceReader.GetPosition();
		return true;
	}


	bool TraceReader::StartReadClient(TraceView& out, NetworkClient& client)
	{
		Reset(out);

		u32 traceMemSize = 128 * 1024 * 1024;
		m_memoryHandle = uba::CreateMemoryMappingW(m_logger, PAGE_READWRITE, traceMemSize);
		if (!m_memoryHandle.IsValid())
			return false;
		m_memoryBegin = MapViewOfFile(m_memoryHandle, FILE_MAP_ALL_ACCESS, 0, traceMemSize);
		m_memoryPos = m_memoryBegin;
		m_memoryEnd = m_memoryBegin;

		out.finished = false;
		out.sessions.emplace_back();
		out.sessions.back().name = L"LOCAL";
		bool changed;
		return UpdateReadClient(out, client, changed);
	}

	bool TraceReader::UpdateReadClient(TraceView& out, NetworkClient& client, bool& outChanged)
	{
		outChanged = false;
		if (!m_memoryHandle.IsValid())
			return true;
		while (true)
		{
			u32 pos = u32(m_memoryEnd - m_memoryBegin);

			StackBinaryWriter<32> writer;
			NetworkMessage msg(client, SessionServiceId, SessionMessageType_GetTraceInformation, writer);
			writer.WriteU32(pos);
			StackBinaryReader<SendDefaultSize> reader;
			if (!msg.Send(reader))
			{
				for (auto& session : out.sessions)
					for (auto& processor : session.processors)
					{
						auto& process = processor.processes.back();
						if (process.stop == ~u64(0))
						{
							process.stop = GetTime() - out.startTime;
							process.exitCode = 0; // This is wrong but since we didn't get the final result we can't tell if it was success or not
							process.bitmapDirty = true;
						}
					}
				out.finished = true;
				return false;
			}
			u64 remotePos = reader.ReadU32();
			u32 left = u32(reader.GetLeft());
			reader.ReadBytes(m_memoryEnd, left);
			pos += left;
			m_memoryEnd += left;
			if (remotePos == pos)
				break;
		}
		outChanged = !m_activeProcesses.empty() || m_memoryPos != m_memoryEnd;
		return ReadMemory(out, false, ~u64(0));
	}

	bool TraceReader::StartReadNamed(TraceView& out, const tchar* namedTrace, bool silentFail, bool replay)
	{
		Reset(out);

		if (!namedTrace)
			namedTrace = TC("");

		if (*namedTrace && m_namedTrace != namedTrace)
		{
			m_memoryHandle.handle = ::OpenFileMappingW(PAGE_READWRITE, false, namedTrace);
			if (!m_memoryHandle.IsValid())
			{
				if (!silentFail)
					m_logger.Error(L"OpenFileMappingW - Failed to open file mapping %ls (%ls)", namedTrace, LastErrorToText().data);
				return false;
			}
			m_memoryBegin = MapViewOfFile(m_memoryHandle, FILE_MAP_READ, 0, 0);
			if (!m_memoryBegin)
				return false;
		}

		m_namedTrace = namedTrace;

		m_memoryPos = m_memoryBegin;
		m_memoryEnd = m_memoryBegin;
		out.finished = false;
		out.sessions.emplace_back();
		bool changed;
		return UpdateReadNamed(out, replay ? 0ull : ~u64(0), changed);
	}

	bool TraceReader::UpdateReadNamed(TraceView& out, u64 maxTime, bool& outChanged)
	{
		outChanged = false;
		if (!m_memoryBegin)
			return true;
		m_memoryEnd = m_memoryBegin + *(u32*)m_memoryBegin;
		outChanged = !m_activeProcesses.empty() || m_memoryPos != m_memoryEnd;
		bool res = ReadMemory(out, true, maxTime);
		if (m_hostProcess && WaitForSingleObject(m_hostProcess, 0) != WAIT_TIMEOUT)
			StopAllActive(out, GetTime() - out.realStartTime);

		if (res || m_namedTrace.empty())
			return true;

		// Move memory to local mapping in case we want to replay
		u64 traceMemSize = m_memoryEnd - m_memoryBegin;
		u8* memoryBegin = nullptr;
		FileMappingHandle memoryHandle = uba::CreateMemoryMappingW(m_logger, PAGE_READWRITE, traceMemSize);
		if (memoryHandle.IsValid())
			if ((memoryBegin = MapViewOfFile(memoryHandle, FILE_MAP_ALL_ACCESS, 0, traceMemSize)) != nullptr)
				memcpy(memoryBegin, m_memoryBegin, traceMemSize);

		u64 pos = m_memoryPos - m_memoryBegin;
		u64 end = m_memoryEnd - m_memoryBegin;
		Unmap();
		m_memoryHandle = memoryHandle;
		m_memoryPos = memoryBegin + pos;
		m_memoryEnd = memoryBegin + end;
		m_memoryBegin = memoryBegin;

		return res;
	}

	bool TraceReader::ReadMemory(TraceView& out, bool trackHost, u64 maxTime)
	{
		if (m_memoryEnd == m_memoryPos)
			return true;

		u64 toRead = m_memoryEnd - m_memoryPos;
		BinaryReader traceReader(m_memoryPos);

		if (m_memoryPos == m_memoryBegin)
		{
			if (toRead < 128)
				return true;
			u32 traceSize = traceReader.ReadU32(); (void)traceSize;
			u32 version = traceReader.ReadU32();
			if (version < TraceReadCompatibilityVersion || version > TraceVersion)
				return m_logger.Error(L"Incompatible trace version (%u). Current executable supports version %u to %u.", version, TraceReadCompatibilityVersion, TraceVersion);

			bool replay = maxTime != ~u64(0);

			out.version = version;
			u32 hostProcessId = traceReader.ReadU32();
			m_hostProcess = 0;
			if (trackHost && !replay)
				m_hostProcess = OpenProcess(SYNCHRONIZE, FALSE, hostProcessId);
			u64 traceSystemStartTimeUs = 0;
			if (version >= 18)
				traceSystemStartTimeUs = traceReader.Read7BitEncoded();
			if (version >= 18)
				out.frequency = traceReader.Read7BitEncoded();
			else
				out.frequency = GetFrequency();

			out.realStartTime = traceReader.Read7BitEncoded();
			if (traceSystemStartTimeUs)
				out.realStartTime = GetTime() - UsToTime(GetSystemTimeUs() - traceSystemStartTimeUs);

			out.startTime = out.realStartTime;
			if (replay)
				out.startTime = GetTime();
		}

		u64 lastPos = traceReader.GetPosition();
		while (lastPos != toRead)
		{
			if (!ReadTrace(out, traceReader, maxTime))
				return false;
			u64 pos = traceReader.GetPosition();
			if (pos == lastPos)
				break;
			UBA_ASSERT(pos <= toRead);
			lastPos = pos;
		}
		m_memoryPos += lastPos;
		return true;
	}

	bool TraceReader::ReadTrace(TraceView& out, BinaryReader& reader, u64 maxTime)
	{
		u64 readPos = reader.GetPosition();
		u8 traceType = reader.ReadByte();
		u64 time = 0;
		if (out.version >= 15 && traceType != TraceType_String)
		{
			time = ConvertTime(out, reader.Read7BitEncoded());
			if (time > maxTime)
			{
				reader.SetPosition(readPos);
				return true;
			}
		}

		#if 0
		static const tchar* traceTypeNames[] =
		{
			#define UBA_TRACE_TYPE(name) TC(#name),
			UBA_TRACE_TYPES
			#undef UBA_TRACE_TYPE
		};
		OutputDebugString(StringBuffer<>().Appendf(L"TraceType: %s\r\n", traceTypeNames[traceType]).data);
		#endif

		switch (traceType)
		{
		case TraceType_SessionAdded:
		{
			StringBuffer<128> sessionName;
			reader.ReadString(sessionName);
			StringBuffer<> sessionInfo;
			reader.ReadString(sessionInfo);
			Guid clientUid = ReadClientId(out, reader);
			u32 sessionIndex = reader.ReadU32();

			StringBuffer<> fullName;
			fullName.Append(sessionName).Append(L" (").Append(sessionInfo).Append(L")");

			// Check if we can re-use existing session (same machine was disconnected and then reconnected)
			u32 virtualSessionIndex = sessionIndex;
			for (u32 i = 0; i != out.sessions.size(); ++i)
			{
				auto& oldSession = out.sessions[i];
				if (oldSession.fullName != fullName.data)
					continue;
				if (oldSession.disconnectTime == ~u64(0))
					break;
				oldSession.isReset = true;
				oldSession.disconnectTime = ~u64(0);
				oldSession.proxyName.clear();
				oldSession.proxyCreated = false;
				oldSession.notification.clear();
				//oldSession.fetchedFiles.clear();
				//oldSession.storedFiles.clear();
				virtualSessionIndex = i;
				break;
			}


			if (out.sessions.size() <= virtualSessionIndex)
				out.sessions.resize(virtualSessionIndex + 1);

			if (m_sessionIndexToSession.size() <= sessionIndex)
				m_sessionIndexToSession.resize(sessionIndex + 1);

			m_sessionIndexToSession[sessionIndex] = virtualSessionIndex;

			TraceView::Session& session = GetSession(out, sessionIndex);
			session.name = sessionName.data;
			session.fullName = fullName.data;
			session.clientUid = clientUid;

			++out.activeSessionCount;
			break;
		}
		case TraceType_SessionUpdate:
		{
			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			u32 sessionIndex;
			u8 connectionCount = 0;
			u64 totalSend;
			u64 totalRecv;
			u64 lastPing;

			if (out.version >= 14)
			{
				sessionIndex = u32(reader.Read7BitEncoded());
				connectionCount = u8(reader.Read7BitEncoded());
				totalSend = reader.Read7BitEncoded();
				totalRecv = reader.Read7BitEncoded();
				lastPing = reader.Read7BitEncoded();
			}
			else
			{
				sessionIndex = reader.ReadU32();
				totalSend = reader.ReadU64();
				totalRecv = reader.ReadU64();
				lastPing = reader.Read7BitEncoded();
			}

			u64 memAvail = 0;
			u64 memTotal = 0;
			if (out.version >= 9)
			{
				memAvail = reader.Read7BitEncoded();
				memTotal = reader.Read7BitEncoded();
			}

			float cpuLoad = 0;
			if (out.version >= 13)
			{
				u32 value = reader.ReadU32();
				cpuLoad = *(float*)&value;
			}
			TraceView::Session& session = GetSession(out, sessionIndex);
			if (session.isReset)
			{
				session.isReset = false;
				session.prevUpdateTime = 0;
				session.prevSend = 0;
				session.prevRecv = 0;
				session.memTotal = 0;
				if (!session.updates.empty())
					session.updates.push_back({ time, 0, 0, lastPing, memAvail, cpuLoad, connectionCount });
			}
			else
			{
				auto& prevUpdate = session.updates.back();
				session.prevSend = prevUpdate.send;
				session.prevRecv = prevUpdate.recv;
				session.prevUpdateTime = session.updates.back().time;
			}

			//session.lastPing = lastPing;
			//session.memAvail = memAvail;
			session.memTotal = memTotal;
			session.updates.push_back({time, totalSend, totalRecv, lastPing, memAvail, cpuLoad, connectionCount});
			
			u64 send = totalSend - session.prevSend;
			u64 recv = totalRecv - session.prevRecv;
			session.highestSendPerS = Max(session.highestSendPerS, float(send)/TimeToS(time - session.prevUpdateTime));
			session.highestRecvPerS = Max(session.highestRecvPerS, float(recv)/TimeToS(time - session.prevUpdateTime));
			break;
		}
		case TraceType_SessionDisconnect:
		{
			u32 sessionIndex = reader.ReadU32();
			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			TraceView::Session& session = GetSession(out, sessionIndex);
			session.disconnectTime = time;
			session.maxVisibleFiles = 0;
			for (auto it = session.fetchedFiles.begin(), end = session.fetchedFiles.end(); it != end; ++it)
				if (it->stop == ~u64(0))
					it->stop = time;
			for (auto it = session.storedFiles.begin(), end = session.storedFiles.end(); it != end; ++it)
				if (it->stop == ~u64(0))
					it->stop = time;

			--out.activeSessionCount;
			break;
		}
		case TraceType_SessionNotification:
		{
			u32 sessionIndex = reader.ReadU32();
			TraceView::Session& session = GetSession(out, sessionIndex);
			session.notification = reader.ReadString();
			break;
		}
		case TraceType_SessionSummary:
		{
			u32 sessionIndex = reader.ReadU32();
			u32 lineCount = reader.ReadU32();

			TraceView::Session& session = GetSession(out, sessionIndex);
			session.summary.reserve(lineCount);
			for (u32 i=0; i!=lineCount; ++i)
				session.summary.push_back(reader.ReadString());
			break;
		}
		case TraceType_ProcessAdded:
		{
			u32 sessionIndex = reader.ReadU32();
			u32 id = reader.ReadU32();
			StringBuffer<> desc;
			reader.ReadString(desc);

			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			ProcessBegin(out, sessionIndex, id, time, desc.data);
			break;
		}
		case TraceType_ProcessExited:
		{
			u32 id = reader.ReadU32();
			u32 exitCode = reader.ReadU32();
			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			u32 sessionIndex;
			TraceView::Process& process = *ProcessEnd(out, sessionIndex, id, time);

			process.exitCode = exitCode;

			ProcessStats processStats;
			SessionStats sessionStats;
			StorageStats storageStats;
			KernelStats kernelStats;

			const u8* dataStart = reader.GetPositionData();
			processStats.Read(reader, out.version);

			UBA_ASSERT(process.isRemote == (sessionIndex != 0));

			if (process.isRemote)
			{
				if (out.version >= 7)
				{
					sessionStats.Read(reader, out.version);
					storageStats.Read(reader, out.version);
					kernelStats.Read(reader, out.version);
				}
			}
			else if (out.version >= 30)
			{
				storageStats.Read(reader, out.version);
				kernelStats.Read(reader, out.version);
			}
			const u8* dataEnd = reader.GetPositionData();
			process.stats.resize(dataEnd - dataStart);
			memcpy(process.stats.data(), dataStart, dataEnd - dataStart);

			if (out.version >= 34)
			{
				process.breadcrumbs = reader.ReadString();
			}

			process.createFilesTime = processStats.createFile.time;
			process.writeFilesTime = Max(processStats.writeFiles.time, processStats.sendFiles.time);

			if (out.version >= 22)
			{
				while (true)
				{
					auto type = (LogEntryType)reader.ReadByte();
					if (type == 255)
						break;
					process.logLines.emplace_back(reader.ReadString(), type);
				}
			}
			else if (out.version >= 20)
			{
				u64 logLineCount = reader.Read7BitEncoded();
				if (logLineCount >= 101)
					logLineCount = 101;
				process.logLines.reserve(logLineCount);
				while (logLineCount--)
				{
					auto type = (LogEntryType)reader.ReadByte();
					process.logLines.emplace_back(reader.ReadString(), type);
				}
			}

			break;
		}
		case TraceType_ProcessEnvironmentUpdated:
		{
			u32 processId = reader.ReadU32();
			auto findIt = m_activeProcesses.find(processId);
			if (findIt == m_activeProcesses.end())
				return false;
			StringBuffer<> reason;
			reader.ReadString(reason);
			if (out.version < 15)
				time = reader.Read7BitEncoded();
			TraceView::ProcessLocation active = findIt->second;
			auto& session = GetSession(out, active.sessionIndex);

			auto& processes = session.processors[active.processorIndex].processes;
			TraceView::Process& process = processes[active.processIndex];

			const u8* dataStart = reader.GetPositionData();

			ProcessStats processStats;
			SessionStats sessionStats;
			StorageStats storageStats;
			KernelStats kernelStats;
			processStats.Read(reader, out.version);
			sessionStats.Read(reader, out.version);
			storageStats.Read(reader, out.version);
			kernelStats.Read(reader, out.version);

			const u8* dataEnd = reader.GetPositionData();
			process.stats.resize(dataEnd - dataStart);
			memcpy(process.stats.data(), dataStart, dataEnd - dataStart);

			process.exitCode = 0u;
			process.stop = time;
			process.bitmapDirty = true;
			bool isRemote = process.isRemote;

			processes.emplace_back();
			auto& process2 = processes.back();
			m_activeProcesses[processId].processIndex = u32(processes.size() - 1);
			process2.id = processId;
			process2.description = reason.data;
			process2.start = time;
			process2.stop = ~u64(0);
			process2.exitCode = ~0u;
			process2.isRemote = isRemote;

			++session.processExitedCount;
			++out.totalProcessExitedCount;
			break;
		}
		case TraceType_ProcessReturned:
		{
			u32 id = reader.ReadU32();
			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}
			TString reason;
			if (out.version >= 33)
				reason = reader.ReadString();
			if (reason.empty())
				reason = TC("Unknown");

			auto findIt = m_activeProcesses.find(id);
			if (findIt == m_activeProcesses.end())
				return false;
			TraceView::ProcessLocation active = findIt->second;
			m_activeProcesses.erase(findIt);

			auto& session = GetSession(out, active.sessionIndex);
			--session.processActiveCount;
			--out.totalProcessActiveCount;

			TraceView::Process& process = session.processors[active.processorIndex].processes[active.processIndex];
			process.exitCode = 0;
			process.stop = time;
			process.returnedReason = reason;
			process.bitmapDirty = true;
			break;
		}
		case TraceType_FileBeginFetch:
		{
			Guid clientUid = ReadClientId(out, reader);
			CasKey key = reader.ReadCasKey();
			u64 size = reader.Read7BitEncoded();
			StringBuffer<> temp;
			const tchar* hint;
			if (out.version < 14)
			{
				reader.ReadString(temp);
				hint = temp.data;
			}
			else
				hint = out.strings[reader.Read7BitEncoded()].data();
				
			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			if (auto session = GetSession(out, clientUid))
			{
				session->fetchedFiles.push_back({ key, size, hint, time, ~u64(0) });
				session->fetchedFilesBytes += size;
			}
			break;
		}
		case TraceType_FileFetchLight:
		{
			Guid clientUid = ReadClientId(out, reader);
			u64 fileSize = reader.Read7BitEncoded();
			if (auto session = GetSession(out, clientUid))
			{
				session->fetchedFiles.push_back({ CasKeyZero, fileSize, L"", time, time });
				session->fetchedFilesBytes += fileSize;
			}
			break;
		}
		case TraceType_ProxyCreated:
		{
			Guid clientUid = ReadClientId(out, reader);
			StringBuffer<> proxyName;
			reader.ReadString(proxyName);
			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			if (auto session = GetSession(out, clientUid))
			{
				session->proxyName = proxyName.data;
				session->proxyCreated = true;
			}
			break;
		}
		case TraceType_ProxyUsed:
		{
			Guid clientUid = ReadClientId(out, reader);
			StringBuffer<> proxyName;
			reader.ReadString(proxyName);

			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			if (auto session = GetSession(out, clientUid))
				session->proxyName = proxyName.data;
			break;
		}
		case TraceType_FileEndFetch:
		{
			Guid clientUid = ReadClientId(out, reader);
			CasKey key = reader.ReadCasKey();

			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			if (auto session = GetSession(out, clientUid))
			{
				for (auto rit = session->fetchedFiles.rbegin(), rend = session->fetchedFiles.rend(); rit != rend; ++rit)
				{
					if (rit->key != key)
						continue;
					rit->stop = time;
					break;
				}
			}
			break;
		}
		case TraceType_FileBeginStore:
		{
			Guid clientUid = ReadClientId(out, reader);
			CasKey key = reader.ReadCasKey();
			u64 size = reader.Read7BitEncoded();
			StringBuffer<> temp;
			const tchar* hint;
			if (out.version < 14)
			{
				reader.ReadString(temp);
				hint = temp.data;
			}
			else
				hint = out.strings[reader.Read7BitEncoded()].data();
			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			if (auto session = GetSession(out, clientUid))
			{
				session->storedFiles.push_back({ key, size, hint, time, ~u64(0) });
				session->storedFilesBytes += size;
			}
			break;
		}
		case TraceType_FileStoreLight:
		{
			Guid clientUid = ReadClientId(out, reader);
			u64 fileSize = reader.Read7BitEncoded();
			if (auto session = GetSession(out, clientUid))
			{
				session->storedFiles.push_back({ CasKeyZero, fileSize, L"", time, time });
				session->storedFilesBytes += fileSize;
			}
			break;
		}
		case TraceType_FileEndStore:
		{
			Guid clientUid = ReadClientId(out, reader);
			CasKey key = reader.ReadCasKey();

			if (out.version < 15)
			{
				time = reader.Read7BitEncoded();
				if (time > maxTime)
				{
					reader.SetPosition(readPos);
					return true;
				}
			}

			if (auto session = GetSession(out, clientUid))
			{
				for (auto rit = session->storedFiles.rbegin(), rend = session->storedFiles.rend(); rit != rend; ++rit)
				{
					if (rit->key != key)
						continue;
					rit->stop = time;
					break;
				}
			}
			break;
		}
		case TraceType_Summary:
		{
			if (out.version < 15)
				time = reader.Read7BitEncoded();
			StopAllActive(out, time);
			return false;
		}
		case TraceType_BeginWork:
		{
			u32 workIndex;
			if (out.version < 14)
				workIndex = reader.ReadU32();
			else
				workIndex = u32(reader.Read7BitEncoded());

			u32 trackIndex = 0;
			TraceView::WorkTrack* workTrack = nullptr;
			for (u32 i=0, e=u32(out.workTracks.size()); i!=e; ++i)
			{
				auto& it = out.workTracks[i];
				if (it.records.back().stop == ~u64(0))
					continue;
				trackIndex = i;
				workTrack = &it;
				break;
			}
			if (!workTrack)
			{
				trackIndex = u32(out.workTracks.size());
				out.workTracks.emplace_back();
				workTrack = &out.workTracks.back();
			}

			workTrack->records.emplace_back();
			auto& record = workTrack->records.back();

			m_activeWorkRecords.try_emplace(workIndex, WorkRecordLocation{trackIndex, u32(workTrack->records.size() - 1)});

			u64 stringIndex;
			if (out.version < 14)
				stringIndex = reader.ReadU32();
			else
				stringIndex = reader.Read7BitEncoded();
				
			record.description = out.strings[stringIndex].data();
			if (out.version < 15)
				record.start = reader.Read7BitEncoded();
			else
				record.start = time;
			record.stop = ~u64(0);
			break;
		}
		case TraceType_EndWork:
		{
			u32 workIndex;
			if (out.version < 14)
				workIndex = reader.ReadU32();
			else
				workIndex = u32(reader.Read7BitEncoded());

			auto findIt = m_activeWorkRecords.find(workIndex);
			if (findIt == m_activeWorkRecords.end())
				return false;
			WorkRecordLocation active = findIt->second;
			m_activeWorkRecords.erase(findIt);

			TraceView::WorkRecord& record = out.workTracks[active.track].records[active.index];
			if (out.version < 15)
				record.stop = reader.Read7BitEncoded();
			else
				record.stop = time;
			break;
		}
		case TraceType_ProgressUpdate:
		{
			out.progressProcessesTotal = u32(reader.Read7BitEncoded());
			out.progressProcessesDone = u32(reader.Read7BitEncoded());
			out.progressErrorCount = u32(reader.Read7BitEncoded());
			break;
		}
		case TraceType_StatusUpdate:
		{
			if (out.version < 32)
			{
				reader.Read7BitEncoded();
				reader.Read7BitEncoded();
				reader.ReadString();
				reader.Read7BitEncoded();
				reader.ReadString();
				reader.ReadByte();
			}
			else
			{
				u64 row = reader.Read7BitEncoded();
				u64 column = reader.Read7BitEncoded();
				u64 key = (row << 32) | column;
				auto& status = out.statusMap[key];
				status.text = reader.ReadString();
				status.type = (LogEntryType)reader.ReadByte();
				status.link = reader.ReadString();
			}

			break;
		}
		case TraceType_RemoteExecutionDisabled:
		{
			out.remoteExecutionDisabled = true;
			break;
		}
		case TraceType_String:
		{
			out.strings.push_back(reader.ReadString());
			break;
		}
		case TraceType_CacheBeginFetch:
		{
			u32 id = u32(reader.Read7BitEncoded());
			StringBuffer<> desc;
			reader.ReadString(desc);
			ProcessBegin(out, 0, id, time, desc.data)->cacheFetch = true;
			break;
		}
		case TraceType_CacheEndFetch:
		{
			u32 id = u32(reader.Read7BitEncoded());
			bool success = reader.ReadBool();

			u32 sessionIndex;
			TraceView::Process& process = *ProcessEnd(out, sessionIndex, id, time);
			process.exitCode = 0;//success ? 0 : -1;
			if (!success)
				process.returnedReason = TC("M");

			CacheStats cacheStats;
			KernelStats kernelStats;
			StorageStats storageStats;
			const u8* dataStart = reader.GetPositionData();
			cacheStats.Read(reader, out.version);
			if (success || out.version >= 29)
			{
				storageStats.Read(reader, out.version);
				kernelStats.Read(reader, out.version);
			}
			const u8* dataEnd = reader.GetPositionData();
			process.stats.resize(dataEnd - dataStart);
			memcpy(process.stats.data(), dataStart, dataEnd - dataStart);
			break;
		}
		case TraceType_CacheBeginWrite:
		{
			u32 processId = u32(reader.Read7BitEncoded());
			out.cacheWrites[processId].start = time;
			break;
		}
		case TraceType_CacheEndWrite:
		{
			u32 processId = u32(reader.Read7BitEncoded());
			TraceView::CacheWrite& write = out.cacheWrites[processId];
			write.success = reader.ReadBool();
			write.bytesSent = reader.Read7BitEncoded();
			write.end = time;
		}
		}
		return true;
	}

	void TraceReader::StopAllActive(TraceView& out, u64 stopTime)
	{
		for (auto& pair : m_activeProcesses)
		{
			auto& active = pair.second;
			TraceView::Process& process = GetSession(out, active.sessionIndex).processors[active.processorIndex].processes[active.processIndex];
			process.exitCode = ~0u;
			process.stop = stopTime;
			process.bitmapDirty = true;
		}

		m_activeProcesses.clear();
		out.finished = true;
	}

	void TraceReader::Reset(TraceView& out)
	{
		out.Clear();
		m_activeProcesses.clear();
		m_activeWorkRecords.clear();
		m_sessionIndexToSession.clear();
	}

	void TraceReader::Unmap()
	{
		if (m_hostProcess)
			CloseHandle(m_hostProcess);
		m_hostProcess = 0;
		if (m_memoryBegin)
			UnmapViewOfFile(m_memoryBegin, 0, TC("TraceReader"));
		m_memoryBegin = nullptr;
		if (m_memoryHandle.IsValid())
			CloseFileMapping(m_memoryHandle);
		m_memoryHandle = {};
		m_namedTrace.clear();
	}

	bool TraceReader::SaveAs(const tchar* fileName)
	{
		if (!m_memoryBegin)
		{
			m_logger.Warning(L"Can only save traces that are opened using -listen/-name.");
			return false;
		}

		FileAccessor file(m_logger, fileName);
		if (!file.CreateWrite())
			return false;
		if (!file.Write(m_memoryBegin, m_memoryPos - m_memoryBegin))
			return false;
		return file.Close();
	}

	Guid TraceReader::ReadClientId(TraceView& out, BinaryReader& reader)
	{
		if (out.version < 15)
			return reader.ReadGuid();
		Guid clientUid;
		clientUid.data1 = u32(reader.Read7BitEncoded());
		return clientUid;
	}

	TraceView::Session& TraceReader::GetSession(TraceView& out, u32 sessionIndex)
	{
		return out.sessions[m_sessionIndexToSession[sessionIndex]];
	}

	TraceView::Session* TraceReader::GetSession(TraceView& out, const Guid& clientUid)
	{
		for (auto& session : out.sessions)
			if (session.clientUid == clientUid)
				return &session;
		// First file can be retrieved here before session is connected... haven't figured out how this can happen but let's ignore that for now :-)
		//UBA_ASSERTF(false, L"Failed to get session for clientUid %ls", GuidToString(clientUid).str);
		return nullptr;
	}

	TraceView::Process* TraceReader::ProcessBegin(TraceView& out, u32 sessionIndex, u32 id, u64 time, const tchar* description)
	{
		TraceView::Processor* processor = nullptr;
		TraceView::Session& session = GetSession(out, sessionIndex);

		u32 processorIndex = 0;
		for (u32 i=0, e=u32(session.processors.size()); i!=e; ++i)
		{
			auto& it = session.processors[i];
			if (it.processes.back().stop == ~u64(0))
				continue;
			processorIndex = i;
			processor = &it;
			break;
		}
		if (!processor)
		{
			processorIndex = u32(session.processors.size());
			session.processors.emplace_back();
			processor = &session.processors.back();
		}

		processor->processes.emplace_back();
		auto& process = processor->processes.back();

		m_activeProcesses.try_emplace(id, TraceView::ProcessLocation{sessionIndex, processorIndex, u32(processor->processes.size() - 1)});
			
		++session.processActiveCount;
		++out.totalProcessActiveCount;

		process.id = id;
		process.description = description;
		process.start = time;
		process.stop = ~u64(0);
		process.exitCode = ~0u;
		process.isRemote = sessionIndex != 0;
		return &process;
	}

	TraceView::Process* TraceReader::ProcessEnd(TraceView& out, u32& outSessionIndex, u32 id, u64 time)
	{
		auto findIt = m_activeProcesses.find(id);
		if (findIt == m_activeProcesses.end())
			return nullptr;
		TraceView::ProcessLocation active = findIt->second;
		m_activeProcesses.erase(findIt);
		outSessionIndex = active.sessionIndex;
		auto& session = GetSession(out, active.sessionIndex);
		++session.processExitedCount;
		--session.processActiveCount;

		++out.totalProcessExitedCount;
		--out.totalProcessActiveCount;

		TraceView::Process& process = session.processors[active.processorIndex].processes[active.processIndex];

		process.stop = time;
		process.bitmapDirty = true;
		return &process;
	}

#endif // PLATFORM_WINDOWS
}