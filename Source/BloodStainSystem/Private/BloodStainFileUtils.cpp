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

	FString GetFullFilePath(const FString& FileName, const FString& LevelName)
	{
		FString Dir = GetSaveDirectory() / LevelName;
		IFileManager::Get().MakeDirectory(*Dir, /*Tree*/true);
		return Dir / (FileName + TEXT(".bin"));
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

    // 2) (옵션에 따라) 압축 → Payload
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
    FileAr << FileHeader;
	FileAr << LocalCopy.Header;
    FileAr.Serialize(Payload.GetData(), Payload.Num());

    const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(FileName, LevelName);
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
    // Reading entire file from disk
    const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(FileName, LevelName);
    TArray<uint8> AllBytes;
    if (!FFileHelper::LoadFileToArray(AllBytes, *Path))
    {
        UE_LOG(LogBloodStain, Error, TEXT("[BS] LoadFromFile failed read: %s"), *Path);
        return false;	
    }

    // Header Deserialization
    FMemoryReader MemR(AllBytes, true);
    FBloodStainFileHeader FileHeader;
    MemR << FileHeader;  // 읽고 커서가 헤더 끝으로 이동
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

bool BloodStainFileUtils::LoadHeaderFromFile(const FString& FileName, const FString& LevelName, FRecordHeaderData& OutRecordHeaderData)
{	
	const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(FileName, LevelName);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> FileHandle(PlatformFile.OpenRead(*Path));

	if (!FileHandle)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] Failed to open file for reading: %s"), *Path);
		return false;
	}
	
	constexpr int64 BytesToRead = sizeof(FBloodStainFileHeader) + sizeof(FRecordHeaderData);

	if (FileHandle->Size() < BytesToRead)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] File is smaller than the expected header size: %s"), *Path);
		return false;
	}

	TArray<uint8> HeaderBytes;
	HeaderBytes.SetNum(BytesToRead);

	if (!FileHandle->Read(HeaderBytes.GetData(), BytesToRead))
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


int32 BloodStainFileUtils::LoadHeadersForAllFiles(TMap<FString, FRecordHeaderData>& OutLoadedHeaders, const FString& LevelName)
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
			// If loading was successful, add to the map
			OutLoadedHeaders.Add(BaseFileName, LoadedData);
		}
	}

	return OutLoadedHeaders.Num();
}

int32 BloodStainFileUtils::LoadAllFiles(TMap<FString, FRecordSaveData>& OutLoadedDataMap, const FString& LevelName)
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

FString BloodStainFileUtils::GetFullFilePath(const FString& FileName, const FString& LevelName)
{
	return BloodStainFileUtils_Internal::GetFullFilePath(FileName, LevelName);
}
