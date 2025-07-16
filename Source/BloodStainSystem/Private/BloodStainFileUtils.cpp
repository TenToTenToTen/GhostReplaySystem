/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "BloodStainFileUtils.h"
#include "BloodStainCompressionUtils.h"
#include "BloodStainSystem.h"
#include "QuantizationHelper.h"
#include "Serialization/BufferArchive.h"
// #include "Serialization/BufferArchive.h"

namespace BloodStainFileUtils_Internal
{
	const TCHAR* FILE_EXTENSION = TEXT(".bin");

	FString GetSaveDirectory()
	{
		return FPaths::ProjectSavedDir() / TEXT("BloodStain");
	}

	FString GetFullFilePath(const FString& FileName)
	{
		FString Dir = GetSaveDirectory();
		IFileManager::Get().MakeDirectory(*Dir, /*Tree*/true);
		return Dir / (FileName + TEXT(".bin"));
	}
}

bool BloodStainFileUtils::SaveToFile(
    const FRecordSaveData&       SaveData,
    const FString&               FileName,
    const FBloodStainFileOptions& Options)
{
    // 1) Raw 직렬화 → RawBytes
    FRecordSaveData LocalCopy = SaveData;
	FBloodStainFileOptions LocalOptions = Options;
    // QuantizationArchive를 써서, FTransform 필드는 자동으로 양자화됩니다
	FBufferArchive BufferAr;

	BloodStainFileUtils_Internal::SerializeSaveData(
		BufferAr,
		LocalCopy,
		LocalOptions.Quantization
	);

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
        if (!BloodStainCompressionUtils::CompressBuffer(
                RawBytes, Payload, Options.Compression))
        {
            UE_LOG(LogBloodStain, Error, TEXT("[BS] CompressBuffer failed"));
            return false;
        }
    }

    // 3) 헤더 준비
    FBloodStainFileHeader FileHeader;
    FileHeader.Options          = Options;
    FileHeader.UncompressedSize = RawBytes.Num();

    // 4) 파일 아카이브: Header + Payload
    FBufferArchive FileAr;
    FileAr << FileHeader;
	FileAr << LocalCopy.Header;
    FileAr.Serialize(Payload.GetData(), Payload.Num());

    const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(FileName);
    bool bOK = FFileHelper::SaveArrayToFile(FileAr, *Path);
    FileAr.FlushCache(); FileAr.Empty();

    if (!bOK)
    {
        UE_LOG(LogBloodStain, Error, TEXT("[BS] SaveToFile failed: %s"), *Path);
    }

    {
        for (const FRecordActorSaveData& RecordActorData : SaveData.RecordActorDataArray)
        {
            // 🔽 추가 정보 로그 출력
            const int32 NumFrames = RecordActorData.RecordedFrames.Num();
            const float Duration  = NumFrames > 0 
                ? RecordActorData.RecordedFrames.Last().TimeStamp - RecordActorData.RecordedFrames[0].TimeStamp 
                : 0.0f;

            int32 BoneCount = 0;
            if (NumFrames > 0)
            {
                BoneCount = RecordActorData.RecordedFrames[0].ComponentTransforms.Num();
            }

            UE_LOG(LogBloodStain, Log, TEXT("[BS] Saved recording to %s"), *Path);
            UE_LOG(LogBloodStain, Log, TEXT("[BS] ▶ Duration: %.2f sec | Frames: %d | Sockets: %d"), 
                Duration, NumFrames, BoneCount);    
        }
    }
    return bOK;
}

bool BloodStainFileUtils::LoadFromFile(const FString& FileName, const FString& LevelName, FRecordSaveData& OutData)
{
    // 1) 파일 전체 읽기
    const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(LevelName / FileName);
    TArray<uint8> AllBytes;
    if (!FFileHelper::LoadFileToArray(AllBytes, *Path))
    {
        UE_LOG(LogBloodStain, Error, TEXT("[BS] LoadFromFile failed read: %s"), *Path);
        return false;	
    }

    // 2) 헤더 역직렬화
    FMemoryReader MemR(AllBytes, /*bIsPersistent=*/true);
    FBloodStainFileHeader FileHeader;
    MemR << FileHeader;  // 읽고 커서가 헤더 끝으로 이동
	MemR << OutData.Header;

    // 3) 남은 바이트(Payload) TArray<uint8> 로 복사
    int64 Offset = MemR.Tell();
    int64 Remain = AllBytes.Num() - Offset;
    const uint8* Ptr = AllBytes.GetData() + Offset;

    TArray<uint8> Compressed;
    Compressed.SetNumUninitialized(Remain);
    FMemory::Memcpy(Compressed.GetData(), Ptr, Remain);

    // 4) (옵션에 따라) 압축 해제 → RawBytes
    TArray<uint8> RawBytes;
    if (FileHeader.Options.Compression.Method == ECompressionMethod::None)
    {
        RawBytes = MoveTemp(Compressed);
    }
    else
    {
        if (!BloodStainCompressionUtils::DecompressBuffer(
                Compressed, RawBytes, FileHeader.Options.Compression, FileHeader.UncompressedSize))
        {
            UE_LOG(LogBloodStain, Error, TEXT("[BS] DecompressBuffer failed"));
            return false;
        }
    }

	FMemoryReader MemoryReader(RawBytes, true);
	BloodStainFileUtils_Internal::DeserializeSaveData(
		MemoryReader,
		OutData,
		FileHeader.Options.Quantization
	);
    return true;
}

bool BloodStainFileUtils::LoadHeaderFromFile(const FString& FileName, const FString& LevelName, FRecordHeaderData& OutRecordHeaderData)
{	
	const FString Path = BloodStainFileUtils_Internal::GetFullFilePath(LevelName / FileName);

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
	// 1. 기존 맵 데이터를 초기화합니다.
	OutLoadedHeaders.Empty();

	// 2. 파일 관리자 인스턴스를 가져옵니다.
	IFileManager& FileManager = IFileManager::Get();

	// 3. 검색할 디렉토리와 파일 패턴을 지정합니다.
	const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory() / LevelName;
	const FString FilePattern = FString(TEXT("*")) + BloodStainFileUtils_Internal::FILE_EXTENSION; // "*.bin"

	// 4. 파일 시스템에서 파일들을 찾습니다. (결과는 파일 이름 + 확장자)
	TArray<FString> FoundFileNamesWithExt;
	FileManager.FindFiles(FoundFileNamesWithExt, *SearchDirectory, *FilePattern);

	UE_LOG(LogBloodStain, Log, TEXT("Found %d recording files in %s."), FoundFileNamesWithExt.Num(), *SearchDirectory);

	// 5. 찾은 각 파일에 대해 로드 작업을 수행합니다.
	for (const FString& FileNameWithExt : FoundFileNamesWithExt)
	{
		// 확장자를 제거하여 순수 파일 이름(키로 사용할 이름)을 만듭니다.
		FString BaseFileName = FileNameWithExt;
		BaseFileName.RemoveFromEnd(BloodStainFileUtils_Internal::FILE_EXTENSION);

		// 기존에 만든 LoadFromFile 함수를 재활용합니다.
		FRecordHeaderData LoadedData;
		if (LoadHeaderFromFile(BaseFileName, LevelName, LoadedData))
		{
			// 로드에 성공하면, 맵에 추가합니다.
			OutLoadedHeaders.Add(BaseFileName, LoadedData);
		}
	}

	return OutLoadedHeaders.Num();
}

int32 BloodStainFileUtils::LoadAllFiles(TMap<FString, FRecordSaveData>& OutLoadedDataMap, const FString& LevelName)
{
	// 1. 기존 맵 데이터를 초기화합니다.
	OutLoadedDataMap.Empty();

	// 2. 파일 관리자 인스턴스를 가져옵니다.
	IFileManager& FileManager = IFileManager::Get();

	// 3. 검색할 디렉토리와 파일 패턴을 지정합니다.
	const FString SearchDirectory = BloodStainFileUtils_Internal::GetSaveDirectory() / LevelName;
	const FString FilePattern = FString(TEXT("*")) + BloodStainFileUtils_Internal::FILE_EXTENSION; // "*.bin"

	// 4. 파일 시스템에서 파일들을 찾습니다. (결과는 파일 이름 + 확장자)
	TArray<FString> FoundFileNamesWithExt;
	FileManager.FindFiles(FoundFileNamesWithExt, *SearchDirectory, *FilePattern);

	UE_LOG(LogBloodStain, Log, TEXT("Found %d recording files in %s."), FoundFileNamesWithExt.Num(), *SearchDirectory);

	// 5. 찾은 각 파일에 대해 로드 작업을 수행합니다.
	for (const FString& FileNameWithExt : FoundFileNamesWithExt)
	{
		// 확장자를 제거하여 순수 파일 이름(키로 사용할 이름)을 만듭니다.
		FString BaseFileName = FileNameWithExt;
		BaseFileName.RemoveFromEnd(BloodStainFileUtils_Internal::FILE_EXTENSION);

		// 기존에 만든 LoadFromFile 함수를 재활용합니다.
		FRecordSaveData LoadedData;
		if (LoadFromFile(BaseFileName, LevelName, LoadedData))
		{
			// 로드에 성공하면, 맵에 추가합니다.
			OutLoadedDataMap.Add(BaseFileName, LoadedData);
		}
	}

	return OutLoadedDataMap.Num();
}

FString BloodStainFileUtils::GetFullFilePath(const FString& FileName)
{
	return BloodStainFileUtils_Internal::GetFullFilePath(FileName);
}
