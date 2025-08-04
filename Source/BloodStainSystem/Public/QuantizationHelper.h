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
	 * @param Transform The source transform to serialize.
	 * @param QuantOpts The quantization method and precision to use.
	 * @param LocRange The location range, required for 'Standard_Low' quantization.
	 * @param ScaleRange The scale range, required for 'Standard_Low' quantization.
	 */
	void SerializeQuantizedTransform(FArchive& Ar, const FTransform& Transform, const ETransformQuantizationMethod& QuantOpts, const FLocRange* LocRange = nullptr, const FScaleRange* ScaleRange = nullptr);

	/**
	 * Deserializes a quantized transform from an archive and reconstructs the FTransform.
	 * @param QuantOpts The quantization options used during serialization.
	 * @param LocRange The location range, only required for 'Standard_Low' option.
	 * @param ScaleRange The scale range, only required for 'Standard_Low' option.
	 * @return The reconstructed FTransform.
	 */
	FTransform DeserializeQuantizedTransform(FArchive& Ar, const ETransformQuantizationMethod& QuantOpts, const FLocRange* LocRange = nullptr, const FScaleRange* ScaleRange = nullptr);

	/**
	 * Serializes an entire FRecordSaveData object to a raw byte archive.
	 * Automatically computes ranges and quantizes all FTransform data according to the options.
	 * @param SaveData The source replay data to serialize. Its range members will be modified.
	 * @param QuantOpts The quantization options to apply to all transforms.
	 */
	void SerializeSaveData(FArchive& RawAr,FRecordSaveData& SaveData, ETransformQuantizationMethod& QuantOpts);

	/**
	 * Deserializes raw byte data from an archive into an FRecordSaveData object.
	 * Reconstructs all quantized transforms back to their original FTransform format.
	 * @param OutData The FRecordSaveData object to populate with the deserialized data.
	 * @param QuantOpts The quantization options used when the data was originally saved.
	 */
	void DeserializeSaveData(FArchive& DataAr, FRecordSaveData& OutData, const ETransformQuantizationMethod& QuantOpts);
}
