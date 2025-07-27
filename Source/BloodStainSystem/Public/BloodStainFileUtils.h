/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "BloodStainFileOptions.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "GhostData.h"

/**
 * FBloodStainFileUtils
 *  - Serialize/Deserialize Binary of FRecordSavedData
 *  - Save & Load from .bin extension file in Saved/BloodStain folder
 */
namespace BloodStainFileUtils
{
	/** 
	 * Binary Save SaveData to Project/Saved/BloodStain/LevelName/<FileName>.bin
	 * @param FileName  without extension
	 * @return Success or failure
	 */
	bool SaveToFile(const FRecordSaveData& SaveData, const FString& LevelName, const FString& FileName, const FBloodStainFileOptions& Options = FBloodStainFileOptions());
	/**
	 * Project/Saved/BloodStain/<FileName>.bin 에서 이진 로드하여 OutData에 채움
	 * @param OutData   읽어들인 데이터를 담을 구조체 (empty여도 덮어쓰기)
	 * @param FileName  확장자 없이 쓸 파일 이름
	 * @return Success or failure
	 */
	bool LoadFromFile(const FString& FileName, const FString& LevelName, FRecordSaveData& OutData);

	bool LoadFromFile(const FString& RelativeFilePath, FRecordSaveData& OutData);

	/**
	 * @brief Directly loads the header and compressed original data payload from the file.
	 * @param FileName Name of the file
	 * @param LevelName Name of the level
	 * @param OutFileHeader Metadata of the file (compression/quantization options, etc.)
	 * @param OutRecordHeader Header data of the replay itself (spawn location, tags, etc.)
	 * @param OutCompressedPayload Compressed and quantized original binary data
	 * @return Success status
	 */
	bool LoadRawPayloadFromFile(const FString& FileName, const FString& LevelName, FBloodStainFileHeader& OutFileHeader, FRecordHeaderData& OutRecordHeader, TArray<uint8>& OutCompressedPayload);

	bool LoadHeaderFromFile(const FString& FileName, const FString& LevelName, FRecordHeaderData& OutRecordHeaderData);

	bool LoadHeaderFromFile(const FString& RelativeFilePath, FRecordHeaderData& OutRecordHeaderData);

	int32 LoadHeadersForAllFilesInLevel(TMap<FString, FRecordHeaderData>& OutLoadedHeaders, const TArray<FString>& LevelNames);
	
	int32 LoadHeadersForAllFilesInLevel(TMap<FString, FRecordHeaderData>& OutLoadedHeaders, const FString& LevelName);

	int32 LoadHeadersForAllFiles(TMap<FString, FRecordHeaderData>& OutLoadedHeaders);
	
	/**
	 * Finds and loads all recording files from the save directory.
	 * @param OutLoadedDataMap A map where the key is the file name (without extension) and the value is the loaded data.
	 * @return The number of files successfully loaded.
	 */
	int32 LoadAllFilesInLevel(TMap<FString, FRecordSaveData>& OutLoadedDataMap, const FString& LevelName);

	int32 LoadAllFilesInLevel(TMap<FString, FRecordSaveData>& OutLoadedDataMap, const TArray<FString>& LevelNames);

	int32 LoadAllFiles(TMap<FString, FRecordSaveData>& OutLoadedDataMap);
	
	bool DeleteFile(const FString& FileName, const FString& LevelName);

	TArray<FString> GetSavedLevelNames();

	TArray<FString> GetSavedFileNames(const FString& LevelName);
	
	FString GetFullFilePath(const FString& FileName, const FString& LevelName);

	FString GetRelativeFilePath(const FString& FileName, const FString& LevelName);
};
