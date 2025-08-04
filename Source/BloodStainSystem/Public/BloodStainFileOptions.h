/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "BloodStainFileOptions.generated.h"

/**
 * @brief Supported compression algorithms
 */
UENUM(BlueprintType)
enum class ECompressionMethod : uint8
{
	None  UMETA(DisplayName = "None"),
	Zlib  UMETA(DisplayName = "Zlib"),
	Gzip  UMETA(DisplayName = "Gzip"),
	LZ4   UMETA(DisplayName = "LZ4")  
};

/**
 * @brief Supported transform quantization methods.
 *
 * - None: No quantization (stores full FTransform).
 * - Standard_High: High‑precision quantization (uses FQuantizedTransform_High).
 * - Standard_Medium: Medium quantization (uses FQuantizedTransform_Medium).
 * - Standard_Low: Lowest‑bit quantization (uses FQuantizedTransform_Lowest).
 */
UENUM(BlueprintType)
enum class ETransformQuantizationMethod : uint8
{
	None,            
	Standard_High,   
	Standard_Medium,
	Standard_Low     
};

/**
 * @brief High-level file I/O options for BloodStain recordings
 */
USTRUCT(BlueprintType)
struct FBloodStainFileOptions
{
	GENERATED_BODY()

	/** Compression settings for file payload */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Compression")
	ECompressionMethod CompressionOption = ECompressionMethod::Zlib;

	/** Quantization settings for bone transforms */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Quantization")
	ETransformQuantizationMethod QuantizationOption = ETransformQuantizationMethod::Standard_Medium;
	
	friend FArchive& operator<<(FArchive& Ar, FBloodStainFileOptions& Options)
	{
		Ar << Options.CompressionOption;
		Ar << Options.QuantizationOption;
		return Ar;
	}
};

/**
 * @brief Header prepended to all BloodStain data files
 */
USTRUCT()
struct FBloodStainFileHeader
{
    GENERATED_BODY()

	/** [Unused] Magic identifier ('RStn') and version */
    uint32 Magic = 0x5253746E;
    uint32 Version = 1;

	/** File I/O options */
    UPROPERTY()
    FBloodStainFileOptions Options;

	/** Size of the uncompressed payload in bytes */
	UPROPERTY()
	int64 UncompressedSize = 0;

    friend FArchive& operator<<(FArchive& Ar, FBloodStainFileHeader& Header)
	{
		Ar << Header.Magic;
		Ar << Header.Version;
		Ar << Header.Options;
		Ar << Header.UncompressedSize;
		return Ar;
	}
};