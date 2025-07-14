#pragma once

#include "CoreMinimal.h"
#include "BloodStainFileOptions.h"
#include "BloodStainFileUtils.h"   // FRecordSaveData, FRecordActorSaveData, FBloodStainFileHeader
#include "QuantizationTypes.h"     // FQuantizedTransform_High / Compact

namespace BloodStainFileUtils_Internal
{
	void ComputeBoneRanges(FRecordSaveData& SaveData);
	
	/** 
	 * 한 개의 FTransform을 주어진 옵션에 따라 양자화하여 Ar에 직렬화합니다.
	 */
	void SerializeQuantizedTransform(
		FArchive& Ar,
		const FTransform& Transform,
		const FQuantizationOption& QuantOpts,
		const FBoneRange* Range = nullptr);

	/**
	 * 양자화된 형태로 Ar에 기록된 데이터를 읽어 FTransform으로 복원합니다.
	 */
	FTransform DeserializeQuantizedTransform(
		FArchive& Ar,
		const FQuantizationOption& QuantOpts,
		const FBoneRange* Range = nullptr);

	/**
	 * FRecordSaveData 전체를 RawAr에 직렬화합니다.
	 * 내부의 모든 FTransform은 QuantOpts에 따라 양자화됩니다.
	 */
	void SerializeSaveData(
		FArchive& RawAr,
		FRecordSaveData& SaveData,
		FQuantizationOption& QuantOpts);

	/**
	 * Raw 데이터(양자화+직렬화된)를 DataAr에서 읽어 FRecordSaveData로 역직렬화합니다.
	 */
	void DeserializeSaveData(
		FArchive& DataAr,
		FRecordSaveData& OutData,
		const FQuantizationOption& QuantOpts);
}
