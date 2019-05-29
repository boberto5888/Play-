#include "../AppConfig.h"
#include "Iop_Ioman.h"
#include "StdStream.h"
#include "../states/XmlStateFile.h"
#include "xml/Utils.h"
#include "../Log.h"
#include <stdexcept>
#include <cctype>
#include "std_experimental_map.h"

using namespace Iop;

#define LOG_NAME "iop_ioman"

#define STATE_FILES_FILENAME ("iop_ioman/files.xml")
#define STATE_FILES_FILESNODE "Files"
#define STATE_FILES_FILENODE "File"
#define STATE_FILES_FILENODE_IDATTRIBUTE ("Id")
#define STATE_FILES_FILENODE_PATHATTRIBUTE ("Path")
#define STATE_FILES_FILENODE_FLAGSATTRIBUTE ("Flags")

#define PREF_IOP_FILEIO_STDLOGGING ("iop.fileio.stdlogging")

#define FUNCTION_ADDDRV "AddDrv"
#define FUNCTION_DELDRV "DelDrv"

//Directories have "group read" only permissions? This is required by PS2PSXe.
#define STAT_MODE_DIR (0747 | (1 << 12))  //File mode + Dir type (1)
#define STAT_MODE_FILE (0777 | (2 << 12)) //File mode + File type (2)

static std::string RightTrim(std::string inputString)
{
	auto nonSpaceEnd = std::find_if(inputString.rbegin(), inputString.rend(), [](int ch) { return !std::isspace(ch); });
	inputString.erase(nonSpaceEnd.base(), inputString.end());
	return inputString;
}

struct PATHINFO
{
	std::string deviceName;
	std::string devicePath;
};

static PATHINFO SplitPath(const char* path)
{
	std::string fullPath(path);
	auto position = fullPath.find(":");
	if(position == std::string::npos)
	{
		throw std::runtime_error("Invalid path.");
	}
	PATHINFO result;
	result.deviceName = std::string(fullPath.begin(), fullPath.begin() + position);
	result.devicePath = std::string(fullPath.begin() + position + 1, fullPath.end());
	//Some games (Street Fighter EX3) provide paths with trailing spaces
	result.devicePath = RightTrim(result.devicePath);
	return result;
}

CIoman::CIoman(uint8* ram)
    : m_ram(ram)
    , m_nextFileHandle(3)
{
	CAppConfig::GetInstance().RegisterPreferenceBoolean(PREF_IOP_FILEIO_STDLOGGING, false);

	//Insert standard files if requested.
	if(CAppConfig::GetInstance().GetPreferenceBoolean(PREF_IOP_FILEIO_STDLOGGING)
#ifdef DEBUGGER_INCLUDED
	   || true
#endif
	)
	{
		try
		{
			auto stdoutPath = CAppConfig::GetBasePath() / "ps2_stdout.txt";
			auto stderrPath = CAppConfig::GetBasePath() / "ps2_stderr.txt";

			m_files[FID_STDOUT] = FileInfo{new Framework::CStdStream(fopen(stdoutPath.string().c_str(), "ab"))};
			m_files[FID_STDERR] = FileInfo{new Framework::CStdStream(fopen(stderrPath.string().c_str(), "ab"))};
		}
		catch(...)
		{
			//Humm, some error occured when opening these files...
		}
	}
}

CIoman::~CIoman()
{
	m_files.clear();
	m_devices.clear();
}

std::string CIoman::GetId() const
{
	return "ioman";
}

std::string CIoman::GetFunctionName(unsigned int functionId) const
{
	switch(functionId)
	{
	case 4:
		return "open";
		break;
	case 5:
		return "close";
		break;
	case 6:
		return "read";
		break;
	case 8:
		return "seek";
		break;
	case 16:
		return "getstat";
		break;
	case 20:
		return FUNCTION_ADDDRV;
		break;
	case 21:
		return FUNCTION_DELDRV;
		break;
	default:
		return "unknown";
		break;
	}
}

void CIoman::RegisterDevice(const char* name, const DevicePtr& device)
{
	m_devices[name] = device;
}

uint32 CIoman::Open(uint32 flags, const char* path)
{
	CLog::GetInstance().Print(LOG_NAME, "Open(flags = 0x%08X, path = '%s');\r\n", flags, path);

	if(flags == 0)
	{
		//If no flags provided, assume we want to open the file
		//Required by Capcom Arcade Collection
		flags = Ioman::CDevice::OPEN_FLAG_RDONLY;
	}

	uint32 handle = 0xFFFFFFFF;
	try
	{
		auto stream = OpenInternal(flags, path);
		handle = m_nextFileHandle++;
		FileInfo fileInfo(stream);
		fileInfo.flags = flags;
		fileInfo.path = path;
		m_files[handle] = std::move(fileInfo);
	}
	catch(const std::exception& except)
	{
		CLog::GetInstance().Warn(LOG_NAME, "%s: Error occured while trying to open file : %s\r\n", __FUNCTION__, except.what());
	}
	return handle;
}

Framework::CStream* CIoman::OpenInternal(uint32 flags, const char* path)
{
	auto pathInfo = SplitPath(path);
	auto deviceIterator = m_devices.find(pathInfo.deviceName);
	if(deviceIterator == m_devices.end())
	{
		throw std::runtime_error("Device not found.");
	}
	auto stream = deviceIterator->second->GetFile(flags, pathInfo.devicePath.c_str());
	if(!stream)
	{
		throw std::runtime_error("File not found.");
	}
	return stream;
}

uint32 CIoman::Close(uint32 handle)
{
	CLog::GetInstance().Print(LOG_NAME, "Close(handle = %d);\r\n", handle);

	uint32 result = 0xFFFFFFFF;
	try
	{
		auto file(m_files.find(handle));
		if(file == std::end(m_files))
		{
			throw std::runtime_error("Invalid file handle.");
		}
		m_files.erase(file);
		//Returns handle instead of 0 (needed by Naruto: Ultimate Ninja 2)
		result = handle;
	}
	catch(const std::exception& except)
	{
		CLog::GetInstance().Warn(LOG_NAME, "%s: Error occured while trying to close file : %s\r\n", __FUNCTION__, except.what());
	}
	return result;
}

uint32 CIoman::Read(uint32 handle, uint32 size, void* buffer)
{
	CLog::GetInstance().Print(LOG_NAME, "Read(handle = %d, size = 0x%X, buffer = ptr);\r\n", handle, size);

	uint32 result = 0xFFFFFFFF;
	try
	{
		auto stream = GetFileStream(handle);
		if(stream->IsEOF())
		{
			result = 0;
		}
		else
		{
			result = static_cast<uint32>(stream->Read(buffer, size));
		}
	}
	catch(const std::exception& except)
	{
		CLog::GetInstance().Warn(LOG_NAME, "%s: Error occured while trying to read file : %s\r\n", __FUNCTION__, except.what());
	}
	return result;
}

uint32 CIoman::Write(uint32 handle, uint32 size, const void* buffer)
{
	CLog::GetInstance().Print(LOG_NAME, "Write(handle = %d, size = 0x%X, buffer = ptr);\r\n", handle, size);

	uint32 result = 0xFFFFFFFF;
	try
	{
		auto stream = GetFileStream(handle);
		result = static_cast<uint32>(stream->Write(buffer, size));
		if((handle == FID_STDOUT) || (handle == FID_STDERR))
		{
			//Force flusing stdout and stderr
			stream->Flush();
		}
	}
	catch(const std::exception& except)
	{
		if((handle != FID_STDOUT) && (handle != FID_STDERR))
		{
			CLog::GetInstance().Warn(LOG_NAME, "%s: Error occured while trying to write file : %s\r\n", __FUNCTION__, except.what());
		}
	}
	return result;
}

uint32 CIoman::Seek(uint32 handle, uint32 position, uint32 whence)
{
	CLog::GetInstance().Print(LOG_NAME, "Seek(handle = %d, position = 0x%X, whence = %d);\r\n",
	                          handle, position, whence);

	uint32 result = 0xFFFFFFFF;
	try
	{
		auto stream = GetFileStream(handle);
		switch(whence)
		{
		case SEEK_DIR_SET:
			whence = Framework::STREAM_SEEK_SET;
			break;
		case SEEK_DIR_CUR:
			whence = Framework::STREAM_SEEK_CUR;
			break;
		case SEEK_DIR_END:
			whence = Framework::STREAM_SEEK_END;
			break;
		}

		stream->Seek(position, static_cast<Framework::STREAM_SEEK_DIRECTION>(whence));
		result = static_cast<uint32>(stream->Tell());
	}
	catch(const std::exception& except)
	{
		CLog::GetInstance().Warn(LOG_NAME, "%s: Error occured while trying to seek file : %s\r\n", __FUNCTION__, except.what());
	}
	return result;
}

int32 CIoman::Dopen(const char* path)
{
	CLog::GetInstance().Print(LOG_NAME, "Dopen(path = '%s');\r\n",
	                          path);
	int32 handle = -1;
	try
	{
		auto pathInfo = SplitPath(path);
		auto deviceIterator = m_devices.find(pathInfo.deviceName);
		if(deviceIterator == m_devices.end())
		{
			throw std::runtime_error("Device not found.");
		}
		auto directory = deviceIterator->second->GetDirectory(pathInfo.devicePath.c_str());
		handle = m_nextFileHandle++;
		m_directories[handle] = directory;
	}
	catch(const std::exception& except)
	{
		CLog::GetInstance().Warn(LOG_NAME, "%s: Error occured while trying to open directory : %s\r\n", __FUNCTION__, except.what());
	}
	return handle;
}

int32 CIoman::Dclose(uint32 handle)
{
	CLog::GetInstance().Print(LOG_NAME, "Dclose(handle = %d);\r\n",
	                          handle);

	auto directoryIterator = m_directories.find(handle);
	if(directoryIterator == std::end(m_directories))
	{
		return -1;
	}

	m_directories.erase(directoryIterator);
	return 0;
}

int32 CIoman::Dread(uint32 handle, DIRENTRY* dirEntry)
{
	CLog::GetInstance().Print(LOG_NAME, "Dread(handle = %d, entry = ptr);\r\n",
	                          handle);

	auto directoryIterator = m_directories.find(handle);
	if(directoryIterator == std::end(m_directories))
	{
		return -1;
	}

	auto& directory = directoryIterator->second;
	if(directory == Ioman::Directory())
	{
		return 0;
	}

	auto itemPath = directory->path();
	auto name = itemPath.leaf().string();
	strncpy(dirEntry->name, name.c_str(), DIRENTRY::NAME_SIZE);
	dirEntry->name[DIRENTRY::NAME_SIZE - 1] = 0;

	auto& stat = dirEntry->stat;
	memset(&stat, 0, sizeof(STAT));
	if(boost::filesystem::is_directory(itemPath))
	{
		stat.mode = STAT_MODE_DIR;
		stat.attr = 0x8427;
	}
	else
	{
		stat.mode = STAT_MODE_FILE;
		stat.loSize = boost::filesystem::file_size(itemPath);
		stat.attr = 0x8497;
	}

	directory++;

	return strlen(dirEntry->name);
}

uint32 CIoman::GetStat(const char* path, STAT* stat)
{
	CLog::GetInstance().Print(LOG_NAME, "GetStat(path = '%s', stat = ptr);\r\n", path);

	//Try with a file
	{
		int32 fd = Open(Ioman::CDevice::OPEN_FLAG_RDONLY, path);
		if(fd >= 0)
		{
			uint32 size = Seek(fd, 0, SEEK_DIR_END);
			Close(fd);
			memset(stat, 0, sizeof(STAT));
			stat->mode = STAT_MODE_FILE;
			stat->loSize = size;
			return 0;
		}
	}

	//Try with a directory
	{
		int32 fd = Dopen(path);
		if(fd >= 0)
		{
			Dclose(fd);
			memset(stat, 0, sizeof(STAT));
			stat->mode = STAT_MODE_DIR;
			return 0;
		}
	}

	return -1;
}

uint32 CIoman::AddDrv(uint32 drvPtr)
{
	CLog::GetInstance().Print(LOG_NAME, FUNCTION_ADDDRV "(drvPtr = 0x%08X);\r\n",
	                          drvPtr);
	return -1;
}

uint32 CIoman::DelDrv(uint32 drvNamePtr)
{
	CLog::GetInstance().Print(LOG_NAME, FUNCTION_DELDRV "(drvNamePtr = %s);\r\n",
	                          PrintStringParameter(m_ram, drvNamePtr).c_str());
	return -1;
}

Framework::CStream* CIoman::GetFileStream(uint32 handle)
{
	auto file(m_files.find(handle));
	if(file == std::end(m_files))
	{
		throw std::runtime_error("Invalid file handle.");
	}
	return file->second.stream;
}

void CIoman::SetFileStream(uint32 handle, Framework::CStream* stream)
{
	auto prevStreamIterator = m_files.find(handle);
	m_files.erase(prevStreamIterator);
	m_files[handle] = {stream};
}

//IOP Invoke
void CIoman::Invoke(CMIPS& context, unsigned int functionId)
{
	switch(functionId)
	{
	case 4:
		context.m_State.nGPR[CMIPS::V0].nD0 = static_cast<int32>(Open(
		    context.m_State.nGPR[CMIPS::A1].nV[0],
		    reinterpret_cast<char*>(&m_ram[context.m_State.nGPR[CMIPS::A0].nV[0]])));
		break;
	case 5:
		context.m_State.nGPR[CMIPS::V0].nD0 = static_cast<int32>(Close(
		    context.m_State.nGPR[CMIPS::A0].nV[0]));
		break;
	case 6:
		context.m_State.nGPR[CMIPS::V0].nD0 = static_cast<int32>(Read(
		    context.m_State.nGPR[CMIPS::A0].nV[0],
		    context.m_State.nGPR[CMIPS::A2].nV[0],
		    &m_ram[context.m_State.nGPR[CMIPS::A1].nV[0]]));
		break;
	case 8:
		context.m_State.nGPR[CMIPS::V0].nD0 = static_cast<int32>(Seek(
		    context.m_State.nGPR[CMIPS::A0].nV[0],
		    context.m_State.nGPR[CMIPS::A1].nV[0],
		    context.m_State.nGPR[CMIPS::A2].nV[0]));
		break;
	case 16:
		context.m_State.nGPR[CMIPS::V0].nD0 = static_cast<int32>(GetStat(
		    reinterpret_cast<char*>(&m_ram[context.m_State.nGPR[CMIPS::A0].nV[0]]),
		    reinterpret_cast<STAT*>(&m_ram[context.m_State.nGPR[CMIPS::A1].nV[0]])));
		break;
	case 20:
		context.m_State.nGPR[CMIPS::V0].nD0 = static_cast<int32>(AddDrv(
		    context.m_State.nGPR[CMIPS::A0].nV0));
		break;
	case 21:
		context.m_State.nGPR[CMIPS::V0].nD0 = static_cast<int32>(DelDrv(
		    context.m_State.nGPR[CMIPS::A0].nV0));
		break;
	default:
		CLog::GetInstance().Warn(LOG_NAME, "%s(%08X): Unknown function (%d) called.\r\n", __FUNCTION__, context.m_State.nPC, functionId);
		break;
	}
}

void CIoman::SaveState(Framework::CZipArchiveWriter& archive)
{
	auto fileStateFile = new CXmlStateFile(STATE_FILES_FILENAME, STATE_FILES_FILESNODE);
	auto filesStateNode = fileStateFile->GetRoot();

	for(const auto& filePair : m_files)
	{
		if(filePair.first == FID_STDOUT) continue;
		if(filePair.first == FID_STDERR) continue;

		const auto& file = filePair.second;

		auto fileStateNode = new Framework::Xml::CNode(STATE_FILES_FILENODE, true);
		fileStateNode->InsertAttribute(Framework::Xml::CreateAttributeIntValue(STATE_FILES_FILENODE_IDATTRIBUTE, filePair.first));
		fileStateNode->InsertAttribute(Framework::Xml::CreateAttributeIntValue(STATE_FILES_FILENODE_FLAGSATTRIBUTE, file.flags));
		fileStateNode->InsertAttribute(Framework::Xml::CreateAttributeStringValue(STATE_FILES_FILENODE_PATHATTRIBUTE, file.path.c_str()));
		filesStateNode->InsertNode(fileStateNode);
	}

	archive.InsertFile(fileStateFile);
}

void CIoman::LoadState(Framework::CZipArchiveReader& archive)
{
	std::experimental::erase_if(m_files,
	                            [](const FileMapType::value_type& filePair) {
		                            return (filePair.first != FID_STDOUT) && (filePair.first != FID_STDERR);
	                            });

	auto fileStateFile = CXmlStateFile(*archive.BeginReadFile(STATE_FILES_FILENAME));
	auto fileStateNode = fileStateFile.GetRoot();

	int32 maxFileId = 0;
	auto fileNodes = fileStateNode->SelectNodes(STATE_FILES_FILESNODE "/" STATE_FILES_FILENODE);
	for(auto fileNode : fileNodes)
	{
		int32 id = 0, flags = 0;
		std::string path;
		if(!Framework::Xml::GetAttributeIntValue(fileNode, STATE_FILES_FILENODE_IDATTRIBUTE, &id)) break;
		if(!Framework::Xml::GetAttributeStringValue(fileNode, STATE_FILES_FILENODE_PATHATTRIBUTE, &path)) break;
		if(!Framework::Xml::GetAttributeIntValue(fileNode, STATE_FILES_FILENODE_FLAGSATTRIBUTE, &flags)) break;

		auto stream = OpenInternal(flags, path.c_str());
		FileInfo fileInfo(stream);
		fileInfo.flags = flags;
		fileInfo.path = path;
		m_files[id] = std::move(fileInfo);

		maxFileId = std::max(maxFileId, id);
	}
	m_nextFileHandle = maxFileId + 1;
}

//--------------------------------------------------
// CFile
//--------------------------------------------------

CIoman::CFile::CFile(uint32 handle, CIoman& ioman)
    : m_handle(handle)
    , m_ioman(ioman)
{
}

CIoman::CFile::~CFile()
{
	m_ioman.Close(m_handle);
}

CIoman::CFile::operator uint32()
{
	return m_handle;
}
