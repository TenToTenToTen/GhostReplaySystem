#include "BloodStainFileUtils.h"

#include "BloodStainCompressionUtils.h"
#include "BloodStainSystem.h"

#include "Serialization/BufferArchive.h"

namespace BloodStainFileUtils_Internal
{
	const TCHAR* FILE_EXTENSION = TEXT(".bin");

	FString GetSaveDirectory()
	{
		return FPaths::ProjectSavedDir() / TEXT("BloodStain");
	}

	FString GetFullFilePath(const FString& FileNameWithExt)
	{
		return GetSaveDirectory() / FileNameWithExt;
	}
}

// bool FBloodStainFileUtils::SaveToFile(const FRecordSavedData& SaveData, const FString& FileName,const FBloodStainFileOptions& Options)
// {
// 	// 1) 디렉토리 보장
// 	const FString Dir = FPaths::ProjectSavedDir() / TEXT("BloodStain");
// 	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
//
// 	// 2) 전체 경로 + .bin
// 	const FString FullPath = Dir / (FileName + TEXT(".bin"));
//
// 	// 3) 구조체 복사본 생성 (const-correctness)
// 	FRecordSavedData LocalCopy = SaveData;
//
// 	// 4) 바이너리 직렬화
// 	FBufferArchive Archive;
// 	Archive << LocalCopy;  // operator<< 로 멤버 일괄 직렬화
//
// 	// 5) 디스크 쓰기
// 	const bool bSuccess = FFileHelper::SaveArrayToFile(Archive, *FullPath);
//
// 	// 6) 메모리 해제
// 	Archive.FlushCache();
// 	Archive.Empty();
//
// 	if (!bSuccess)
// 	{
// 		UE_LOG(LogBloodStain, Error, TEXT("[BloodStainFileUtils] Failed to save to %s"), *FullPath);
// 	}
// 	else
// 	{
// 		UE_LOG(LogBloodStain, Log, TEXT("[BloodStainFileUtils] Saved recording to %s"), *FullPath);
// 	}
//
// 	return bSuccess;
// }
//
// bool FBloodStainFileUtils::LoadFromFile(FRecordSavedData& OutData, const FString& FileName)
// {
// 	// 1) 전체 경로
// 	const FString Dir      = FPaths::ProjectSavedDir() / TEXT("BloodStain");
// 	const FString FullPath = Dir / (FileName + TEXT(".bin"));
//
// 	// 2) 파일을 바이트 배열로 읽기
// 	TArray<uint8> Binary;
// 	if (!FFileHelper::LoadFileToArray(Binary, *FullPath))
// 	{
// 		UE_LOG(LogBloodStain, Error, TEXT("[BloodStainFileUtils] Failed to load file %s"), *FullPath);
// 		return false;
// 	}
//
// 	// 3) 이진에서 구조체로 역직렬화
// 	FMemoryReader FromBinary(Binary, /*bIsPersistent=*/ true);
// 	FromBinary << OutData;  // operator<< 로 일괄 역직렬화
// 	FromBinary.Close();
//
// 	UE_LOG(LogBloodStain, Log, TEXT("[BloodStainFileUtils] Loaded recording from %s"), *FullPath);
// 	return true;
// }

bool FBloodStainFileUtils::SaveToFile(
	const FRecordSaveData&       SaveData,
	const FString&                FileName,
	const FBloodStainFileOptions& Options)
{
	// 1) Raw 직렬화 → RawBytes
	FRecordSaveData LocalCopy = SaveData; 
	FBufferArchive RawAr;
	RawAr << LocalCopy;

	TArray<uint8> RawBytes;
	RawBytes.Append(RawAr.GetData(), RawAr.Num());

	// 2) (옵션에 따라) 압축 → Payload
	TArray<uint8> Payload;
	if (Options.Compression.Method == EBSFCompressionMethod::None)
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
	FBloodStainFileHeader Header;
	Header.Options          = Options;
	Header.UncompressedSize = RawBytes.Num();

	// 4) 파일 아카이브: Header + Payload
	FBufferArchive FileAr;
	FileAr << Header;
	FileAr.Serialize(Payload.GetData(), Payload.Num());

	const FString Path = GetFullFilePath(FileName);
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

bool FBloodStainFileUtils::LoadFromFile(FRecordSaveData& OutData, const FString& FileName)
{
	// 1) 파일 전체 읽기
	const FString Path = GetFullFilePath(FileName);
	TArray<uint8> AllBytes;
	if (!FFileHelper::LoadFileToArray(AllBytes, *Path))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] LoadFromFile failed read: %s"), *Path);
		return false;
	}

	// 2) 헤더 역직렬화
	FMemoryReader MemR(AllBytes, /*bIsPersistent=*/true);
	FBloodStainFileHeader Header;
	MemR << Header;  // 읽고 커서가 헤더 끝으로 이동

	// 3) 남은 바이트(Payload) TArray<uint8> 로 복사
	int64 Offset = MemR.Tell();
	int64 Remain = AllBytes.Num() - Offset;
	const uint8* Ptr = AllBytes.GetData() + Offset;

	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(Remain);
	FMemory::Memcpy(Compressed.GetData(), Ptr, Remain);

	// 4) (옵션에 따라) 압축 해제 → RawBytes
	TArray<uint8> RawBytes;
	if (Header.Options.Compression.Method == EBSFCompressionMethod::None)
	{
		RawBytes = MoveTemp(Compressed);
	}
	else
	{
		if (!BloodStainCompressionUtils::DecompressBuffer(
				Compressed, RawBytes, Header.Options.Compression,Header.UncompressedSize))
		{
			UE_LOG(LogBloodStain, Error, TEXT("[BS] DecompressBuffer failed"));
			return false;
		}
	}

	// 5) RawBytes → OutData 역직렬화
	FMemoryReader DataR(RawBytes, /*bIsPersistent=*/true);
	DataR << OutData;  // operator<<(FArchive&, FRecordSavedData&)

	return true;
}

int32 FBloodStainFileUtils::LoadAllFiles(TMap<FString, FRecordSaveData>& OutLoadedDataMap, const FString& LevelName)
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
		if (LoadFromFile(LoadedData, LevelName / BaseFileName))
		{
			// 로드에 성공하면, 맵에 추가합니다.
			OutLoadedDataMap.Add(BaseFileName, LoadedData);
		}
	}

	return OutLoadedDataMap.Num();
}

FString FBloodStainFileUtils::GetFullFilePath(const FString& FileName)
{
	FString Dir = FPaths::ProjectSavedDir() / TEXT("BloodStain");
	IFileManager::Get().MakeDirectory(*Dir, /*Tree*/true);
	return Dir / (FileName + TEXT(".bin"));
}
