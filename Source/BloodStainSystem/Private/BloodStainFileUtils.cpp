/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "BloodStainFileUtils.h"
#include "BloodStainCompressionUtils.h"
#include "BloodStainSystem.h"
#include "QuantizationHelper.h"
#include "Serialization/BufferArchive.h"

namespace BloodStainFileUtils_Internal
{
	const TCHAR* FILE_EXTENSION = TEXT(".bin");

	FString GetSaveDirectory()
	{
		return FPaths::ProjectSavedDir() / TEXT("BloodStain");
	}

	FString GetSaveDirectory(const FString& LevelName)
	{
		FString Dir = GetSaveDirectory() / LevelName;
		return Dir;
	}

	/** @param FileName FileName without Extension
	 *  @param LevelName
	 */
	FString GetFullFilePath(const FString& FileName, const FString& LevelName)
	{
		const FString Dir = GetSaveDirectory() / LevelName;
		return Dir / (FileName + FILE_EXTENSION);
	}

	FString GetFullFilePath(const FString& RelativeFilePath)
	{
		const FString Dir = GetSaveDirectory();
		return Dir / (RelativeFilePath + FILE_EXTENSION);
	}
}

bool BloodStainFileUtils::SaveToFile(
    const FRecordSaveData&       SaveData,
    const FString&               LevelName,
    const FString&               FileName,
    const FBloodStainFileOptions& Options)
{
    FRecordSaveData LocalCopy = SaveData;
	FBloodStainFileOptions LocalOptions = Options;
	FBufferArchive BufferAr;

	BloodStainFileUtils_Internal::SerializeSaveData(BufferAr, LocalCopy,LocalOptions.Quantization);

    TArray<uint8> RawBytes;
    RawBytes.Append(BufferAr.GetData(), BufferAr.Num());
	
    TArray<uint8> Payload;
    if (Options.Compression.Method == ECompressionMethod::None)
    {
        Payload = MoveTemp(RawBytes);
    }
    else
    {
        if (!BloodStainCompressionUtils::CompressBuffer(RawBytes, Payload, Options.Compression))
        {
            UE_LOG(LogBloodStain, Error, TEXT("[BS] CompressBuffer failed"));
            return false;
        }
    }

    FBloodStainFileHeader FileHeader;
    FileHeader.Options          = Options;
    FileHeader.UncompressedSize = RawBytes.Num();

    FBufferArchive FileAr;

	FileAr.SetIsSaving(true);

	int64 StartPos = FileAr.Tell();
	int32 HeaderByteSize = 0;
	FileAr << HeaderByteSize;
	
    FileAr << FileHeader;
	FileAr << LocalCopy.Header;

	int64 EndPos = FileAr.Tell();
	HeaderByteSize = static_cast<int32>(EndPos - StartPos);

	FileAr.Seek(StartPos);
	FileAr << HeaderByteSize;

	FileAr.Seek(EndPos);

    FileAr.Serialize(Payload.GetData(), Payload.Num());

    const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(FileName, LevelName);

	const FString SaveDir = BloodStainFileUtils_Internal::GetSaveDirectory(LevelName);
	IFileManager::Get().MakeDirectory(*SaveDir, /*Tree*/true);
	
    bool bOK = FFileHelper::SaveArrayToFile(FileAr, *Path);
    FileAr.FlushCache(); FileAr.Empty();

    if (!bOK)
    {
        UE_LOG(LogBloodStain, Error, TEXT("[BS] SaveToFile failed: %s"), *Path);
    }

    
    for (const FRecordActorSaveData& RecordActorData : SaveData.RecordActorDataArray)
    {
        const int32 NumFrames = RecordActorData.RecordedFrames.Num();
        const float Duration  = NumFrames > 0 
            ? RecordActorData.RecordedFrames.Last().TimeStamp - RecordActorData.RecordedFrames[0].TimeStamp 
            : 0.0f;

        int32 BoneCount = 0;
        if (NumFrames > 0)
        {
            BoneCount = RecordActorData.RecordedFrames[0].ComponentTransforms.Num();
        }

        UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] Saved recording to %s"), *Path);
        UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] ▶ Duration: %.2f sec | Frames: %d | Sockets: %d"), 
            Duration, NumFrames, BoneCount);    
    }
    
    return bOK;
}

bool BloodStainFileUtils::LoadFromFile(const FString& FileName, const FString& LevelName, FRecordSaveData& OutData)
{
	const FString RelativeFilePath = GetRelativeFilePath(FileName, LevelName);
	return LoadFromFile(RelativeFilePath, OutData);
}

bool BloodStainFileUtils::LoadFromFile(const FString& RelativeFilePath, FRecordSaveData& OutData)
{
	// Reading entire file from disk
	const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(RelativeFilePath);
	TArray<uint8> AllBytes;
	if (!FFileHelper::LoadFileToArray(AllBytes, *Path))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] LoadFromFile failed read: %s"), *Path);
		return false;	
	}

	int32 HeaderByteSize;
	
	// Header Deserialization
	FMemoryReader MemR(AllBytes, true);
	FBloodStainFileHeader FileHeader;
	
	MemR << HeaderByteSize;
	MemR << FileHeader;
	MemR << OutData.Header;

	int64 Offset = MemR.Tell();
	int64 Remain = AllBytes.Num() - Offset;
	const uint8* Ptr = AllBytes.GetData() + Offset;

	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(Remain);
	FMemory::Memcpy(Compressed.GetData(), Ptr, Remain);

	TArray<uint8> RawBytes;
	if (FileHeader.Options.Compression.Method == ECompressionMethod::None)
	{
		RawBytes = MoveTemp(Compressed);
	}
	else
	{
		if (!BloodStainCompressionUtils::DecompressBuffer(FileHeader.UncompressedSize, Compressed, RawBytes, FileHeader.Options.Compression))
		{
			UE_LOG(LogBloodStain, Error, TEXT("[BS] DecompressBuffer failed"));
			return false;
		}
	}

	FMemoryReader MemoryReader(RawBytes, true);
	BloodStainFileUtils_Internal::DeserializeSaveData(MemoryReader,OutData,FileHeader.Options.Quantization);
	
	return true;
}

bool BloodStainFileUtils::LoadRawPayloadFromFile(const FString& FileName, const FString& LevelName,
	FBloodStainFileHeader& OutFileHeader, FRecordHeaderData& OutRecordHeader, TArray<uint8>& OutCompressedPayload)
{
	const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(FileName, LevelName);
	TArray<uint8> AllBytes;
	if (!FFileHelper::LoadFileToArray(AllBytes, *Path))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStainFileUtils] LoadRawPayloadFromFile failed read: %s"), *Path);
		return false;
	}

	int32 HeaderByteSize;
	
	FMemoryReader MemR(AllBytes, true);

	// Only Deserialize the file header and record header
	MemR << HeaderByteSize;
	MemR << OutFileHeader;
	MemR << OutRecordHeader;
	
	const int64 Offset = MemR.Tell();
	const int64 PayloadSize = AllBytes.Num() - Offset;
	
	if (PayloadSize > 0)
	{
		OutCompressedPayload.SetNumUninitialized(PayloadSize);
		FMemory::Memcpy(OutCompressedPayload.GetData(), AllBytes.GetData() + Offset, PayloadSize);
	}
    
	return true;
}

bool BloodStainFileUtils::LoadHeaderFromFile(const FString& FileName, const FString& LevelName, FRecordHeaderData& OutRecordHeaderData)
{
	const FString RelativeFilePath = GetRelativeFilePath(FileName, LevelName);
	return LoadHeaderFromFile(RelativeFilePath, OutRecordHeaderData);
}

bool BloodStainFileUtils::LoadHeaderFromFile(const FString& RelativeFilePath, FRecordHeaderData& OutRecordHeaderData)
{	
	const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(RelativeFilePath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> FileHandle(PlatformFile.OpenRead(*Path));

	if (!FileHandle)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] Failed to open file for reading: %s"), *Path);
		return false;
	}

	if (FileHandle->Size() <= 0)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] File is smaller than the expected header size: %s"), *Path);
		return false;
	}
	
	int32 HeaderByteSize = 0;

	int32 IntByteSize = sizeof(int32);
	{
		TArray<uint8> IntBuffer;
		IntBuffer.SetNum(IntByteSize);
		
		if (FileHandle->Size() < IntByteSize)
		{
			UE_LOG(LogBloodStain, Error, TEXT("[BS] File is smaller than the expected header size: %s"), *Path);
			return false;
		}
		
		if (!FileHandle->Read(IntBuffer.GetData(), IntByteSize))
		{
			UE_LOG(LogBloodStain, Error, TEXT("[BS] Failed to read header size from file: %s"), *Path);
			return false;
		}

		FMemoryReader IntReader(IntBuffer, true);
		IntReader << HeaderByteSize;
	}

	if (FileHandle->Size() < HeaderByteSize)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] File is smaller than the expected header size: %s"), *Path);
		return false;
	}

	TArray<uint8> HeaderBytes;
	HeaderBytes.SetNum(HeaderByteSize);
	if (!FileHandle->Read(HeaderBytes.GetData(), HeaderByteSize - IntByteSize))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] Failed to read header data from file: %s"), *Path);
		return false;
	}
	
	FMemoryReader MemR(HeaderBytes, true);
	FBloodStainFileHeader FileHeader;
	MemR << FileHeader;
	MemR << OutRecordHeaderData;
	
	return true;
}

int32 BloodStainFileUtils::LoadHeadersForAllFilesInLevel(TMap<FString, FRecordHeaderData>& OutLoadedHeaders, const TArray<FString>& LevelNames)
{
	// Initialize existing map data
	OutLoadedHeaders.Empty();

	IFileManager& FileManager = IFileManager::Get();
	const FString FilePattern = FString(TEXT("*")) + BloodStainFileUtils_Internal::FILE_EXTENSION; // "*.bin"
	
	for (const FString& LevelName : LevelNames)
	{
		// Decide the directory and file pattern to search for
		const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory() / LevelName;

		// Find all files in the specified directory that match the pattern
		TArray<FString> FoundFileNamesWithExt;
		FileManager.FindFiles(FoundFileNamesWithExt, *SearchDirectory, *FilePattern);

		UE_LOG(LogBloodStain, Log, TEXT("Found %d recording files in %s."), FoundFileNamesWithExt.Num(), *SearchDirectory);

		// Load each found file
		for (const FString& FileNameWithExt : FoundFileNamesWithExt)
		{
			FString BaseFileName = FileNameWithExt;
			BaseFileName.RemoveFromEnd(BloodStainFileUtils_Internal::FILE_EXTENSION);

			FRecordHeaderData LoadedData;
			if (LoadHeaderFromFile(BaseFileName, LevelName, LoadedData))
			{
				const FString RelativeFilePath = GetRelativeFilePath(BaseFileName, LevelName);
				// If loading was successful, add to the map
				OutLoadedHeaders.Add(RelativeFilePath, LoadedData);
			}
		}
	}
	return OutLoadedHeaders.Num();
}

int32 BloodStainFileUtils::LoadHeadersForAllFilesInLevel(TMap<FString, FRecordHeaderData>& OutLoadedHeaders, const FString& LevelName)
{
	// Initialize existing map data
	OutLoadedHeaders.Empty();

	IFileManager& FileManager = IFileManager::Get();

	// Decide the directory and file pattern to search for
	const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory() / LevelName;
	const FString FilePattern = FString(TEXT("*")) + BloodStainFileUtils_Internal::FILE_EXTENSION; // "*.bin"

	// Find all files in the specified directory that match the pattern
	TArray<FString> FoundFileNamesWithExt;
	FileManager.FindFiles(FoundFileNamesWithExt, *SearchDirectory, *FilePattern);

	UE_LOG(LogBloodStain, Log, TEXT("Found %d recording files in %s."), FoundFileNamesWithExt.Num(), *SearchDirectory);

	// Load each found file
	for (const FString& FileNameWithExt : FoundFileNamesWithExt)
	{
		FString BaseFileName = FileNameWithExt;
		BaseFileName.RemoveFromEnd(BloodStainFileUtils_Internal::FILE_EXTENSION);

		FRecordHeaderData LoadedData;
		if (LoadHeaderFromFile(BaseFileName, LevelName, LoadedData))
		{
			const FString RelativeFilePath = GetRelativeFilePath(BaseFileName, LevelName);
			// If loading was successful, add to the map
			OutLoadedHeaders.Add(RelativeFilePath, LoadedData);
		}
	}

	return OutLoadedHeaders.Num();
}

int32 BloodStainFileUtils::LoadHeadersForAllFiles(TMap<FString, FRecordHeaderData>& OutLoadedHeaders)
{
	// Initialize existing map data
	OutLoadedHeaders.Empty();

	IFileManager& FileManager = IFileManager::Get();

	// Decide the directory and file pattern to search for
	const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory();
	const FString FilePattern = FString(TEXT("*")) + BloodStainFileUtils_Internal::FILE_EXTENSION; // "*.bin"

	// Find all files in the specified directory that match the pattern
	TArray<FString> FoundFileNamesWithExt;
	FileManager.FindFilesRecursive(FoundFileNamesWithExt, *SearchDirectory, *FilePattern, true, false);

	UE_LOG(LogBloodStain, Log, TEXT("Found %d recording files in %s."), FoundFileNamesWithExt.Num(), *SearchDirectory);

	// Load each found file
	for (const FString& FileNameWithExt : FoundFileNamesWithExt)
	{
		const FString RelativeFilePathWithExt = FileNameWithExt.Replace(*SearchDirectory, TEXT(""));
		
		FString RelativeFilePathWithoutExt = RelativeFilePathWithExt;
		RelativeFilePathWithoutExt.RemoveFromStart(TEXT("/"));
		RelativeFilePathWithoutExt.RemoveFromEnd(BloodStainFileUtils_Internal::FILE_EXTENSION);

		FRecordHeaderData LoadedData;
		if (LoadHeaderFromFile(RelativeFilePathWithoutExt, LoadedData))
		{
			// If loading was successful, add to the map
			OutLoadedHeaders.Add(RelativeFilePathWithExt, LoadedData);
		}
	}

	return OutLoadedHeaders.Num();
}

int32 BloodStainFileUtils::LoadAllFilesInLevel(TMap<FString, FRecordSaveData>& OutLoadedDataMap, const FString& LevelName)
{
	OutLoadedDataMap.Empty();

	IFileManager& FileManager = IFileManager::Get();

	const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory() / LevelName;
	const FString FilePattern = FString(TEXT("*")) + BloodStainFileUtils_Internal::FILE_EXTENSION; // "*.bin"

	TArray<FString> FoundFileNamesWithExt;
	FileManager.FindFiles(FoundFileNamesWithExt, *SearchDirectory, *FilePattern);

	UE_LOG(LogBloodStain, Log, TEXT("Found %d recording files in %s."), FoundFileNamesWithExt.Num(), *SearchDirectory);

	for (const FString& FileNameWithExt : FoundFileNamesWithExt)
	{
		FString BaseFileName = FileNameWithExt;
		BaseFileName.RemoveFromEnd(BloodStainFileUtils_Internal::FILE_EXTENSION);

		FRecordSaveData LoadedData;
		if (LoadFromFile(BaseFileName, LevelName, LoadedData))
		{
			OutLoadedDataMap.Add(BaseFileName, LoadedData);
		}
	}

	return OutLoadedDataMap.Num();
}

int32 BloodStainFileUtils::LoadAllFilesInLevel(TMap<FString, FRecordSaveData>& OutLoadedDataMap, const TArray<FString>& LevelNames)
{
	OutLoadedDataMap.Empty();

	IFileManager& FileManager = IFileManager::Get();
	const FString FilePattern = FString(TEXT("*")) + BloodStainFileUtils_Internal::FILE_EXTENSION; // "*.bin"

	for (const FString& LevelName : LevelNames)
	{
		const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory() / LevelName;

		TArray<FString> FoundFileNamesWithExt;
		FileManager.FindFiles(FoundFileNamesWithExt, *SearchDirectory, *FilePattern);

		UE_LOG(LogBloodStain, Log, TEXT("Found %d recording files in %s."), FoundFileNamesWithExt.Num(), *SearchDirectory);

		for (const FString& FileNameWithExt : FoundFileNamesWithExt)
		{
			FString BaseFileName = FileNameWithExt;
			BaseFileName.RemoveFromEnd(BloodStainFileUtils_Internal::FILE_EXTENSION);

			FRecordSaveData LoadedData;
			if (LoadFromFile(BaseFileName, LevelName, LoadedData))
			{
				OutLoadedDataMap.Add(BaseFileName, LoadedData);
			}
		}
	}

	return OutLoadedDataMap.Num();
}

int32 BloodStainFileUtils::LoadAllFiles(TMap<FString, FRecordSaveData>& OutLoadedDataMap)
{
	OutLoadedDataMap.Empty();

	IFileManager& FileManager = IFileManager::Get();

	const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory();
	const FString FilePattern = FString(TEXT("*")) + BloodStainFileUtils_Internal::FILE_EXTENSION; // "*.bin"

	TArray<FString> FoundFileNamesWithExt;
	FileManager.FindFilesRecursive(FoundFileNamesWithExt, *SearchDirectory, *FilePattern, true, false);

	UE_LOG(LogBloodStain, Log, TEXT("Found %d recording files in %s."), FoundFileNamesWithExt.Num(), *SearchDirectory);

	for (const FString& FileNameWithExt : FoundFileNamesWithExt)
	{
		const FString RelativeFilePathWithExt = FileNameWithExt.Replace(*SearchDirectory, TEXT(""));
		
		FString RelativeFilePathWithoutExt = RelativeFilePathWithExt;
		RelativeFilePathWithoutExt.RemoveFromStart(TEXT("/"));
		RelativeFilePathWithoutExt.RemoveFromEnd(BloodStainFileUtils_Internal::FILE_EXTENSION);

		FRecordSaveData LoadedData;
		if (LoadFromFile(RelativeFilePathWithoutExt, LoadedData))
		{
			OutLoadedDataMap.Add(RelativeFilePathWithoutExt, LoadedData);
		}
	}

	return OutLoadedDataMap.Num();
}


bool BloodStainFileUtils::DeleteFile(const FString& FileName, const FString& LevelName)
{
	const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(FileName, LevelName);
	
	if (FPaths::FileExists(Path))
	{
		const bool bSuccess = IFileManager::Get().Delete(*Path);
		if (!bSuccess)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Delete File] Failed to delete file: %s"), *Path);
		}
		return bSuccess;
	}
	else
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[Delete File] File does not exist: %s"), *Path);
		return false;
	}
}

TArray<FString> BloodStainFileUtils::GetSavedLevelNames()
{
	IFileManager& FileManager = IFileManager::Get();

	const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory();

	TArray<FString> SubDirectories;
	FileManager.FindFiles(SubDirectories, *(SearchDirectory / TEXT("*")), false, true);

	TArray<FString> LevelNames;
	
	for (const FString& SubDirName : SubDirectories)
	{
		const FString FullSubDirPath = SearchDirectory / SubDirName;

		TArray<FString> FilesInSubDir;
		FileManager.FindFiles(FilesInSubDir, *(FullSubDirPath / TEXT("*.*")), true, false);

		if (FilesInSubDir.Num() > 0)
		{
			LevelNames.Add(SubDirName);
		}
	}

	return LevelNames;
}

TArray<FString> BloodStainFileUtils::GetSavedFileNames(const FString& LevelName)
{
	IFileManager& FileManager = IFileManager::Get();
	
	const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory();
	const FString LevelDirectory = SearchDirectory / LevelName;
	
	TArray<FString> FileNamesWithExt;
	FileManager.FindFiles(FileNamesWithExt, *(LevelDirectory / TEXT("*.*")), true, false);

	TArray<FString> FileNames;

	for (const FString& FileNameWithExt : FileNamesWithExt)
	{
		const FString FileName = FPaths::GetBaseFilename(FileNameWithExt);
		FileNames.Add(FileName);
	}
	
	return FileNames;
}

FString BloodStainFileUtils::GetFullFilePath(const FString& FileName, const FString& LevelName)
{
	return BloodStainFileUtils_Internal::GetFullFilePath(FileName, LevelName);
}

FString BloodStainFileUtils::GetRelativeFilePath(const FString& FileName, const FString& LevelName)
{
	return LevelName/FileName;
}