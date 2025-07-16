/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "BloodStainFileOptions.h"
#include "QuantizationTypes.h"

/**
 * @namespace BloodStainFileUtils_Internal
 * @brief Internal helper functions for file serialization and quantization.
 *
 * This namespace contains the core logic for quantizing, serializing, and deserializing
 * replay data.
 */
namespace BloodStainFileUtils_Internal
{
	/**
	 * Computes the min/max ranges for location and scale across all frames in the save data.
	 * This is a prerequisite for 'Standard_Low' quantization.
	 * @param SaveData The replay data to process. Ranges will be computed and stored within this struct.
	 */
	void ComputeRanges(FRecordSaveData& SaveData);
	
	/** 
	 * Serializes a single FTransform to an archive using the specified quantization options.
	 * @param Ar The archive to write to.
	 * @param Transform The source transform to serialize.
	 * @param QuantOpts The quantization method and precision to use.
	 * @param Range The location range, required for 'Standard_Low' quantization.
	 * @param ScaleRange The scale range, required for 'Standard_Low' quantization.
	 */
	void SerializeQuantizedTransform(FArchive& Ar, const FTransform& Transform, const FQuantizationOption& QuantOpts, const FLocRange* Range = nullptr, const FScaleRange* ScaleRange = nullptr);

	/**
	 * Deserializes a quantized transform from an archive and reconstructs the FTransform.
	 * @param Ar The archive to read from.
	 * @param QuantOpts The quantization options used during serialization.
	 * @param Range The location range, required for 'Standard_Low' quantization.
	 * @param ScaleRange The scale range, required for 'Standard_Low' quantization.
	 * @return The reconstructed FTransform.
	 */
	FTransform DeserializeQuantizedTransform(FArchive& Ar, const FQuantizationOption& QuantOpts, const FLocRange* Range = nullptr, const FScaleRange* ScaleRange = nullptr);

	/**
	 * FRecordSaveData 전체를 RawAr에 직렬화합니다.
	 * 내부의 모든 FTransform은 QuantOpts에 따라 양자화됩니다.
	 */
	void SerializeSaveData(FArchive& RawAr,FRecordSaveData& SaveData, FQuantizationOption& QuantOpts);

	/**
	 * Raw 데이터(양자화+직렬화된)를 DataAr에서 읽어 FRecordSaveData로 역직렬화합니다.
	 */
	void DeserializeSaveData(FArchive& DataAr, FRecordSaveData& OutData, const FQuantizationOption& QuantOpts);
}
