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
#include "GhostData.h"    // FRecordSavedData, FRecordFrame 정의
#include "BloodStainFileOptions.h" // FBloodStainFileOptions 정의

/**
 * FBloodStainFileUtils
 *  - FRecordSavedData 의 바이너리 직렬화/역직렬화
 *  - Saved/BloodStain 폴더에 .bin 확장자로 저장/로드
 */
struct FBloodStainFileUtils
{
	/** 
	 * SaveData를 Project/Saved/BloodStain/<FileName>.bin 으로 이진 저장
	 * @param SaveData  작성할 데이터
	 * @param FileName  확장자 없이 쓸 파일 이름
	 * @param Options
	 * @return 성공 여부
	 */
	static bool SaveToFile(const FRecordSaveData& SaveData, const FString& FileName, const FBloodStainFileOptions& Options = FBloodStainFileOptions());
	/**
	 * Project/Saved/BloodStain/<FileName>.bin 에서 이진 로드하여 OutData에 채움
	 * @param OutData   읽어들인 데이터를 담을 구조체 (empty여도 덮어쓰기)
	 * @param FileName  확장자 없이 쓸 파일 이름
	 * @return 성공 여부
	 */
	static bool LoadFromFile(const FString& FileName, const FString& LevelName, FRecordSaveData& OutData);


	static bool LoadHeaderFromFile(const FString& FileName, const FString& LevelName, FRecordHeaderData& OutRecordHeaderData);

	static int32 LoadHeadersForAllFiles(TMap<FString, FRecordHeaderData>& OutLoadedHeaders, const FString& LevelName);
	
	/**
	 * 저장 디렉토리에서 모든 녹화 파일을 찾아 로드
	 * @param OutLoadedDataMap 파일 이름(확장자 제외)을 키로, 로드된 데이터를 값으로 하는 맵
	 * @return 성공적으로 로드한 파일의 개수를 반환
	 */
	static int32 LoadAllFiles(TMap<FString, FRecordSaveData>& OutLoadedDataMap, const FString& LevelName);

private:
	static FString GetFullFilePath(const FString& FileName);


private:
	// // Raw-float 경로
	// static bool SaveRaw(
	// 	FRecordSavedData        SaveData,  // by-value 복사
	// 	const FString&          FullPath,
	// 	const FBloodStainFileOptions& Options,
	// 	FBloodStainFileHeader   Header);   // by-value non-const
	//
	// static bool LoadRaw(
	// 	FBitReader& Reader,
	// 	FRecordSavedData& OutData, const FBloodStainFileHeader& Header);   // by-value
	//
	// // NetQuantize 경로
	// static bool SaveQuantized(
	// 	const FRecordSavedData&       SaveData,
	// 	const FString&                FullPath,
	// 	const FBloodStainFileOptions& Options,
	// 	FBloodStainFileHeader         Header);
	//
	// static bool LoadQuantized(
	// 	FBitReader& Reader,
	// 	FRecordSavedData& OutData, const FBloodStainFileHeader& Header);
};
